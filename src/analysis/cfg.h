#pragma once

#include "support/graph.h"

#include "dom_tree.h"
#include "loop_tree.h"

struct scope;

enum cfg_user_data_index {
    CFG_DOM_TREE_INDEX,
    CFG_POST_DOM_TREE_INDEX,
    CFG_LOOP_TREE_INDEX,
    CFG_USER_DATA_COUNT
};

struct cfg {
    struct graph graph;
    struct dom_tree dom_tree;
    struct dom_tree post_dom_tree;
    struct loop_tree loop_tree;
};

[[nodiscard]] struct cfg cfg_create(const struct scope*);
void cfg_destroy(struct cfg*);

void cfg_compute_loop_tree(struct cfg*);
void cfg_compute_dom_tree(struct cfg*);
void cfg_compute_post_dom_tree(struct cfg*);
