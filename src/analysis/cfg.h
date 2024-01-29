#pragma once

#include "support/graph.h"

#include "dom_tree.h"
#include "loop_tree.h"

struct scope;
struct fir_node;

enum {
    CFG_POST_ORDER_INDEX,
    CFG_POST_ORDER_BACK_INDEX,
    CFG_DEPTH_FIRST_ORDER_INDEX,
    CFG_DOM_TREE_INDEX,
    CFG_POST_DOM_TREE_INDEX,
    CFG_LOOP_TREE_INDEX,
    CFG_USER_DATA_COUNT
};

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

[[nodiscard]] struct fir_node*       cfg_block_func(const struct graph_node*);
[[nodiscard]] struct graph_node*     cfg_find(struct cfg*, const struct fir_node*);
[[nodiscard]] struct dom_tree_node*  cfg_dom_tree_node(const struct graph_node*);
[[nodiscard]] struct dom_tree_node*  cfg_post_dom_tree_node(const struct graph_node*);
[[nodiscard]] struct loop_tree_node* cfg_loop_tree_node(const struct graph_node*);

[[nodiscard]] bool cfg_is_dominated_by(const struct graph_node*, const struct graph_node*);

void cfg_print(FILE*, const struct cfg*);
void cfg_dump(const struct cfg*);
