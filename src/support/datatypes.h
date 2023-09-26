#pragma once

#include "set.h"
#include "map.h"
#include "vec.h"
#include "span.h"
#include "graph.h"

struct fir_node;

SET_DECL(node_set, const struct fir_node*, PUBLIC)
MAP_DECL(node_map, const struct fir_node*, const struct fir_node*, PUBLIC)
VEC_DECL(node_vec, const struct fir_node*, PUBLIC)
SMALL_VEC_DECL(small_node_vec, const struct fir_node*, PUBLIC)
SPAN_DECL(node_span, const struct fir_node*)
CONST_SPAN_DECL(const_node_span, const struct fir_node*)
GRAPH_DECL(node_graph, const struct fir_node*, PUBLIC)
