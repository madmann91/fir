#pragma once

#include "support/graph.h"

#include "dom_tree.h"
#include "loop_tree.h"

struct scope;

struct cfg {
    struct graph graph;
    struct graph_node_vec post_order;
    struct graph_node_vec post_order_back;
    struct graph_node_vec depth_first_order;
    struct dom_tree dom_tree;
    struct dom_tree post_dom_tree;
    struct loop_tree loop_tree;
};

[[nodiscard]] struct cfg cfg_create(const struct scope*);
void cfg_destroy(struct cfg*);

struct dom_tree_node*  cfg_dom_of(const struct graph_node*);
struct dom_tree_node*  cfg_post_dom_of(const struct graph_node*);
struct loop_tree_node* cfg_loop_of(const struct graph_node*);
