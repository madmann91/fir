#include "scope.h"

#include "fir/node.h"
#include "fir/module.h"

#include "../support/vec.h"

#include <assert.h>

static inline uint32_t hash_node(const struct fir_node* const* node_ptr) {
    return (*node_ptr)->id;
}

static inline bool cmp_node(
    const struct fir_node* const* node_ptr,
    const struct fir_node* const* other_ptr)
{
    return (*node_ptr) == (*other_ptr);
}

IMPL_SET(scope, const struct fir_node*, hash_node, cmp_node, PUBLIC)
DEF_VEC(node_stack, const struct fir_node*, PRIVATE)

struct scope scope_compute(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    struct scope scope = scope_create();
    const struct fir_node* param = fir_param(func);

    struct node_stack node_stack = node_stack_create();
    node_stack_push(&node_stack, &param);
    while (node_stack.elem_count > 0) {
        const struct fir_node* node = node_stack.elems[node_stack.elem_count - 1];
        node_stack_pop(&node_stack);

        if (node == func || !scope_insert(&scope, &node))
            continue;

        for (const struct fir_use* use = node->uses; use; use = use->next)
            node_stack_push(&node_stack, &use->user);
    }

    return scope;
}
