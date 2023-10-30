#include "cfg.h"
#include "scope.h"
#include "dom_tree.h"
#include "loop_tree.h"

#include "fir/node.h"

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
    return cfg;
}

void cfg_destroy(struct cfg* cfg) {
    graph_destroy(&cfg->graph);
    dom_tree_destroy(&cfg->dom_tree);
    dom_tree_destroy(&cfg->post_dom_tree);
    loop_tree_destroy(&cfg->loop_tree);
    memset(cfg, 0, sizeof(struct cfg));
}

void cfg_compute_loop_tree(struct cfg* cfg) {
    loop_tree_destroy(&cfg->loop_tree);
    cfg->loop_tree = loop_tree_create(&cfg->graph, CFG_LOOP_TREE_INDEX);
}

void cfg_compute_dom_tree(struct cfg* cfg) {
    dom_tree_destroy(&cfg->dom_tree);
    cfg->dom_tree = dom_tree_create(&cfg->graph, CFG_DOM_TREE_INDEX, GRAPH_DIR_FORWARD);
}

void cfg_compute_post_dom_tree(struct cfg* cfg) {
    dom_tree_destroy(&cfg->post_dom_tree);
    cfg->post_dom_tree = dom_tree_create(&cfg->graph, CFG_POST_DOM_TREE_INDEX, GRAPH_DIR_BACKWARD);
}
