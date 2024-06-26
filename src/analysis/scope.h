#pragma once

#include "datatypes.h"

#include <stdbool.h>

struct scope {
    const struct fir_node* func;
    struct node_set nodes;
};

[[nodiscard]] struct scope scope_create(const struct fir_node* func);
bool scope_contains(const struct scope*, const struct fir_node*);
void scope_destroy(struct scope*);
