#include "cfg.h"
#include "scope.h"
#include "dom_tree.h"
#include "loop_tree.h"

#include "fir/node.h"

static inline struct const_node_span jump_targets(const struct fir_node* node) {
    assert(fir_node_is_jump(node));
    if (fir_node_is_choice(node->ops[0])) {
        const struct fir_node* array = node->ops[0]->ops[0];
        return (struct const_node_span) { .elems = array->ops, .elem_count = array->op_count };
    }
    return (struct const_node_span) { .elems = &node->ops[0], .elem_count = 1 };
}

struct cfg cfg_create(const struct scope* scope) {
    assert(fir_node_func_entry(scope->func));
    assert(fir_node_func_return(scope->func));

    struct cfg cfg = {
        .graph = graph_create(
            CFG_USER_DATA_COUNT,
            (void*)fir_node_func_entry(scope->func),
            (void*)fir_node_func_return(scope->func))
    };

    SET_FOREACH(const struct fir_node*, node_ptr, scope->nodes) {
        const struct fir_node* func = *node_ptr;
        if (func->tag != FIR_FUNC || !func->ops[0])
            continue;

        struct graph_node* from = graph_insert(&cfg.graph, (void*)func);
        struct const_node_span targets = jump_targets(func->ops[0]);
        CONST_SPAN_FOREACH(const struct fir_node*, target_ptr, targets) {
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
        cfg.post_order.elems[i]->data[CFG_POST_ORDER_INDEX].index = i;
    for (size_t i = 0; i < cfg.post_order_back.elem_count; ++i)
        cfg.post_order_back.elems[i]->data[CFG_POST_ORDER_BACK_INDEX].index = i;
    for (size_t i = 0; i < cfg.depth_first_order.elem_count; ++i)
        cfg.depth_first_order.elems[i]->data[CFG_DEPTH_FIRST_ORDER_INDEX].index = i;

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
    return (struct dom_tree_node*)node->data[CFG_DOM_TREE_INDEX].ptr;
}

struct dom_tree_node* cfg_post_dom_tree_node(const struct graph_node* node) {
    return (struct dom_tree_node*)node->data[CFG_POST_DOM_TREE_INDEX].ptr;
}

struct loop_tree_node* cfg_loop_tree_node(const struct graph_node* node) {
    return (struct loop_tree_node*)node->data[CFG_LOOP_TREE_INDEX].ptr;
}

bool cfg_is_dominated_by(const struct graph_node* block, const struct graph_node* other_block) {
    return dom_tree_node_is_dominated_by(cfg_dom_tree_node(block), cfg_dom_tree_node(other_block), CFG_DOM_TREE_INDEX);
}
