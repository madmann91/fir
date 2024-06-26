#pragma once

#include <overture/graph.h>

#include <stddef.h>

struct dom_tree_node {
    struct graph_node* idom;
    size_t depth;
};

struct dom_tree {
    struct dom_tree_node* nodes;
    size_t node_count;
};

[[nodiscard]] struct dom_tree dom_tree_create(
    const struct graph_node_vec* post_order,
    size_t post_order_index,
    size_t dom_tree_index,
    enum graph_dir dir);

void dom_tree_destroy(struct dom_tree*);

[[nodiscard]] bool dom_tree_node_is_dominated_by(
    struct dom_tree_node*,
    struct dom_tree_node*,
    size_t dom_tree_index);
