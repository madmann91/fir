#pragma once

#include "support/datatypes.h"
#include "support/graph.h"

struct cfg;

struct schedule {
    struct cfg* cfg;
    struct node_map blocks;
    struct node_vec* block_contents;
};

[[nodiscard]] struct schedule schedule_create(struct cfg*);
void schedule_destroy(struct schedule*);

struct node_cspan schedule_block_contents(const struct schedule*, const struct graph_node*);
const struct graph_node_set* schedule_node_blocks(const struct schedule*, const struct fir_node*);
bool schedule_is_in_block(const struct schedule*, const struct fir_node*, struct graph_node*);
