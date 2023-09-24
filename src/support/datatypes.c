#include "datatypes.h"

#include "fir/node.h"

static inline uint32_t hash_node(const struct fir_node* const* node_ptr) {
    return (*node_ptr)->id;
}

static inline bool cmp_node(
    const struct fir_node* const* node_ptr,
    const struct fir_node* const* other_ptr)
{
    return (*node_ptr) == (*other_ptr);
}

IMPL_MAP(node_map, const struct fir_node*, const struct fir_node*, hash_node, cmp_node, PUBLIC)
IMPL_SET(node_set, const struct fir_node*, hash_node, cmp_node, PUBLIC)
IMPL_VEC(node_vec, const struct fir_node*, PUBLIC)
IMPL_SMALL_VEC(small_node_vec, const struct fir_node*, PUBLIC)
