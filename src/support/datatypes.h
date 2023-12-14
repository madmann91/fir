#pragma once

#include "set.h"
#include "map.h"
#include "vec.h"
#include "span.h"
#include "str.h"

struct fir_node;
struct fir_use;

SET_DECL(str_view_set, struct str_view, PUBLIC)
SMALL_VEC_DECL(small_string_vec, char*, PUBLIC)
SET_DECL(node_set, const struct fir_node*, PUBLIC)
MAP_DECL(node_map, const struct fir_node*, void*, PUBLIC)
VEC_DECL(node_vec, const struct fir_node*, PUBLIC)
VEC_DECL(use_vec, const struct fir_use*, PUBLIC)
SMALL_VEC_DECL(small_node_vec, const struct fir_node*, PUBLIC)
SPAN_DECL(node_span, const struct fir_node*)
CSPAN_DECL(node_cspan, const struct fir_node*)
MAP_DECL(use_map, const struct fir_use*, void*, PUBLIC)
