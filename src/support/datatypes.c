#include "datatypes.h"

#include "fir/node.h"

#include <inttypes.h>

static inline uint32_t hash_node(const struct fir_node* const* node_ptr) {
    return (*node_ptr)->id;
}

static inline bool cmp_node(
    const struct fir_node* const* node_ptr,
    const struct fir_node* const* other_ptr)
{
    return (*node_ptr) == (*other_ptr);
}

static inline void print_node(FILE* file, const struct fir_node* const* node_ptr) {
    fprintf(file, "%s_%"PRIu64, fir_node_name(*node_ptr), (*node_ptr)->id);
}

MAP_IMPL(node_map, const struct fir_node*, const struct fir_node*, hash_node, cmp_node, PUBLIC)
SET_IMPL(node_set, const struct fir_node*, hash_node, cmp_node, PUBLIC)
VEC_IMPL(node_vec, const struct fir_node*, PUBLIC)
SMALL_VEC_IMPL(small_node_vec, const struct fir_node*, PUBLIC)
GRAPH_IMPL(node_graph, const struct fir_node*, hash_node, cmp_node, print_node, PUBLIC)
