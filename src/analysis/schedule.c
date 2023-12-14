#include "schedule.h"

#include "fir/node.h"

#include "analysis/dom_tree.h"
#include "analysis/loop_tree.h"
#include "analysis/cfg.h"

#include "support/datatypes.h"
#include "support/graph.h"
#include "support/queue.h"
#include "support/vec.h"

#include <string.h>

// Scheduler based on the ideas outlined in C. Click's "Global Code Motion -- Global Value Numbering"
// paper. The difference is that this scheduler does not produce partially-dead code, unlike
// C. Click's algorithm. This is achieved with two major design changes: First, each node is
// assigned to (potentially) multiple basic-blocks. Second, in order to avoid partially-dead code,
// a live range analysis is run before deciding the block assignment.

static bool cmp_dom_tree_depth(
    struct graph_node* const* graph_node_ptr,
    struct graph_node* const* other_ptr)
{
    return cfg_dom_tree_node(*graph_node_ptr)->depth < cfg_dom_tree_node(*other_ptr)->depth;
}

QUEUE_DEFINE(live_block_queue, struct graph_node*, cmp_dom_tree_depth, PRIVATE)

struct scheduler_data {
    struct cfg* cfg;

    struct node_map late_blocks;
    struct node_map early_blocks;
    struct node_vec early_stack;
    struct node_vec late_stack;

    struct node_vec visit_stack;
    struct node_set visit_set;

    struct node_vec* block_contents;

    struct graph_node_vec use_blocks;
    struct live_block_queue live_block_queue;
    struct graph_node_set live_blocks;
};

static inline struct graph_node* find_trivial_block(struct scheduler_data* scheduler, const struct fir_node* node) {
    if (fir_node_is_nominal(node) || (node->props & FIR_PROP_INVARIANT))
        return scheduler->cfg->graph.source;

    if (node->tag == FIR_PARAM) {
        // The node might be the parameter of a block, or the parameter of the surrounding function.
        // We therefore return the block itself in the former case, or the entry block in the latter.
        if (fir_node_is_cont_ty(node->ops[0]->ty)) {
            struct graph_node* block = cfg_find(scheduler->cfg, node->ops[0]);
            assert(block && "accessing block that is not in CFG");
            return block;
        } else {
            return scheduler->cfg->graph.source;
        }
    }

    return NULL;
}

static inline struct graph_node* find_early_block(struct scheduler_data* scheduler, const struct fir_node* node) {
    struct graph_node* trivial_block = find_trivial_block(scheduler, node);
    if (trivial_block)
        return trivial_block;

    struct graph_node** block_ptr = (struct graph_node**)node_map_find(&scheduler->early_blocks, &node);
    return block_ptr ? *block_ptr : NULL;
}

static inline struct graph_node* schedule_early(
    struct scheduler_data* scheduler,
    const struct fir_node* target_node)
{
    node_vec_clear(&scheduler->early_stack);
    node_vec_push(&scheduler->early_stack, &target_node);

restart:
    while (scheduler->early_stack.elem_count > 0) {
        const struct fir_node* node = *node_vec_last(&scheduler->early_stack);
        if (find_early_block(scheduler, node)) {
            node_vec_pop(&scheduler->early_stack);
            continue;
        }

        struct graph_node* early_block = scheduler->cfg->graph.source;
        for (size_t i = 0; i < node->op_count; ++i) {
            struct graph_node* op_block = find_early_block(scheduler, node->ops[i]);
            if (!op_block) {
                node_vec_push(&scheduler->early_stack, &node->ops[i]);
                goto restart;
            }
            early_block = cfg_dom_tree_node(op_block)->depth > cfg_dom_tree_node(early_block)->depth
                ? op_block : early_block;
        }

        assert(early_block);
        node_map_insert(&scheduler->early_blocks, &node, (void*)&early_block);
        node_vec_pop(&scheduler->early_stack);
    }

    struct graph_node* early_block = find_early_block(scheduler, target_node);
    assert(early_block);
    return early_block;
}

static inline void enqueue_live_block(struct scheduler_data* scheduler, struct graph_node* block) {
    if (graph_node_set_insert(&scheduler->live_blocks, &block)) {
        struct graph_node* idom = cfg_dom_tree_node(block)->idom;
        live_block_queue_push(&scheduler->live_block_queue, &idom);
    }
}

static inline struct graph_node* find_best_block(
    struct scheduler_data* scheduler,
    struct graph_node* use_block,
    struct graph_node* early_block,
    bool is_speculatable)
{
    assert(cfg_dom_tree_node(use_block)->depth >= cfg_dom_tree_node(early_block)->depth);
    struct graph_node* best_block = use_block;
    struct graph_node* cur_block  = use_block;

    const size_t early_depth = cfg_loop_tree_node(early_block)->loop_depth;
    size_t best_depth = cfg_loop_tree_node(best_block)->loop_depth;

    while (cur_block != early_block) {
        cur_block = cfg_dom_tree_node(cur_block)->idom;

        const size_t loop_depth = cfg_loop_tree_node(cur_block)->loop_depth;
        if (!graph_node_set_find(&scheduler->live_blocks, &cur_block)) {
            if (!is_speculatable || loop_depth <= early_depth)
                break;
        }

        if (loop_depth < best_depth) {
            best_block = cur_block;
            best_depth = loop_depth;
        }
    }

    return best_block;
}

static inline struct graph_node_set* find_late_blocks(struct scheduler_data* scheduler, const struct fir_node* node) {
    struct graph_node_set** late_blocks = (struct graph_node_set**)node_map_find(&scheduler->late_blocks, &node);
    return late_blocks ? *late_blocks : NULL;
}

static inline bool mark_live_blocks(struct scheduler_data* scheduler, const struct fir_node* node) {
    live_block_queue_clear(&scheduler->live_block_queue);
    graph_node_set_clear(&scheduler->live_blocks);

    // Mark the blocks where the value is used as live
    for (const struct fir_use* use = node->uses; use; use = use->next) {
        struct graph_node_set* late_blocks = find_late_blocks(scheduler, use->user);
        if (!late_blocks) {
            node_vec_push(&scheduler->late_stack, &use->user);
            return false;
        }
        SET_FOREACH(struct graph_node*, block_ptr, *late_blocks)
            enqueue_live_block(scheduler, *block_ptr);
    }

    // Copy the places where the set blocks where the node is used
    graph_node_vec_clear(&scheduler->use_blocks);
    SET_FOREACH(struct graph_node*, block_ptr, scheduler->live_blocks)
        graph_node_vec_push(&scheduler->use_blocks, block_ptr);

    // Walk up the dominator tree from those nodes, starting from the deepest ones to the shallowest
    // ones, using a priority queue.
    struct graph_node* early_block = schedule_early(scheduler, node);
restart:
    while (!live_block_queue_is_empty(&scheduler->live_block_queue)) {
        struct graph_node* top = *live_block_queue_top(&scheduler->live_block_queue);
        live_block_queue_pop(&scheduler->live_block_queue);

        // Stop the search when the earliest position has been reached, or when the current node has
        // already been visited.
        if (cfg_dom_tree_node(top)->depth < cfg_dom_tree_node(early_block)->depth ||
            graph_node_set_find(&scheduler->live_blocks, &top))
            continue;

        GRAPH_FOREACH_OUTGOING_EDGE(edge, top) {
            if (!graph_node_set_find(&scheduler->live_blocks, &edge->to))
                goto restart;
        }

        enqueue_live_block(scheduler, top);
    }

    return true;
}

static inline void compute_late_blocks(
    struct scheduler_data* scheduler,
    const struct fir_node* node,
    struct graph_node_set* late_blocks)
{
    // Find the best blocks to place the node in, based on the live range analysis above
    const bool is_speculatable = fir_node_is_speculatable(node);
    struct graph_node* early_block = schedule_early(scheduler, node);
    VEC_FOREACH(struct graph_node*, block_ptr, scheduler->use_blocks) {
        struct graph_node* best_block = find_best_block(scheduler, *block_ptr, early_block, is_speculatable);
        graph_node_set_insert(late_blocks, &best_block);
    }
}

static inline const struct graph_node_set* schedule_late(
    struct scheduler_data* scheduler,
    const struct fir_node* target_node)
{
    node_vec_clear(&scheduler->late_stack);
    node_vec_push(&scheduler->late_stack, &target_node);
    while (scheduler->late_stack.elem_count > 0) {
        const struct fir_node* node = *node_vec_last(&scheduler->late_stack);
        if (find_late_blocks(scheduler, node)) {
            node_vec_pop(&scheduler->late_stack);
            continue;
        }

        struct graph_node* trivial_block = find_trivial_block(scheduler, node);
        if (!trivial_block && !mark_live_blocks(scheduler, node))
            continue;

        struct graph_node_set* late_blocks = xmalloc(sizeof(struct graph_node_set));
        *late_blocks = graph_node_set_create();
        if (trivial_block)
            graph_node_set_insert(late_blocks, &trivial_block);
        else
            compute_late_blocks(scheduler, node, late_blocks);

        node_map_insert(&scheduler->late_blocks, &node, (void*)&late_blocks);
        node_vec_pop(&scheduler->late_stack);
    }

    struct graph_node_set* late_blocks = find_late_blocks(scheduler, target_node);
    assert(late_blocks);
    return late_blocks;
}

static void visit_operands(struct scheduler_data* scheduler, const struct fir_node* node) {
    // Visit operands in post-order, and place them in the blocks in which they have been scheduled
    node_vec_clear(&scheduler->visit_stack);
    node_vec_push(&scheduler->visit_stack, &node);
restart:
    while (scheduler->visit_stack.elem_count > 0) {
        const struct fir_node* node = *node_vec_last(&scheduler->visit_stack);
        if (node_set_find(&scheduler->visit_set, &node)) {
            node_vec_pop(&scheduler->visit_stack);
            continue;
        }

        if (!find_trivial_block(scheduler, node)) {
            for (size_t i = 0; i < node->op_count; ++i) {
                if (!node_set_find(&scheduler->visit_set, &node->ops[i])) {
                    node_vec_push(&scheduler->visit_stack, &node->ops[i]);
                    goto restart;
                }
            }
        }

        node_vec_pop(&scheduler->visit_stack);
        node_set_insert(&scheduler->visit_set, &node);

        if (!fir_node_is_nominal(node)) {
            const struct graph_node_set* late_blocks = schedule_late(scheduler, node);
            SET_FOREACH(struct graph_node*, late_block_ptr, *late_blocks)
                node_vec_push(&scheduler->block_contents[(*late_block_ptr)->index], &node);
        }
    }
}

static void run_scheduler(struct scheduler_data* scheduler) {
    // List blocks in reverse post-order
    node_set_clear(&scheduler->visit_set);
    VEC_REV_FOREACH(struct graph_node*, block_ptr, scheduler->cfg->post_order) {
        const struct fir_node* func = cfg_block_func(*block_ptr);
        if (!func)
            continue;
        visit_operands(scheduler, func->ops[0]);
    }
}

struct schedule schedule_create(struct cfg* cfg) {
    struct scheduler_data scheduler = {
        .cfg = cfg,
        .early_blocks = node_map_create(),
        .late_blocks = node_map_create(),
        .early_stack = node_vec_create(),
        .late_stack = node_vec_create(),
        .visit_stack = node_vec_create(),
        .visit_set = node_set_create(),
        .block_contents = xcalloc(cfg->graph.node_count, sizeof(struct node_vec)),
        .use_blocks = graph_node_vec_create(),
        .live_block_queue = live_block_queue_create(),
        .live_blocks = graph_node_set_create()
    };

    run_scheduler(&scheduler);

    node_map_destroy(&scheduler.early_blocks);
    node_vec_destroy(&scheduler.early_stack);
    node_vec_destroy(&scheduler.late_stack);

    node_set_destroy(&scheduler.visit_set);
    node_vec_destroy(&scheduler.visit_stack);

    graph_node_vec_destroy(&scheduler.use_blocks);
    live_block_queue_destroy(&scheduler.live_block_queue);
    graph_node_set_destroy(&scheduler.live_blocks);

    return (struct schedule) {
        .cfg = cfg,
        .blocks = scheduler.late_blocks,
        .block_contents = scheduler.block_contents
    };
}

void schedule_destroy(struct schedule* schedule) {
    MAP_FOREACH_VAL(void*, late_blocks_ptr, schedule->blocks) {
        graph_node_set_destroy((struct graph_node_set*)*late_blocks_ptr);
        free(*late_blocks_ptr);
    }
    for (size_t i = 0; i < schedule->cfg->graph.node_count; ++i)
        node_vec_destroy(&schedule->block_contents[i]);

    node_map_destroy(&schedule->blocks);
    free(schedule->block_contents);
    memset(schedule, 0, sizeof(struct schedule));
}

struct node_cspan schedule_block_contents(const struct schedule* schedule, const struct graph_node* block) {
    const struct node_vec* block_contents = &schedule->block_contents[block->index];
    return (struct node_cspan) { block_contents->elems, block_contents->elem_count };
}

const struct graph_node_set* schedule_blocks_of(const struct schedule* schedule, const struct fir_node* node) {
    struct graph_node_set** blocks = (struct graph_node_set**)node_map_find(&schedule->blocks, &node);
    return blocks ? *blocks : NULL;
}

bool schedule_is_in_block(
    const struct schedule* schedule,
    const struct fir_node* node,
    struct graph_node* block)
{
    const struct graph_node_set* blocks = schedule_blocks_of(schedule, node);
    return blocks ? (graph_node_set_find(blocks, &block) != NULL) : false;
}
