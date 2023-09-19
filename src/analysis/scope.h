#pragma once

#include "../support/set.h"

struct fir_node;
DECL_SET(scope, const struct fir_node*, PUBLIC)

struct scope scope_compute(const struct fir_node* func);
