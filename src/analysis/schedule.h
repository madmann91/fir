#pragma once

#include "support/datatypes.h"
#include "support/graph.h"
#include "support/immutable_set.h"

struct cfg;

IMMUTABLE_SET_DECL(block_list, struct graph_node*, PUBLIC)

struct schedule {
    struct cfg* cfg;
    struct node_map blocks;
    struct node_vec* block_contents;
    struct block_list_pool block_list_pool;
};

[[nodiscard]] struct schedule schedule_create(struct cfg*);
void schedule_destroy(struct schedule*);

struct const_node_span schedule_block_contents(const struct schedule*, const struct graph_node*);
const struct block_list* schedule_node_blocks(const struct schedule*, const struct fir_node*);
bool schedule_is_in_block(const struct schedule*, const struct fir_node*, struct graph_node*);
