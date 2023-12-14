#include "scope.h"

#include "fir/node.h"
#include "fir/module.h"

#include <assert.h>

struct scope scope_create(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    struct node_set nodes = node_set_create();
    const struct fir_node* param = fir_param(func);

    struct node_vec node_stack = node_vec_create();
    node_vec_push(&node_stack, &param);
    while (node_stack.elem_count > 0) {
        const struct fir_node* node = *node_vec_pop(&node_stack);

        if (node == func || !node_set_insert(&nodes, &node))
            continue;

        if (node->tag == FIR_PARAM)
            node_vec_push(&node_stack, &node->ops[0]);

        for (const struct fir_use* use = node->uses; use; use = use->next)
            node_vec_push(&node_stack, &use->user);
    }
    node_vec_destroy(&node_stack);

    return (struct scope) { func, nodes };
}

bool scope_contains(const struct scope* scope, const struct fir_node* node) {
    return node_set_find(&scope->nodes, &node) != NULL;
}

void scope_destroy(struct scope* scope) {
    node_set_destroy(&scope->nodes);
    memset(scope, 0, sizeof(struct scope));
}
