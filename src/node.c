#include "fir/node.h"

#include <assert.h>

#define x(tag, ...) case FIR_##tag:
#define pred_func(pred, list) \
    bool fir_node_tag_##pred(enum fir_node_tag tag) { \
        switch (tag) { \
            list(x) \
                return true; \
            default: \
                return false; \
        } \
    } \
    bool fir_node_##pred(const struct fir_node* node) { \
        return fir_node_tag_##pred(node->tag); \
    }
    

pred_func(is_ty, FIR_TYPE_LIST)
pred_func(is_nominal, FIR_NOMINAL_NODE_LIST)
pred_func(is_iarith_op, FIR_IARITH_OP_LIST)
pred_func(is_farith_op, FIR_FARITH_OP_LIST)
pred_func(is_icmp_op, FIR_ICMP_OP_LIST)
pred_func(is_fcmp_op, FIR_FCMP_OP_LIST)
pred_func(is_idiv_op, FIR_IDIV_OP_LIST)
pred_func(is_fdiv_op, FIR_FDIV_OP_LIST)
pred_func(is_bit_op, FIR_BIT_OP_LIST)
pred_func(is_cast_op, FIR_CAST_OP_LIST)
pred_func(is_aggr_op, FIR_AGGR_OP_LIST)
pred_func(is_mem_op, FIR_MEM_OP_LIST)
pred_func(is_control_op, FIR_CONTROL_OP_LIST)

#undef pred_func
#undef x

bool fir_node_tag_has_fp_flags(enum fir_node_tag tag) {
    return fir_node_tag_is_fdiv_op(tag) || fir_node_tag_is_farith_op(tag);
}

bool fir_node_tag_has_bitwidth(enum fir_node_tag tag) {
    return tag == FIR_INT_TY || tag == FIR_FLOAT_TY;
}

bool fir_node_has_fp_flags(const struct fir_node* node) {
    return fir_node_tag_has_fp_flags(node->tag);
}

bool fir_node_has_bitwidth(const struct fir_node* node) {
    return fir_node_tag_has_bitwidth(node->tag);
}

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

void fir_node_set_dbg_info(const struct fir_node* node, const struct fir_dbg_info* dbg_info) {
    ((struct fir_node*)node)->dbg_info = dbg_info;
}
