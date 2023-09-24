#include "scope.h"

#include "fir/node.h"
#include "fir/module.h"

#include <assert.h>

struct node_set scope_compute(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    struct node_set scope = node_set_create();
    const struct fir_node* param = fir_param(func);

    struct node_vec node_stack = node_vec_create();
    node_vec_push(&node_stack, &param);
    while (node_stack.elem_count > 0) {
        const struct fir_node* node = node_stack.elems[node_stack.elem_count - 1];
        node_vec_pop(&node_stack);

        if (node == func || !node_set_insert(&scope, &node))
            continue;

        for (const struct fir_use* use = node->uses; use; use = use->next)
            node_vec_push(&node_stack, &use->user);
    }
    node_vec_destroy(&node_stack);

    return scope;
}
