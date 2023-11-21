#include "cfg.h"
#include "scope.h"
#include "dom_tree.h"
#include "loop_tree.h"

#include "fir/node.h"

enum cfg_user_data_index {
    CFG_POST_ORDER_INDEX,
    CFG_POST_ORDER_BACK_INDEX,
    CFG_DEPTH_FIRST_ORDER_INDEX,
    CFG_DOM_TREE_INDEX,
    CFG_POST_DOM_TREE_INDEX,
    CFG_LOOP_TREE_INDEX,
    CFG_USER_DATA_COUNT
};

static inline struct node_cspan jump_targets(const struct fir_node* node) {
    assert(fir_node_is_jump(node));
    if (fir_node_is_choice(node->ops[0])) {
        const struct fir_node* array = node->ops[0]->ops[0];
        return (struct node_cspan) { .elems = array->ops, .elem_count = array->op_count };
    }
    return (struct node_cspan) { .elems = &node->ops[0], .elem_count = 1 };
}

struct cfg cfg_create(const struct scope* scope) {
    struct cfg cfg = { .graph = graph_create(CFG_USER_DATA_COUNT) };

    assert(fir_func_entry(scope->func));
    assert(fir_func_return(scope->func));

    graph_connect(&cfg.graph,
        graph_source(&cfg.graph, GRAPH_DIR_FORWARD),
        graph_insert(&cfg.graph, (void*)fir_func_entry(scope->func)));

    graph_connect(&cfg.graph,
        graph_insert(&cfg.graph, (void*)fir_func_return(scope->func)),
        graph_sink(&cfg.graph, GRAPH_DIR_FORWARD));

    SET_FOREACH(const struct fir_node*, node_ptr, scope->nodes) {
        const struct fir_node* func = *node_ptr;
        if (func->tag != FIR_FUNC || !func->ops[0])
            continue;

        struct graph_node* from = graph_insert(&cfg.graph, (void*)func);
        struct node_cspan targets = jump_targets(func->ops[0]);
        CSPAN_FOREACH(const struct fir_node*, target_ptr, targets) {
            if (scope_contains(scope, *target_ptr)) {
                struct graph_node* to = graph_insert(&cfg.graph, (void*)*target_ptr);
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
    cfg.loop_tree = loop_tree_create(&cfg.graph, CFG_LOOP_TREE_INDEX);
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

struct dom_tree_node* cfg_dom_of(const struct graph_node* node) {
    return (struct dom_tree_node*)node->user_data[CFG_DOM_TREE_INDEX].ptr;
}

struct dom_tree_node* cfg_post_dom_of(const struct graph_node* node) {
    return (struct dom_tree_node*)node->user_data[CFG_POST_DOM_TREE_INDEX].ptr;
}

struct loop_tree_node* cfg_loop_of(const struct graph_node* node) {
    return (struct loop_tree_node*)node->user_data[CFG_LOOP_TREE_INDEX].ptr;
}
