#pragma once

#include "set.h"
#include "map.h"
#include "vec.h"
#include "span.h"
#include "graph.h"
#include "str.h"

struct fir_node;

SET_DECL(str_view_set, struct str_view, PUBLIC)
SET_DECL(node_set, const struct fir_node*, PUBLIC)
MAP_DECL(node_map, const struct fir_node*, const struct fir_node*, PUBLIC)
VEC_DECL(node_vec, const struct fir_node*, PUBLIC)
SMALL_VEC_DECL(small_node_vec, const struct fir_node*, PUBLIC)
SPAN_DECL(node_span, const struct fir_node*)
CSPAN_DECL(node_cspan, const struct fir_node*)
GRAPH_DECL(node_graph, const struct fir_node*, PUBLIC)
