#include "cfg.h"
#include "scope.h"
#include "dom_tree.h"
#include "loop_tree.h"

#include "fir/node.h"

struct cfg cfg_create(const struct scope* scope) {
    assert(fir_node_func_entry(scope->func));
    assert(fir_node_func_return(scope->func));

    struct cfg cfg = {
        .graph = graph_create(
            CFG_NODE_DATA_SIZE,
            CFG_EDGE_DATA_SIZE,
            (void*)fir_node_func_entry(scope->func),
            (void*)fir_node_func_return(scope->func))
    };

    SET_FOREACH(const struct fir_node*, node_ptr, scope->nodes) {
        const struct fir_node* func = *node_ptr;
        if (func->tag != FIR_FUNC || !FIR_FUNC_BODY(func))
            continue;

        struct graph_node* from = graph_insert(&cfg.graph, (void*)func);
        const struct fir_node* const* targets = fir_node_jump_targets(FIR_FUNC_BODY(func));
        for (size_t i = 0, n = fir_node_jump_target_count(FIR_FUNC_BODY(func)); i < n; ++i) {
            if (scope_contains(scope, targets[i])) {
                struct graph_node* to = graph_insert(&cfg.graph, (void*)targets[i]);
                graph_connect(&cfg.graph, from, to);
            }
        }
    }

    cfg.post_order        = graph_compute_post_order(&cfg.graph, GRAPH_DIR_FORWARD);
    cfg.post_order_back   = graph_compute_post_order(&cfg.graph, GRAPH_DIR_BACKWARD);
    cfg.depth_first_order = graph_compute_depth_first_order(&cfg.graph, GRAPH_DIR_FORWARD);

    for (size_t i = 0; i < cfg.post_order.elem_count; ++i)
        cfg.post_order.elems[i]->user_data[CFG_POST_ORDER_INDEX].index = i;
    for (size_t i = 0; i < cfg.post_order_back.elem_count; ++i)
        cfg.post_order_back.elems[i]->user_data[CFG_POST_ORDER_BACK_INDEX].index = i;
    for (size_t i = 0; i < cfg.depth_first_order.elem_count; ++i)
        cfg.depth_first_order.elems[i]->user_data[CFG_DEPTH_FIRST_ORDER_INDEX].index = i;

    cfg.dom_tree = dom_tree_create(&cfg.post_order,
        CFG_POST_ORDER_INDEX, CFG_DOM_TREE_INDEX, GRAPH_DIR_FORWARD);
    cfg.post_dom_tree = dom_tree_create(&cfg.post_order_back,
        CFG_POST_ORDER_BACK_INDEX, CFG_POST_DOM_TREE_INDEX, GRAPH_DIR_BACKWARD);
    cfg.loop_tree = loop_tree_create(&cfg.depth_first_order,
        CFG_DEPTH_FIRST_ORDER_INDEX, CFG_LOOP_TREE_INDEX, GRAPH_DIR_FORWARD);
    return cfg;
}

void cfg_destroy(struct cfg* cfg) {
    graph_destroy(&cfg->graph);
    graph_node_vec_destroy(&cfg->post_order);
    graph_node_vec_destroy(&cfg->post_order_back);
    graph_node_vec_destroy(&cfg->depth_first_order);
    dom_tree_destroy(&cfg->dom_tree);
    dom_tree_destroy(&cfg->post_dom_tree);
    loop_tree_destroy(&cfg->loop_tree);
    memset(cfg, 0, sizeof(struct cfg));
}

struct fir_node* cfg_block_func(const struct graph_node* node) {
    return (struct fir_node*)node->key;
}

struct graph_node* cfg_find(struct cfg* cfg, const struct fir_node* node) {
    assert(node->tag == FIR_FUNC);
    assert(fir_node_is_cont_ty(node->ty));
    struct graph_node* graph_node = graph_find(&cfg->graph, (void*)node);
    assert(graph_node);
    return graph_node;
}

struct dom_tree_node* cfg_dom_tree_node(const struct graph_node* node) {
    return (struct dom_tree_node*)node->user_data[CFG_DOM_TREE_INDEX].ptr;
}

struct dom_tree_node* cfg_post_dom_tree_node(const struct graph_node* node) {
    return (struct dom_tree_node*)node->user_data[CFG_POST_DOM_TREE_INDEX].ptr;
}

struct loop_tree_node* cfg_loop_tree_node(const struct graph_node* node) {
    return (struct loop_tree_node*)node->user_data[CFG_LOOP_TREE_INDEX].ptr;
}

bool cfg_is_dominated_by(const struct graph_node* block, const struct graph_node* other_block) {
    return dom_tree_node_is_dominated_by(cfg_dom_tree_node(block), cfg_dom_tree_node(other_block), CFG_DOM_TREE_INDEX);
}
