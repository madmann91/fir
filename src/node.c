#include "fir/node.h"

#include <assert.h>

#define x(tag, ...) case FIR_##tag:
#define pred_func(f, list) \
    bool f (enum fir_node_tag tag) { \
        switch (tag) { \
            list(x) \
                return true; \
            default: \
                return false; \
        } \
    }

pred_func(fir_node_tag_is_ty, FIR_TYPE_LIST)
pred_func(fir_node_tag_is_nominal, FIR_NOMINAL_NODE_LIST)
pred_func(fir_node_tag_is_iarith_op, FIR_IARITH_OP_LIST)
pred_func(fir_node_tag_is_farith_op, FIR_FARITH_OP_LIST)
pred_func(fir_node_tag_is_icmp_op, FIR_ICMP_OP_LIST)
pred_func(fir_node_tag_is_fcmp_op, FIR_FCMP_OP_LIST)
pred_func(fir_node_tag_is_idiv_op, FIR_IDIV_OP_LIST)
pred_func(fir_node_tag_is_fdiv_op, FIR_FDIV_OP_LIST)
pred_func(fir_node_tag_is_bit_op, FIR_BIT_OP_LIST)
pred_func(fir_node_tag_is_cast_op, FIR_CAST_OP_LIST)
pred_func(fir_node_tag_is_aggr_op, FIR_AGGR_OP_LIST)
pred_func(fir_node_tag_is_mem_op, FIR_MEM_OP_LIST)
pred_func(fir_node_tag_is_control_op, FIR_CONTROL_OP_LIST)

#undef pred_func
#undef x

const char* fir_node_tag_to_string(enum fir_node_tag tag) {
    switch (tag) {
#define x(tag, str) case FIR_##tag: return str;
        FIR_NODE_LIST(x)
#undef x
        default:
            assert(false && "invalid node tag");
            return "";
    }
}

struct fir_mod* fir_node_mod(const struct fir_node* node) {
    return fir_node_is_ty(node) ? node->mod : node->ty->mod;
}

const char* fir_node_name(const struct fir_node* node) {
    return node->dbg_info ? node->dbg_info->name : "";
}
