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

static inline bool is_pinned_to_source(const struct fir_node* node) {
    return
        (node->props & FIR_PROP_INVARIANT) != 0 ||
        node->tag == FIR_FUNC ||
        node->tag == FIR_GLOBAL;
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
    const struct schedule* schedule,
    const struct fir_node* node)
{
    struct graph_node** block_ptr = (struct graph_node**)node_map_find(&schedule->early_blocks, &node);
    return block_ptr ? *block_ptr : NULL;
}

static inline const struct block_list* find_late_blocks(
    const struct schedule* schedule,
    const struct fir_node* node)
{
    struct block_list** late_blocks = (struct block_list**)node_map_find(&schedule->late_blocks, &node);
    return late_blocks ? *late_blocks : NULL;
}

static inline struct graph_node* find_deepest_dom_block(
    struct graph_node* block,
    struct graph_node* other_block)
{
    return cfg_dom_tree_node(block)->depth > cfg_dom_tree_node(other_block)->depth ? block : other_block;
}

static inline struct graph_node* compute_early_block(struct schedule* schedule, const struct fir_node* node) {
    struct graph_node* early_block = schedule->cfg->graph.source;
    for (size_t i = 0; i < node->op_count; ++i) {
        if (is_pinned_to_source(node))
            continue;

        struct graph_node* op_block = find_early_block(schedule, node->ops[i]);
        if (!op_block) {
            node_vec_push(&schedule->early_stack, &node->ops[i]);
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

            struct graph_node* load_block = find_early_block(schedule, use->user);
            if (!load_block) {
                node_vec_push(&schedule->early_stack, &use->user);
                return NULL;
            }

            early_block = find_deepest_dom_block(early_block, load_block);
        }
    }

    return early_block;
}

static inline struct graph_node* schedule_early(
    struct schedule* schedule,
    const struct fir_node* target_node)
{
    assert(node_vec_is_empty(&schedule->early_stack));
    struct graph_node* early_block = find_early_block(schedule, target_node);
    if (early_block)
        return early_block;
    node_vec_push(&schedule->early_stack, &target_node);

restart:
    while (!node_vec_is_empty(&schedule->early_stack)) {
        const struct fir_node* node = *node_vec_last(&schedule->early_stack);
        assert(!find_early_block(schedule, node));

        struct graph_node* early_block = NULL;
        if (node->tag == FIR_PARAM) {
            early_block = find_func_block(schedule->cfg, FIR_PARAM_FUNC(node));
        } else if ((node->props & FIR_PROP_INVARIANT) || fir_node_is_nominal(node)) {
            early_block = schedule->cfg->graph.source;
        } else {
            early_block = compute_early_block(schedule, node);
            if (!early_block)
                goto restart;
        }
        assert(early_block);

        node_map_insert(&schedule->early_blocks, &node, (void*)&early_block);
        node_vec_pop(&schedule->early_stack);
    }

    early_block = find_early_block(schedule, target_node);
    assert(early_block);
    return early_block;
}

static inline bool collect_late_blocks(
    struct schedule* schedule,
    struct small_graph_node_vec* uses_blocks,
    const struct fir_node* node)
{
    const struct block_list* blocks = find_late_blocks(schedule, node);
    if (!blocks) {
        node_vec_push(&schedule->late_stack, &node);
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
    struct schedule* schedule,
    const struct fir_node* node)
{
    // Nodes that are not used can go in any block after the operands are available. In this case
    // we just use the earliest position, which is the earliest position where the operands are
    // available in the control-flow graph.
    if (!node->uses) {
        struct graph_node* early_block = schedule_early(schedule, node);
        return block_list_pool_insert(&schedule->block_list_pool, &early_block, 1);
    }

    // Collect the blocks where the node is used.
    struct small_graph_node_vec late_blocks;
    small_graph_node_vec_init(&late_blocks);
    for (const struct fir_use* use = node->uses; use; use = use->next) {
        if (!collect_late_blocks(schedule, &late_blocks, use->user))
            return NULL;
    }

    // Loads should be scheduled _before_ the stores that write to their memory object.
    if (node->tag == FIR_LOAD) {
        for (const struct fir_use* use = FIR_LOAD_MEM(node)->uses; use; use = use->next) {
            if (use->user->tag != FIR_STORE)
                continue;
            if (!collect_late_blocks(schedule, &late_blocks, use->user))
                return NULL;
        }
    }

    assert(late_blocks.elem_count > 0);

    // Try to group and/or move the definitions of the node earlier. This is not possible for
    // control-flow instructions, which have to appear where they are needed, always.
    if (node->ty->tag != FIR_NORET_TY) {
        struct graph_node* early_block = schedule_early(schedule, node);
        if (late_blocks.elem_count > 1) {
            compute_liveness(&schedule->liveness, &late_blocks, early_block);
            prune_live_blocks(&schedule->liveness, &late_blocks);
        }

        if (node->props & FIR_PROP_SPECULATABLE) {
            VEC_FOREACH(struct graph_node*, block_ptr, late_blocks)
                *block_ptr = find_shallowest_loop_block(early_block, *block_ptr);
        }
        prune_dominated_blocks(&late_blocks);
    }

    const struct block_list* block_list = block_list_pool_insert(
        &schedule->block_list_pool, late_blocks.elems, late_blocks.elem_count);

    small_graph_node_vec_destroy(&late_blocks);
    return block_list;
}

static inline const struct block_list* schedule_late(
    struct schedule* schedule,
    const struct fir_node* target_node)
{
    assert(node_vec_is_empty(&schedule->late_stack));
    node_vec_push(&schedule->late_stack, &target_node);
restart:
    while (!node_vec_is_empty(&schedule->late_stack)) {
        const struct fir_node* node = *node_vec_last(&schedule->late_stack);

        const struct block_list* late_blocks = NULL;
        if (node->tag == FIR_PARAM) {
            struct graph_node* param_block = find_func_block(schedule->cfg, FIR_PARAM_FUNC(node));
            late_blocks = block_list_pool_insert(&schedule->block_list_pool, &param_block, 1);
        } else if (node->tag == FIR_FUNC && fir_node_is_cont_ty(node->ty)) {
            struct graph_node* block = find_func_block(schedule->cfg, node);
            late_blocks = block_list_pool_insert(&schedule->block_list_pool, &block, 1);
        } else if ((node->props & FIR_PROP_INVARIANT) || fir_node_is_nominal(node)) {
            late_blocks = block_list_pool_insert(&schedule->block_list_pool, &schedule->cfg->graph.source, 1);
        } else {
            late_blocks = compute_late_blocks(schedule, node);
            if (!late_blocks)
                goto restart;
        }
        assert(late_blocks);
        assert(late_blocks->elem_count > 0);

        node_map_insert(&schedule->late_blocks, &node, (void**)&late_blocks);
        node_vec_pop(&schedule->late_stack);
    }

    const struct block_list* late_blocks = find_late_blocks(schedule, target_node);
    assert(late_blocks);
    return late_blocks;
}

struct schedule schedule_create(struct cfg* cfg) {
    return (struct schedule) {
        .cfg = cfg,
        .early_blocks = node_map_create(),
        .late_blocks = node_map_create(),
        .early_stack = node_vec_create(),
        .late_stack = node_vec_create(),
        .liveness = liveness_create(),
        .block_list_pool = block_list_pool_create(),
    };
}

void schedule_destroy(struct schedule* schedule) {
    block_list_pool_destroy(&schedule->block_list_pool);
    node_map_destroy(&schedule->early_blocks);
    node_map_destroy(&schedule->late_blocks);
    node_vec_destroy(&schedule->early_stack);
    node_vec_destroy(&schedule->late_stack);
    liveness_destroy(&schedule->liveness);
    memset(schedule, 0, sizeof(struct schedule));
}

const struct block_list* schedule_find_blocks(struct schedule* schedule, const struct fir_node* node) {
    return schedule_late(schedule, node);
}

void schedule_list_block_contents(struct schedule* schedule, struct node_vec* block_contents) {
    struct unique_node_stack stack = unique_node_stack_create();
    VEC_REV_FOREACH(struct graph_node*, block_ptr, schedule->cfg->post_order) {
        const struct fir_node* func = cfg_block_func(*block_ptr);
        if (!func || func->tag != FIR_FUNC || !FIR_FUNC_BODY(func))
            continue;
        assert(unique_node_stack_is_empty(&stack));
        unique_node_stack_push(&stack, &FIR_FUNC_BODY(func));
    restart:
        while (!unique_node_stack_is_empty(&stack)) {
            const struct fir_node* node = *unique_node_stack_last(&stack);

            for (size_t i = 0; i < node->op_count; ++i) {
                if (node->ops[i] && unique_node_stack_push(&stack, &node->ops[i]))
                    goto restart;
            }

            unique_node_stack_pop(&stack);

            if ((node->props & FIR_PROP_INVARIANT) == 0) {
                const struct block_list* blocks = schedule_find_blocks(schedule, node);
                for (size_t i = 0; i < blocks->elem_count; ++i)
                    node_vec_push(&block_contents[blocks->elems[i]->index], &node);
            }
        }
    }
    unique_node_stack_destroy(&stack);
}
