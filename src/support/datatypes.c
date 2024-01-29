#include "datatypes.h"

#include "fir/node.h"

#include <inttypes.h>

static inline uint32_t hash_node(uint32_t h, const struct fir_node* const* node_ptr) {
    return hash_uint64(h, (*node_ptr)->id);
}

static inline bool is_node_equal(
    const struct fir_node* const* node_ptr,
    const struct fir_node* const* other_ptr)
{
    return (*node_ptr) == (*other_ptr);
}

static inline uint32_t hash_use(uint32_t h, const struct fir_use* const* use_ptr) {
    return hash_uint64(hash_uint64(h, (*use_ptr)->index), (*use_ptr)->user->id);
}

static inline uint32_t is_use_equal(
    const struct fir_use* const* use_ptr,
    const struct fir_use* const* other_ptr)
{
    return
        (*use_ptr)->index == (*other_ptr)->index &&
        (*use_ptr)->user == (*other_ptr)->user;
}

SET_IMPL(str_view_set, struct str_view, str_view_hash, str_view_is_equal, PUBLIC)
SMALL_VEC_IMPL(small_string_vec, char*, PUBLIC)
MAP_IMPL(node_map, const struct fir_node*, void*, hash_node, is_node_equal, PUBLIC)
SET_IMPL(node_set, const struct fir_node*, hash_node, is_node_equal, PUBLIC)
VEC_IMPL(node_vec, const struct fir_node*, PUBLIC)
SMALL_VEC_IMPL(small_node_vec, const struct fir_node*, PUBLIC)
MAP_IMPL(use_map, const struct fir_use*, void*, hash_use, is_use_equal, PUBLIC)
UNIQUE_STACK_IMPL(unique_node_stack, const struct fir_node*, hash_node, is_node_equal, PUBLIC)
