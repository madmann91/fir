#pragma once

#include "support/datatypes.h"
#include "support/graph.h"
#include "support/immutable_set.h"

#include "liveness.h"

struct cfg;

IMMUTABLE_SET_DECL(block_list, struct graph_node*, PUBLIC)

struct schedule {
    struct cfg* cfg;
    struct node_map early_blocks;
    struct node_map late_blocks;
    struct node_vec early_stack;
    struct node_vec late_stack;
    struct liveness liveness;
    struct block_list_pool block_list_pool;
};

[[nodiscard]] struct schedule schedule_create(struct cfg*);
void schedule_destroy(struct schedule*);

const struct block_list* schedule_find_blocks(struct schedule*, const struct fir_node*);
void schedule_list_block_contents(struct schedule* schedule, struct node_vec* block_contents);
