#include "schedule.h"

#include "fir/node.h"

#include "analysis/dom_tree.h"
#include "analysis/loop_tree.h"
#include "analysis/cfg.h"
#include "analysis/liveness.h"

#include "support/datatypes.h"
#include "support/graph.h"
#include "support/queue.h"
#include "support/vec.h"

#include <string.h>
#include <stdlib.h>

// Scheduler based on the ideas outlined in C. Click's "Global Code Motion -- Global Value Numbering"
// paper. The difference is that this scheduler does not produce partially-dead code, unlike
// C. Click's algorithm. This is achieved with two major design changes: First, each node is
// assigned to (potentially) multiple basic-blocks. Second, in order to avoid partially-dead code,
// a live range analysis is run before deciding the block assignment.

static inline uint32_t hash_block(uint32_t h, struct graph_node* const* block) {
    return hash_uint64(h, (*block)->index);
}

static inline int cmp_block(
    struct graph_node* const* block_ptr,
    struct graph_node* const* other_ptr)
{
    size_t block_index = (*block_ptr)->index;
    size_t other_index = (*other_ptr)->index;
    if (block_index < other_index)
        return -1;
    else if (block_index > other_index)
        return 1;
    else
        return 0;
}

IMMUTABLE_SET_IMPL(block_list, struct graph_node*, hash_block, cmp_block, PUBLIC)

struct scheduler {
    struct cfg* cfg;
    struct node_map early_blocks;
    struct node_map late_blocks;
    struct node_vec early_stack;
    struct node_vec late_stack;
    struct unique_node_stack visit_stack;
    struct liveness liveness;
    struct node_vec* block_contents;
    struct block_list_pool block_list_pool;
};

static inline bool is_in_schedule(const struct fir_node* node) {
    return
        (node->props & FIR_PROP_INVARIANT) == 0 &&
        !fir_node_is_ty(node) &&
        node->tag != FIR_FUNC &&
        node->tag != FIR_GLOBAL;
}

static inline struct graph_node* find_func_block(struct cfg* cfg, const struct fir_node* func) {
    // The node might be the parameter of a block, or the parameter of the surrounding function.
    // We therefore return the block itself in the former case, or the entry block in the latter.
    assert(func->tag == FIR_FUNC);
    if (fir_node_is_cont_ty(func->ty)) {
        struct graph_node* block = cfg_find(cfg, func);
        assert(block && "accessing block that is not in CFG");
        return block;
    }
    return cfg->graph.source;
}

static inline struct graph_node* find_early_block(
    const struct scheduler* scheduler,
    const struct fir_node* node)
{
    struct graph_node** block_ptr = (struct graph_node**)node_map_find(&scheduler->early_blocks, &node);
    return block_ptr ? *block_ptr : NULL;
}

static inline const struct block_list* find_late_blocks(
    const struct scheduler* scheduler,
    const struct fir_node* node)
{
    struct block_list** late_blocks = (struct block_list**)node_map_find(&scheduler->late_blocks, &node);
    return late_blocks ? *late_blocks : NULL;
}

static inline struct graph_node* find_deepest_dom_block(
    struct graph_node* block,
    struct graph_node* other_block)
{
    return cfg_dom_tree_node(block)->depth > cfg_dom_tree_node(other_block)->depth ? block : other_block;
}

static inline struct graph_node* compute_early_block(struct scheduler* scheduler, const struct fir_node* node) {
    struct graph_node* early_block = scheduler->cfg->graph.source;
    for (size_t i = 0; i < node->op_count; ++i) {
        if (!is_in_schedule(node))
            continue;

        struct graph_node* op_block = find_early_block(scheduler, node->ops[i]);
        if (!op_block) {
            node_vec_push(&scheduler->early_stack, &node->ops[i]);
            return NULL;
        }

        // Pick the node that is the deepest in the dominator tree.
        early_block = find_deepest_dom_block(early_block, op_block);
    }

    if (node->tag == FIR_STORE) {
        // Stores need to be scheduled at the earliest _after_ all the loads on the same memory object.
        for (const struct fir_use* use = node->ops[0]->uses; use; use = use->next) {
            if (use->user->tag != FIR_LOAD)
                continue;

            struct graph_node* load_block = find_early_block(scheduler, use->user);
            if (!load_block) {
                node_vec_push(&scheduler->early_stack, &use->user);
                return NULL;
            }

            early_block = find_deepest_dom_block(early_block, load_block);
        }
    }

    return early_block;
}

static inline struct graph_node* schedule_early(
    struct scheduler* scheduler,
    const struct fir_node* target_node)
{
    assert(node_vec_is_empty(&scheduler->early_stack));
    struct graph_node* early_block = find_early_block(scheduler, target_node);
    if (early_block)
        return early_block;
    node_vec_push(&scheduler->early_stack, &target_node);

restart:
    while (!node_vec_is_empty(&scheduler->early_stack)) {
        const struct fir_node* node = *node_vec_last(&scheduler->early_stack);
        assert(!find_early_block(scheduler, node));

        struct graph_node* early_block = NULL;
        if (node->tag == FIR_PARAM) {
            early_block = find_func_block(scheduler->cfg, node->ops[0]);
        } else if ((node->props & FIR_PROP_INVARIANT) || fir_node_is_nominal(node)) {
            early_block = scheduler->cfg->graph.source;
        } else {
            early_block = compute_early_block(scheduler, node);
            if (!early_block)
                goto restart;
        }
        assert(early_block);

        node_map_insert(&scheduler->early_blocks, &node, (void*)&early_block);
        node_vec_pop(&scheduler->early_stack);
    }

    early_block = find_early_block(scheduler, target_node);
    assert(early_block);
    return early_block;
}

static inline bool collect_late_blocks(
    struct scheduler* scheduler,
    struct small_graph_node_vec* uses_blocks,
    const struct fir_node* node)
{
    const struct block_list* blocks = find_late_blocks(scheduler, node);
    if (!blocks) {
        node_vec_push(&scheduler->late_stack, &node);
        return false;
    }

    IMMUTABLE_SET_FOREACH(struct graph_node*, block_ptr, *blocks)
        small_graph_node_vec_push(uses_blocks, block_ptr);
    return true;
}

static inline void compute_liveness(
    struct liveness* liveness,
    const struct small_graph_node_vec* uses_blocks,
    struct graph_node* early_block)
{
    liveness_reset(liveness);
    VEC_FOREACH(struct graph_node*, block_ptr, *uses_blocks)
        liveness_mark_blocks(liveness, early_block, *block_ptr);
    liveness_finalize(liveness);
}

static inline void prune_live_blocks(struct liveness* liveness, struct small_graph_node_vec* uses_blocks) {
    // Remove and replace nodes that are dominated by fully live nodes. This is done by counting,
    // for each live block, how many uses blocks it dominates. If that number is >1, the uses blocks
    // are removed and the live block is used instead.
    SET_FOREACH(struct graph_node*, live_block_ptr, liveness->fully_live_blocks) {
        if (uses_blocks->elem_count <= 1)
            return;

        size_t dominated_count = 0;
        VEC_FOREACH(struct graph_node*, use_block_ptr, *uses_blocks) {
            dominated_count += cfg_is_dominated_by(*use_block_ptr, *live_block_ptr) ? 1 : 0;
            if (dominated_count > 1)
                break;
        }

        if (dominated_count > 1) {
            size_t elem_count = 0;
            for (size_t i = 0; i < uses_blocks->elem_count; ++i) {
                if (!cfg_is_dominated_by(uses_blocks->elems[i], *live_block_ptr))
                    uses_blocks->elems[elem_count++] = uses_blocks->elems[i];
            }
            assert(elem_count <= uses_blocks->elem_count - 1);
            uses_blocks->elems[elem_count++] = *live_block_ptr;
            small_graph_node_vec_resize(uses_blocks, elem_count);
        }
    }
}

static inline struct graph_node* find_shallowest_loop_block(
    struct graph_node* early_block,
    struct graph_node* use_block)
{
    // Tries to move the use block to an earlier position if it reduces the loop depth.
    assert(cfg_dom_tree_node(early_block)->depth <= cfg_dom_tree_node(use_block)->depth);
    size_t min_depth = cfg_loop_tree_node(early_block)->loop_depth;
    if (cfg_loop_tree_node(use_block)->loop_depth == min_depth)
        return use_block;
    for (struct graph_node* block = use_block; block != early_block; block = cfg_dom_tree_node(block)->idom) {
        size_t depth = cfg_loop_tree_node(block)->loop_depth;
        if (depth < cfg_loop_tree_node(use_block)->loop_depth)
            use_block = block;
        if (depth == min_depth)
            break;
    }
    return use_block;
}

static inline void prune_dominated_blocks(struct small_graph_node_vec* blocks) {
    if (blocks->elem_count <= 1)
        return;
    size_t elem_count = 0;
    for (size_t i = 0; i < blocks->elem_count; ++i) {
        bool needs_pruning = true;
        for (size_t j = 0; j < blocks->elem_count; ++j) {
            if (i == j)
                continue;
            needs_pruning &= dom_tree_node_is_dominated_by(
                cfg_dom_tree_node(blocks->elems[i]),
                cfg_dom_tree_node(blocks->elems[j]),
                CFG_DOM_TREE_INDEX);
        }
        if (needs_pruning)
            continue;
        blocks->elems[elem_count++] = blocks->elems[i];
    }
    small_graph_node_vec_resize(blocks, elem_count);
}

static inline const struct block_list* compute_late_blocks(
    struct scheduler* scheduler,
    const struct fir_node* node)
{
    // Collect the blocks where the node is used.
    struct small_graph_node_vec late_blocks;
    small_graph_node_vec_init(&late_blocks);
    for (const struct fir_use* use = node->uses; use; use = use->next) {
        if (!collect_late_blocks(scheduler, &late_blocks, use->user))
            return NULL;
    }

    // Loads should be scheduled _before_ the stores that write to their memory object.
    if (node->tag == FIR_LOAD) {
        for (const struct fir_use* use = node->ops[0]->uses; use; use = use->next) {
            if (use->user->tag != FIR_STORE)
                continue;
            if (!collect_late_blocks(scheduler, &late_blocks, use->user))
                return NULL;
        }
    }
    assert(late_blocks.elem_count > 0);

    // Try to group and/or move the definitions of the node earlier. This is not possible for
    // control-flow instructions, which have to appear where they are needed, always.
    if (node->ty->tag != FIR_NORET_TY) {
        struct graph_node* early_block = schedule_early(scheduler, node);
        if (late_blocks.elem_count > 1) {
            compute_liveness(&scheduler->liveness, &late_blocks, early_block);
            prune_live_blocks(&scheduler->liveness, &late_blocks);
        }

        if (node->props & FIR_PROP_SPECULATABLE) {
            VEC_FOREACH(struct graph_node*, block_ptr, late_blocks)
                *block_ptr = find_shallowest_loop_block(early_block, *block_ptr);
        }
        prune_dominated_blocks(&late_blocks);
    }

    const struct block_list* block_list = block_list_pool_insert(
        &scheduler->block_list_pool, late_blocks.elems, late_blocks.elem_count);

    small_graph_node_vec_destroy(&late_blocks);
    return block_list;
}

static inline const struct block_list* schedule_late(
    struct scheduler* scheduler,
    const struct fir_node* target_node)
{
    assert(node_vec_is_empty(&scheduler->late_stack));
    node_vec_push(&scheduler->late_stack, &target_node);
restart:
    while (!node_vec_is_empty(&scheduler->late_stack)) {
        const struct fir_node* node = *node_vec_last(&scheduler->late_stack);

        const struct block_list* late_blocks = NULL;
        if (node->tag == FIR_PARAM) {
            struct graph_node* param_block = find_func_block(scheduler->cfg, node->ops[0]);
            late_blocks = block_list_pool_insert(&scheduler->block_list_pool, &param_block, 1);
        } else if (node->tag == FIR_FUNC && fir_node_is_cont_ty(node->ty)) {
            struct graph_node* block = find_func_block(scheduler->cfg, node);
            late_blocks = block_list_pool_insert(&scheduler->block_list_pool, &block, 1);
        } else if ((node->props & FIR_PROP_INVARIANT) || fir_node_is_nominal(node)) {
            late_blocks = block_list_pool_insert(&scheduler->block_list_pool, &scheduler->cfg->graph.source, 1);
        } else {
            late_blocks = compute_late_blocks(scheduler, node);
            if (!late_blocks)
                goto restart;
        }
        assert(late_blocks);
        assert(late_blocks->elem_count > 0);

        node_map_insert(&scheduler->late_blocks, &node, (void**)&late_blocks);
        node_vec_pop(&scheduler->late_stack);
    }

    const struct block_list* late_blocks = find_late_blocks(scheduler, target_node);
    assert(late_blocks);
    return late_blocks;
}

static void visit_node(struct scheduler* scheduler, const struct fir_node* node) {
    // Visit operands in post-order, and place them in the blocks in which they have been scheduled
    unique_node_stack_push(&scheduler->visit_stack, &node);
restart:
    while (!unique_node_stack_is_empty(&scheduler->visit_stack)) {
        const struct fir_node* node = *unique_node_stack_last(&scheduler->visit_stack);
        assert(is_in_schedule(node));

        for (size_t i = 0; i < node->op_count; ++i) {
            if (is_in_schedule(node->ops[i]) && unique_node_stack_push(&scheduler->visit_stack, &node->ops[i]))
                goto restart;
        }

        unique_node_stack_pop(&scheduler->visit_stack);

        if (!fir_node_is_nominal(node)) {
            const struct block_list* late_blocks = schedule_late(scheduler, node);
            for (size_t i = 0; i < late_blocks->elem_count; ++i)
                node_vec_push(&scheduler->block_contents[late_blocks->elems[i]->index], &node);
        }
    }
}

static void run_scheduler(struct scheduler* scheduler) {
    // List blocks in post-order
    unique_node_stack_clear(&scheduler->visit_stack);
    VEC_FOREACH(struct graph_node*, block_ptr, scheduler->cfg->post_order) {
        const struct fir_node* func = cfg_block_func(*block_ptr);
        if (!func || !is_in_schedule(func->ops[0]))
            continue;
        visit_node(scheduler, func->ops[0]);
    }
}

struct schedule schedule_create(struct cfg* cfg) {
    struct scheduler scheduler = {
        .cfg = cfg,
        .early_blocks = node_map_create(),
        .late_blocks = node_map_create(),
        .early_stack = node_vec_create(),
        .late_stack = node_vec_create(),
        .liveness = liveness_create(),
        .visit_stack = unique_node_stack_create(),
        .block_list_pool = block_list_pool_create(),
        .block_contents = xcalloc(cfg->graph.node_count, sizeof(struct node_vec)),
    };

    run_scheduler(&scheduler);

    node_map_destroy(&scheduler.early_blocks);
    node_vec_destroy(&scheduler.early_stack);
    node_vec_destroy(&scheduler.late_stack);

    unique_node_stack_destroy(&scheduler.visit_stack);

    liveness_destroy(&scheduler.liveness);

    return (struct schedule) {
        .cfg = cfg,
        .blocks = scheduler.late_blocks,
        .block_contents = scheduler.block_contents,
        .block_list_pool = scheduler.block_list_pool
    };
}

void schedule_destroy(struct schedule* schedule) {
    for (size_t i = 0; i < schedule->cfg->graph.node_count; ++i)
        node_vec_destroy(&schedule->block_contents[i]);

    block_list_pool_destroy(&schedule->block_list_pool);
    node_map_destroy(&schedule->blocks);
    free(schedule->block_contents);
    memset(schedule, 0, sizeof(struct schedule));
}

struct const_node_span schedule_block_contents(const struct schedule* schedule, const struct graph_node* block) {
    const struct node_vec* block_contents = &schedule->block_contents[block->index];
    return (struct const_node_span) { block_contents->elems, block_contents->elem_count };
}

const struct block_list* schedule_blocks_of(const struct schedule* schedule, const struct fir_node* node) {
    const struct block_list** blocks = (const struct block_list**)node_map_find(&schedule->blocks, &node);
    return blocks ? *blocks : NULL;
}

bool schedule_is_in_block(
    const struct schedule* schedule,
    const struct fir_node* node,
    struct graph_node* block)
{
    const struct block_list* blocks = schedule_blocks_of(schedule, node);
    return blocks ? (block_list_find(blocks, &block) != NULL) : false;
}
