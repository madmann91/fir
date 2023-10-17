#include "cfg.h"

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
    struct node_graph graph = node_graph_create();
    struct node_graph_node* entry = node_graph_insert(&graph,
        (const struct fir_node*[]) { fir_func_entry(scope->func) });

    SET_FOREACH(const struct fir_node*, node_ptr, scope->nodes) {
        const struct fir_node* func = *node_ptr;
        if (func->tag != FIR_FUNC || !func->ops[0])
            continue;

        struct node_graph_node* from = node_graph_insert(&graph, &func);
        struct node_cspan targets = jump_targets(func->ops[0]);
        CSPAN_FOREACH(const struct fir_node*, target_ptr, targets) {
            if (scope_contains(scope, *target_ptr)) {
                struct node_graph_node* to = node_graph_insert(&graph, target_ptr);
                node_graph_connect(&graph, from, to);
            }
        }
    }

    return (struct cfg) { entry, graph };
}

void cfg_destroy(struct cfg* cfg) {
    node_graph_destroy(&cfg->graph);
    memset(cfg, 0, sizeof(struct cfg));
}
