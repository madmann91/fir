#pragma once

#include <overture/set.h>
#include <overture/map.h>
#include <overture/vec.h>
#include <overture/span.h>
#include <overture/str.h>
#include <overture/unique_stack.h>

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
CONST_SPAN_DECL(const_node_span, const struct fir_node*)
MAP_DECL(use_map, const struct fir_use*, void*, PUBLIC)
UNIQUE_STACK_DECL(unique_node_stack, const struct fir_node*, PUBLIC)
