#include "fir/node.h"
#include "fir/module.h"

#include "support/bits.h"

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
pred_func(is_bit_op, FIR_BIT_OP_LIST)
pred_func(is_shift_op, FIR_SHIFT_OP_LIST)
pred_func(is_cast_op, FIR_CAST_OP_LIST)
pred_func(is_aggr_op, FIR_AGGR_OP_LIST)
pred_func(is_mem_op, FIR_MEM_OP_LIST)
pred_func(is_control_op, FIR_CONTROL_OP_LIST)

#undef pred_func
#undef x

bool fir_node_tag_has_fp_flags(enum fir_node_tag tag) {
    return fir_node_tag_is_farith_op(tag);
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

bool fir_node_is_int_const(const struct fir_node* node) {
    return node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY;
}

bool fir_node_is_float_const(const struct fir_node* node) {
    return node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY;
}

bool fir_node_is_bool_ty(const struct fir_node* node) {
    return node->tag == FIR_INT_TY && node->data.bitwidth == 1;
}

bool fir_node_is_not(const struct fir_node* node) {
    assert(node->ty->tag == FIR_INT_TY);
    return
        node->tag == FIR_XOR &&
        node->ops[0]->tag == FIR_CONST &&
        node->ops[0]->data.int_val == make_bitmask(node->ty->data.bitwidth);
}

bool fir_node_is_ineg(const struct fir_node* node) {
    assert(node->ty->tag == FIR_INT_TY);
    return
        node->tag == FIR_ISUB &&
        node->ops[0]->tag == FIR_CONST &&
        node->ops[0]->data.int_val == 0;
}

bool fir_node_is_fneg(const struct fir_node* node) {
    assert(node->ty->tag == FIR_FLOAT_TY);
    return
        node->tag == FIR_FSUB &&
        node->ops[0]->tag == FIR_CONST &&
        node->ops[0]->data.float_val == 0;
}

bool fir_node_is_zero(const struct fir_node* node) {
    return
        node->tag == FIR_CONST &&
        ((node->ty->tag == FIR_FLOAT_TY && node->data.float_val == 0) ||
         (node->ty->tag == FIR_INT_TY   && node->data.int_val == 0));
}

bool fir_node_is_one(const struct fir_node* node) {
    return
        node->tag == FIR_CONST &&
        ((node->ty->tag == FIR_FLOAT_TY && node->data.float_val == 1) ||
         (node->ty->tag == FIR_INT_TY   && node->data.int_val == 1));
}

bool fir_node_is_all_ones(const struct fir_node* node) {
    return
        node->tag == FIR_CONST &&
        node->ty->tag == FIR_INT_TY &&
        node->data.int_val == make_bitmask(node->ty->data.bitwidth);
}

bool fir_node_is_choice(const struct fir_node* node) {
    return
        node->tag == FIR_EXT &&
        node->ops[0]->tag == FIR_ARRAY &&
        node->ops[0]->ty->tag == FIR_ARRAY_TY &&
        fir_node_is_bool_ty(node->ops[1]->ty);
}

bool fir_node_is_select(const struct fir_node* node) {
    return fir_node_is_choice(node) && node->ops[0]->ty->data.array_dim == 2;
}

bool fir_node_is_jump(const struct fir_node* node) {
    return node->tag == FIR_CALL && node->ty->tag == FIR_NORET_TY;
}

bool fir_node_is_branch(const struct fir_node* node) {
    return fir_node_is_jump(node) && fir_node_is_select(node->ops[0]);
}

bool fir_node_is_switch(const struct fir_node* node) {
    return fir_node_is_jump(node) && fir_node_is_choice(node->ops[0]);
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

const struct fir_node* fir_node_rebuild(
    struct fir_mod* mod,
    const struct fir_node* node,
    const struct fir_node* ty,
    const struct fir_node* const* ops)
{
    assert(!fir_node_is_nominal(node));
    switch (node->tag) {
        case FIR_NORET_TY:    return fir_noret_ty(mod);
        case FIR_MEM_TY:      return fir_mem_ty(mod);
        case FIR_PTR_TY:      return fir_ptr_ty(mod);
        case FIR_INT_TY:      return fir_int_ty(mod, node->data.bitwidth);
        case FIR_FLOAT_TY:    return fir_float_ty(mod, node->data.bitwidth);
        case FIR_TUP_TY:      return fir_tup_ty(mod, ops, node->op_count);
        case FIR_ARRAY_TY:    return fir_array_ty(ops[0], node->data.array_dim);
        case FIR_DYNARRAY_TY: return fir_dynarray_ty(ops[0]);
        case FIR_FUNC_TY:     return fir_func_ty(ops[0], ops[1]);
        case FIR_TOP:         return fir_top(ty);
        case FIR_BOT:         return fir_bot(ty);
        case FIR_CONST:
            return ty->tag == FIR_INT_TY
                ? fir_int_const(ty, node->data.int_val)
                : fir_float_const(ty, node->data.float_val);
#define x(tag, ...) case FIR_##tag:
        FIR_IARITH_OP_LIST(x)
            return fir_iarith_op(node->tag, ops[0], ops[1]);
        FIR_FARITH_OP_LIST(x)
            return fir_farith_op(node->tag, node->data.fp_flags, ops[0], ops[1]);
        FIR_ICMP_OP_LIST(x)
            return fir_icmp_op(node->tag, ops[0], ops[1]);
        FIR_FCMP_OP_LIST(x)
            return fir_fcmp_op(node->tag, ops[0], ops[1]);
        FIR_BIT_OP_LIST(x)
            return fir_bit_op(node->tag, ops[0], ops[1]);
        FIR_CAST_OP_LIST(x)
            return fir_cast_op(node->tag, ty, ops[0]);
#undef x
        case FIR_TUP:    return fir_tup(mod, ops, node->op_count);
        case FIR_ARRAY:  return fir_array(ty, ops);
        case FIR_EXT:    return fir_ext(ops[0], ops[1]);
        case FIR_INS:    return fir_ins(ops[0], ops[1], ops[2]);
        case FIR_ADDROF: return fir_addrof(ops[0], ops[1], ops[2]);
        case FIR_CALL:   return fir_call(ops[0], ops[1]);
        case FIR_PARAM:  return fir_param(ops[0]);
        case FIR_START:  return fir_start(ops[0]);
        default:
            assert(false && "invalid node tag");
            return NULL;
    }
}

struct fir_node* fir_node_clone(
    struct fir_mod* mod,
    const struct fir_node* node,
    const struct fir_node* ty)
{
    assert(fir_node_is_nominal(node));
    struct fir_node* cloned_node = NULL;
    if (node->tag == FIR_FUNC)
        cloned_node = fir_func(ty);
    else if (node->tag == FIR_GLOBAL)
        cloned_node = fir_global(mod);
    assert(cloned_node);
    cloned_node->data = node->data;
    return cloned_node;
}

size_t fir_use_count(const struct fir_use* use) {
    size_t use_count = 0;
    for (; use; use = use->next, use_count++);
    return use_count;
}

size_t fir_use_count_up_to(const struct fir_use* use, size_t max_count) {
    size_t use_count = 0;
    for (; use && use_count < max_count; use = use->next, use_count++);
    return use_count;
}

const struct fir_use* fir_use_find(
    const struct fir_use* use,
    const struct fir_node* user,
    size_t index)
{
    while (use) {
        if (use->user == user && use->index == index)
            return use;
        use = use->next;
    }
    return NULL;
}
