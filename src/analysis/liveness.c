#include "liveness.h"

#include <assert.h>

struct liveness liveness_create(void) {
    return (struct liveness) {
        .stack = graph_node_vec_create(),
        .partially_live_blocks = graph_node_set_create(),
        .fully_live_blocks = graph_node_set_create()
    };
}

void liveness_destroy(struct liveness* liveness) {
    graph_node_vec_destroy(&liveness->stack);
    graph_node_set_destroy(&liveness->partially_live_blocks);
    graph_node_set_destroy(&liveness->fully_live_blocks);
}

void liveness_reset(struct liveness* liveness) {
    graph_node_vec_clear(&liveness->stack);
    graph_node_set_clear(&liveness->partially_live_blocks);
    graph_node_set_clear(&liveness->fully_live_blocks);
}

static inline void enqueue_block(struct liveness* liveness, const struct graph_node* block) {
    if (graph_node_set_insert(&liveness->partially_live_blocks, (struct graph_node**)&block))
        graph_node_vec_push(&liveness->stack, (struct graph_node**)&block);
}

void liveness_mark_blocks(
    struct liveness* liveness,
    const struct graph_node* def,
    const struct graph_node* use)
{
    assert(graph_node_vec_is_empty(&liveness->stack));
    enqueue_block(liveness, use);
    graph_node_set_insert(&liveness->fully_live_blocks, (struct graph_node**)&use);
    while (!graph_node_vec_is_empty(&liveness->stack)) {
        const struct graph_node* block = *graph_node_vec_pop(&liveness->stack);
        if (block == def)
            continue;
        GRAPH_FOREACH_INCOMING_EDGE(edge, block) {
            enqueue_block(liveness, edge->from);
        }
    }
}

static inline bool is_fully_live(const struct liveness* liveness, const struct graph_node* block) {
    GRAPH_FOREACH_OUTGOING_EDGE(edge, block) {
        if (!graph_node_set_find(&liveness->fully_live_blocks, &edge->to))
            return false;
    }
    return true;
}

void liveness_finalize(struct liveness* liveness) {
    bool todo = true;
    while (todo) {
        todo = false;
        SET_FOREACH(struct graph_node*, block_ptr, liveness->partially_live_blocks) {
            if (graph_node_set_find(&liveness->fully_live_blocks, block_ptr))
                continue;
            if (is_fully_live(liveness, *block_ptr)) {
                graph_node_set_insert(&liveness->fully_live_blocks, block_ptr);
                todo = true;
            }
        }
    }
}

bool liveness_is_fully_live(const struct liveness* liveness, const struct graph_node* block) {
    return graph_node_set_find(&liveness->fully_live_blocks, (struct graph_node**)&block);
}

bool liveness_is_partially_live(const struct liveness* liveness, const struct graph_node* block) {
    return graph_node_set_find(&liveness->partially_live_blocks, (struct graph_node**)&block);
}
