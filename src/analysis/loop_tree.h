#pragma once

#include "support/vec.h"
#include "support/graph.h"

#include <stddef.h>

enum loop_type {
    LOOP_NONHEADER,
    LOOP_REDUCIBLE,
    LOOP_IRREDUCIBLE,
    LOOP_SELF
};

struct loop_tree_node {
    size_t depth;
    size_t loop_depth;
    enum loop_type type;
    struct graph_node* parent;
};

struct loop_tree {
    struct loop_tree_node* nodes;
    size_t node_count;
};

[[nodiscard]] struct loop_tree loop_tree_create(
    const struct graph_node_vec* depth_first_order,
    size_t depth_first_order_index,
    size_t loop_tree_index,
    enum graph_dir);

void loop_tree_destroy(struct loop_tree*);
