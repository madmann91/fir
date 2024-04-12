#pragma once

#include <overture/graph.h>

struct liveness {
    struct graph_node_vec stack;
    struct graph_node_set partially_live_blocks;
    struct graph_node_set fully_live_blocks;
};

struct liveness liveness_create(void);
void liveness_destroy(struct liveness*);
void liveness_reset(struct liveness*);

void liveness_mark_blocks(
    struct liveness* liveness,
    const struct graph_node* def,
    const struct graph_node* use);

void liveness_finalize(struct liveness*);

bool liveness_is_fully_live(const struct liveness*, const struct graph_node*);
bool liveness_is_partially_live(const struct liveness*, const struct graph_node*);
