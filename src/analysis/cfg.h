#pragma once

#include "scope.h"

#include "../support/datatypes.h"

struct cfg {
    struct node_graph_node* entry;
    struct node_graph graph;
};

struct cfg cfg_create(const struct scope* scope);
void cfg_destroy(struct cfg*);
