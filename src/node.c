#include "fir/node.h"
#include "fir/module.h"

#include "datatypes.h"

#include <overture/bits.h>
#include <overture/mem_stream.h>

#include <assert.h>
#include <inttypes.h>

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

bool fir_node_tag_is_arith_op(enum fir_node_tag tag) {
    return fir_node_tag_is_iarith_op(tag) || fir_node_tag_is_farith_op(tag);
}

bool fir_node_tag_is_cmp_op(enum fir_node_tag tag) {
    return fir_node_tag_is_icmp_op(tag) || fir_node_tag_is_fcmp_op(tag);
}

bool fir_node_tag_has_fp_flags(enum fir_node_tag tag) {
    return fir_node_tag_is_farith_op(tag);
}

bool fir_node_tag_has_mem_flags(enum fir_node_tag tag) {
    return tag == FIR_STORE || tag == FIR_LOAD;
}

bool fir_node_tag_has_bitwidth(enum fir_node_tag tag) {
    return tag == FIR_INT_TY || tag == FIR_FLOAT_TY;
}

bool fir_node_tag_can_be_external(enum fir_node_tag tag) {
    return tag == FIR_GLOBAL || tag == FIR_FUNC;
}

bool fir_node_has_fp_flags(const struct fir_node* node) {
    return fir_node_tag_has_fp_flags(node->tag);
}

bool fir_node_has_mem_flags(const struct fir_node* node) {
    return fir_node_tag_has_mem_flags(node->tag);
}

bool fir_node_has_bitwidth(const struct fir_node* node) {
    return fir_node_tag_has_bitwidth(node->tag);
}

bool fir_node_is_arith_op(const struct fir_node* node) {
    return fir_node_tag_is_arith_op(node->tag);
}

bool fir_node_is_cmp_op(const struct fir_node* node) {
    return fir_node_tag_is_cmp_op(node->tag);
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

bool fir_node_is_cont_ty(const struct fir_node* node) {
    return node->tag == FIR_FUNC_TY && FIR_FUNC_TY_RET(node)->tag == FIR_NORET_TY;
}

bool fir_node_is_not(const struct fir_node* node) {
    assert(node->ty->tag == FIR_INT_TY);
    return
        node->tag == FIR_XOR &&
        FIR_BIT_OP_LEFT(node)->tag == FIR_CONST &&
        FIR_BIT_OP_LEFT(node)->data.int_val == make_bitmask(node->ty->data.bitwidth);
}

bool fir_node_is_ineg(const struct fir_node* node) {
    assert(node->ty->tag == FIR_INT_TY);
    return
        node->tag == FIR_ISUB &&
        FIR_ARITH_OP_LEFT(node)->tag == FIR_CONST &&
        FIR_ARITH_OP_LEFT(node)->data.int_val == 0;
}

bool fir_node_is_fneg(const struct fir_node* node) {
    assert(node->ty->tag == FIR_FLOAT_TY);
    return
        node->tag == FIR_FSUB &&
        FIR_ARITH_OP_LEFT(node)->tag == FIR_CONST &&
        FIR_ARITH_OP_LEFT(node)->data.float_val == 0;
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

bool fir_node_is_unit(const struct fir_node* node) {
    return node->tag == FIR_TUP && node->op_count == 0;
}

bool fir_node_is_unit_ty(const struct fir_node* node) {
    return node->tag == FIR_TUP_TY && node->op_count == 0;
}

bool fir_node_is_choice(const struct fir_node* node) {
    return
        node->tag == FIR_EXT &&
        FIR_EXT_AGGR(node)->tag == FIR_ARRAY &&
        fir_node_is_bool_ty(FIR_EXT_INDEX(node)->ty);
}

bool fir_node_is_select(const struct fir_node* node) {
    return fir_node_is_choice(node) && FIR_EXT_AGGR(node)->ty->data.array_dim == 2;
}

bool fir_node_is_jump(const struct fir_node* node) {
    return node->tag == FIR_CALL && node->ty->tag == FIR_NORET_TY;
}

bool fir_node_is_branch(const struct fir_node* node) {
    return fir_node_is_jump(node) && fir_node_is_select(FIR_CALL_CALLEE(node));
}

bool fir_node_is_switch(const struct fir_node* node) {
    return fir_node_is_jump(node) && fir_node_is_choice(FIR_CALL_CALLEE(node));
}

static inline bool has_non_null_ops(const struct fir_node* node, bool flip) {
    for (size_t i = 0; i < node->op_count; ++i) {
        if (flip ^ (node->ops[i] == NULL))
            return false;
    }
    return true;
}

bool fir_node_is_imported(const struct fir_node* node) {
    return fir_node_is_external(node) && has_non_null_ops(node, true);
}

bool fir_node_is_exported(const struct fir_node* node) {
    return fir_node_is_external(node) && has_non_null_ops(node, false);
}

bool fir_node_can_be_external(const struct fir_node* node) {
    return fir_node_tag_can_be_external(node->tag);
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

char* fir_node_unique_name(const struct fir_node* node) {
    struct mem_stream mem_stream;
    mem_stream_init(&mem_stream);
    fprintf(mem_stream.file, "%s_%"PRIu64, fir_node_name(node), node->id);
    mem_stream_destroy(&mem_stream);
    return mem_stream.buf;
}

void fir_node_set_dbg_info(const struct fir_node* node, const struct fir_dbg_info* dbg_info) {
    ((struct fir_node*)node)->dbg_info = dbg_info;
}

const struct fir_node* fir_node_rebuild(
    struct fir_mod* mod,
    enum fir_node_tag tag,
    const union fir_node_data* data,
    const struct fir_node* ctrl,
    const struct fir_node* ty,
    const struct fir_node* const* ops,
    size_t op_count)
{
    assert(!fir_node_tag_is_nominal(tag));
    switch (tag) {
        case FIR_NORET_TY:    return fir_noret_ty(mod);
        case FIR_MEM_TY:      return fir_mem_ty(mod);
        case FIR_FRAME_TY:    return fir_frame_ty(mod);
        case FIR_PTR_TY:      return fir_ptr_ty(mod);
        case FIR_INT_TY:      return fir_int_ty(mod, data->bitwidth);
        case FIR_FLOAT_TY:    return fir_float_ty(mod, data->bitwidth);
        case FIR_TUP_TY:      return fir_tup_ty(mod, ops, op_count);
        case FIR_ARRAY_TY:    return fir_array_ty(ops[0], data->array_dim);
        case FIR_DYNARRAY_TY: return fir_dynarray_ty(ops[0]);
        case FIR_FUNC_TY:     return fir_func_ty(ops[0], ops[1]);
        case FIR_TOP:         return fir_top(ty);
        case FIR_BOT:         return fir_bot(ty);
        case FIR_CONST:
            return ty->tag == FIR_INT_TY
                ? fir_int_const(ty, data->int_val)
                : fir_float_const(ty, data->float_val);
#define x(tag, ...) case FIR_##tag:
        FIR_IARITH_OP_LIST(x)
            return fir_iarith_op(tag, ctrl, ops[0], ops[1]);
        FIR_FARITH_OP_LIST(x)
            return fir_farith_op(tag, data->fp_flags, ctrl, ops[0], ops[1]);
        FIR_ICMP_OP_LIST(x)
            return fir_icmp_op(tag, ctrl, ops[0], ops[1]);
        FIR_FCMP_OP_LIST(x)
            return fir_fcmp_op(tag, ctrl, ops[0], ops[1]);
        FIR_BIT_OP_LIST(x)
            return fir_bit_op(tag, ctrl, ops[0], ops[1]);
        FIR_CAST_OP_LIST(x)
            return fir_cast_op(tag, ctrl, ty, ops[0]);
#undef x
        case FIR_TUP:    return fir_tup(mod, ctrl, ops, op_count);
        case FIR_ARRAY:  return fir_array(ctrl, ty, ops);
        case FIR_EXT:    return fir_ext(ctrl, ops[0], ops[1]);
        case FIR_INS:    return fir_ins(ctrl, ops[0], ops[1], ops[2]);
        case FIR_ADDROF: return fir_addrof(ctrl, ops[0], ops[1], ops[2]);
        case FIR_STORE:  return fir_store(data->mem_flags, ctrl, ops[0], ops[1], ops[2]);
        case FIR_LOAD:   return fir_load(data->mem_flags, ctrl, ops[0], ops[1], ty);
        case FIR_CALL:   return fir_call(ctrl, ops[0], ops[1]);
        case FIR_PARAM:  return fir_param(ops[0]);
        case FIR_CTRL:   return fir_ctrl(ops[0]);
        case FIR_START:  return fir_start(ops[0]);
        default:
            assert(false && "invalid node tag");
            return NULL;
    }
}

const struct fir_node* fir_node_pin(const struct fir_node* node, const struct fir_node* ctrl) {
    assert(ctrl);
    assert(ctrl->ty->tag == FIR_CTRL_TY);
    struct fir_mod* mod = fir_node_mod(node);
    return fir_node_rebuild(mod, node->tag, &node->data, ctrl, node->ty, node->ops, node->op_count);
}

const struct fir_node* fir_node_unpin(const struct fir_node* node) {
    struct fir_mod* mod = fir_node_mod(node);
    return fir_node_rebuild(mod, node->tag, &node->data, NULL, node->ty, node->ops, node->op_count);
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
    else if (node->tag == FIR_LOCAL)
        cloned_node = fir_local(fir_bot(fir_frame_ty(mod)), fir_bot(fir_unit_ty(mod)));
    assert(cloned_node);
    cloned_node->data = node->data;
    return cloned_node;
}

const struct fir_node* fir_node_prepend(
    const struct fir_node* node,
    const struct fir_node* const* elems,
    size_t elem_count)
{
    struct fir_mod* mod = fir_node_mod(node);
    struct small_node_vec args;
    small_node_vec_init(&args);
    for (size_t i = 0; i < elem_count; ++i)
        small_node_vec_push(&args, &elems[i]);

    bool is_ty = fir_node_is_ty(node);
    const struct fir_node* ty = is_ty ? node : node->ty;
    if (ty->tag == FIR_TUP_TY) {
        for (size_t i = 0; i < ty->op_count; ++i) {
            const struct fir_node* arg = is_ty ? ty->ops[i] : fir_ext_at(node->ctrl, node, i);
            small_node_vec_push(&args, &arg);
        }
    } else {
        small_node_vec_push(&args, &node);
    }

    const struct fir_node* new_node = is_ty
        ? fir_tup_ty(mod, args.elems, args.elem_count)
        : fir_tup(mod, node->ctrl, args.elems, args.elem_count);
    small_node_vec_destroy(&args);
    return new_node;
}

const struct fir_node* fir_node_chop(const struct fir_node* node, size_t elem_count) {
    assert(node->tag == FIR_TUP_TY || node->ty->tag == FIR_TUP_TY);
    struct small_node_vec args;
    small_node_vec_init(&args);

    bool is_ty = fir_node_is_ty(node);
    const struct fir_node* ty = is_ty ? node : node->ty;
    for (size_t i = elem_count; i < ty->op_count; ++i) {
        const struct fir_node* arg = is_ty ? ty->ops[i] : fir_ext_at(node->ctrl, node, i);
        small_node_vec_push(&args, &arg);
    }

    struct fir_mod* mod = fir_node_mod(node);
    const struct fir_node* new_node = NULL;
    if (args.elem_count == 1) {
        new_node = args.elems[0];
    } else {
        new_node = is_ty
            ? fir_tup_ty(mod, args.elems, args.elem_count)
            : fir_tup(mod, node->ctrl, args.elems, args.elem_count);
    }
    small_node_vec_destroy(&args);
    return new_node;
}

const struct fir_node* fir_node_func_entry(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    if (!FIR_FUNC_BODY(func))
        return NULL;
    assert(FIR_FUNC_BODY(func)->tag == FIR_START);
    assert(FIR_START_BLOCK(FIR_FUNC_BODY(func))->tag == FIR_FUNC);
    return FIR_START_BLOCK(FIR_FUNC_BODY(func));
}

const struct fir_node* fir_node_func_return(const struct fir_node* func) {
    const struct fir_node* entry = fir_node_func_entry(func);
    if (!entry)
        return NULL;
    const struct fir_node* param = fir_param(entry);
    assert(param->ty->tag == FIR_TUP_TY);
    assert(param->ty->op_count == 2);
    const struct fir_node* ret = fir_ext_at(NULL, param, 1);
    assert(fir_node_is_cont_ty(ret->ty));
    assert(FIR_FUNC_TY_PARAM(ret->ty) == FIR_FUNC_TY_RET(func->ty));
    return ret;
}

const struct fir_node* fir_node_func_frame(const struct fir_node* func) {
    const struct fir_node* entry = fir_node_func_entry(func);
    if (!entry)
        return NULL;
    const struct fir_node* param = fir_param(entry);
    assert(param->ty->tag == FIR_TUP_TY);
    assert(param->ty->op_count == 2);
    const struct fir_node* frame = fir_ext_at(NULL, param, 0);
    assert(frame->ty->tag == FIR_FRAME_TY);
    return frame;
}

const struct fir_node* fir_node_mem_param(const struct fir_node* func) {
    return fir_ext_mem(NULL, fir_param(func));
}

const struct fir_node* const* fir_node_jump_targets(const struct fir_node* node) {
    assert(fir_node_is_jump(node));
    if (fir_node_is_choice(FIR_CALL_CALLEE(node)))
        return FIR_EXT_AGGR(FIR_CALL_CALLEE(node))->ops;
    return &FIR_CALL_CALLEE(node);
}

size_t fir_node_jump_target_count(const struct fir_node* node) {
    assert(fir_node_is_jump(node));
    if (fir_node_is_choice(FIR_CALL_CALLEE(node)))
        return FIR_EXT_AGGR(FIR_CALL_CALLEE(node))->op_count;
    return 1;
}

const struct fir_node* fir_node_switch_cond(const struct fir_node* node) {
    assert(fir_node_is_switch(node));
    return FIR_EXT_INDEX(FIR_CALL_CALLEE(node));
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

const struct fir_node* fir_assert_tag_debug(const struct fir_node* node, [[maybe_unused]] enum fir_node_tag tag) {
    assert(node->tag == tag);
    return node;
}

const struct fir_node* fir_assert_kind_debug(
    const struct fir_node* node,
    [[maybe_unused]] bool (*kind)(const struct fir_node*))
{
    assert(kind(node));
    return node;
}
