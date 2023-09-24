#pragma once

#include "set.h"
#include "map.h"
#include "vec.h"
#include "graph.h"

struct fir_node;

DECL_SET(node_set, const struct fir_node*, PUBLIC)
DECL_MAP(node_map, const struct fir_node*, const struct fir_node*, PUBLIC)
DECL_VEC(node_vec, const struct fir_node*, PUBLIC)
DECL_SMALL_VEC(small_node_vec, const struct fir_node*, PUBLIC)
DECL_GRAPH(node_graph, const struct fir_node*, PUBLIC)
