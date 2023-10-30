#pragma once

#include "support/graph.h"

#include <stddef.h>

struct dom_tree_node {
    struct graph_node* idom;
    size_t depth;
};

struct dom_tree {
    struct dom_tree_node* nodes;
    size_t node_count;
};

[[nodiscard]] struct dom_tree dom_tree_create(struct graph*, size_t user_data_index, enum graph_dir dir);
void dom_tree_destroy(struct dom_tree*);
