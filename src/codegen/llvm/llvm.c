#include "fir/codegen.h"
#include "fir/module.h"
#include "fir/node.h"

#include "support/alloc.h"
#include "support/mem_pool.h"
#include "support/datatypes.h"
#include "codegen/codegen.h"
#include "analysis/scope.h"
#include "analysis/cfg.h"
#include "analysis/schedule.h"

#include <stdlib.h>
#include <assert.h>

#include <llvm-c/Core.h>

struct scheduled_node {
    const struct fir_node* node;
    struct graph_node* block;
};

static uint32_t hash_scheduled_node(uint32_t h, const struct scheduled_node* scheduled_node) {
    return hash_uint64(hash_uint64(h, scheduled_node->block->index), scheduled_node->node->id);
}

static bool are_scheduled_nodes_equal(
    const struct scheduled_node* scheduled_node,
    const struct scheduled_node* other_scheduled_node)
{
    return
        scheduled_node->node  == other_scheduled_node->node &&
        scheduled_node->block == other_scheduled_node->block;
}

MAP_DEFINE(scheduled_node_map, struct scheduled_node, void*, hash_scheduled_node, are_scheduled_nodes_equal, PRIVATE)

struct llvm_codegen {
    struct fir_codegen base;

    LLVMContextRef llvm_context;
    LLVMModuleRef  llvm_module;
    LLVMBuilderRef llvm_builder;
    LLVMValueRef   llvm_func;

    struct node_map llvm_types;
    struct node_map llvm_constants;
    struct node_map llvm_params;

    struct graph_node_map llvm_blocks;
    struct scheduled_node_map llvm_values;

    struct scope scope;
    struct cfg cfg;
    struct schedule schedule;
    struct unique_node_stack node_stack;
};

SMALL_VEC_DEFINE(small_llvm_type_vec,  LLVMTypeRef, PRIVATE)
SMALL_VEC_DEFINE(small_llvm_value_vec, LLVMValueRef, PRIVATE)

static LLVMTypeRef convert_ty(struct llvm_codegen*, const struct fir_node*);
static LLVMValueRef gen_constant(struct llvm_codegen*, const struct fir_node*);

static inline bool can_be_llvm_type(const struct fir_node* ty) {
    return ty->tag != FIR_MEM_TY && ty->tag != FIR_FRAME_TY;
}

static inline bool can_be_llvm_constant(const struct fir_node* node) {
    return (node->props & FIR_PROP_INVARIANT) != 0 &&
        (node->tag == FIR_CONST || node->tag == FIR_TUP);
}

static inline bool can_be_llvm_param(const struct fir_node* node) {
    return
        (node->tag == FIR_PARAM && node->ty->tag != FIR_TUP_TY) ||
        (node->tag == FIR_EXT && FIR_EXT_AGGR(node)->tag == FIR_PARAM && FIR_EXT_AGGR(node)->ty->tag == FIR_TUP_TY);
}

static void convert_tup_ty_args(
    struct llvm_codegen* codegen,
    const struct fir_node* ty,
    struct small_llvm_type_vec* types)
{
    if (ty->tag == FIR_TUP_TY) {
        for (size_t i = 0; i < ty->op_count; ++i) {
            if (!can_be_llvm_type(ty->ops[i]))
                continue;
            LLVMTypeRef type = convert_ty(codegen, ty->ops[i]);
            small_llvm_type_vec_push(types, &type);
        }
    } else if (can_be_llvm_type(ty)) {
        LLVMTypeRef type = convert_ty(codegen, ty);
        small_llvm_type_vec_push(types, &type);
    }
}

static LLVMTypeRef convert_func_ty(struct llvm_codegen* codegen, const struct fir_node* ty) {
    assert(ty->tag == FIR_FUNC_TY);
    struct small_llvm_type_vec param_types;
    struct small_llvm_type_vec ret_types;
    small_llvm_type_vec_init(&param_types);
    small_llvm_type_vec_init(&ret_types);
    convert_tup_ty_args(codegen, FIR_FUNC_TY_PARAM(ty), &param_types);
    convert_tup_ty_args(codegen, FIR_FUNC_TY_RET(ty), &ret_types);
    LLVMTypeRef ret_type = ret_types.elem_count == 0
        ? LLVMVoidTypeInContext(codegen->llvm_context)
        : convert_ty(codegen, FIR_FUNC_TY_RET(ty));
    LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types.elems, param_types.elem_count, 0);
    small_llvm_type_vec_destroy(&ret_types);
    small_llvm_type_vec_destroy(&param_types);
    return func_type;
}

static LLVMTypeRef convert_tup_ty(struct llvm_codegen* codegen, const struct fir_node* ty) {
    struct small_llvm_type_vec arg_types;
    small_llvm_type_vec_init(&arg_types);
    convert_tup_ty_args(codegen, ty, &arg_types);
    assert(arg_types.elem_count > 0);
    LLVMTypeRef tup_type = arg_types.elem_count == 1
        ? arg_types.elems[0]
        : LLVMStructTypeInContext(codegen->llvm_context, arg_types.elems, arg_types.elem_count, 0);
    small_llvm_type_vec_destroy(&arg_types);
    return tup_type;
}

static LLVMTypeRef convert_ty_uncached(struct llvm_codegen* codegen, const struct fir_node* ty) {
    switch (ty->tag) {
        case FIR_INT_TY:
            return LLVMIntTypeInContext(codegen->llvm_context, ty->data.bitwidth);
            break;
        case FIR_FLOAT_TY:
            switch (ty->data.bitwidth) {
                case 16: return LLVMHalfTypeInContext(codegen->llvm_context);
                case 32: return LLVMFloatTypeInContext(codegen->llvm_context);
                case 64: return LLVMDoubleTypeInContext(codegen->llvm_context);
                default:
                    assert(false && "unsupported floating-point bitwidth");
                    return NULL;
            }
            break;
        case FIR_PTR_TY:
            // Note: Older versions of LLVM do not support opaque pointers. For that reason, we just
            // create a byte pointer instead, which should be supported everywhere.
            return LLVMPointerType(LLVMIntTypeInContext(codegen->llvm_context, 8), 0);
            break;
        case FIR_TUP_TY:
            return convert_tup_ty(codegen, ty);
            break;
        default:
            assert(false && "unsupported type");
            return NULL;
    }
}

static LLVMTypeRef convert_ty(struct llvm_codegen* codegen, const struct fir_node* ty) {
    assert(fir_node_is_ty(ty));
    void* const* type_ptr = node_map_find(&codegen->llvm_types, &ty);
    if (type_ptr)
        return *type_ptr;

    LLVMTypeRef llvm_type = convert_ty_uncached(codegen, ty);
    assert(llvm_type);
    [[maybe_unused]] bool was_inserted = node_map_insert(&codegen->llvm_types, &ty, (void*[]) { llvm_type });
    assert(was_inserted);
    return llvm_type;
}

static inline LLVMValueRef gen_constant_tup(struct llvm_codegen* codegen, const struct fir_node* tup) {
    struct small_llvm_value_vec args;
    small_llvm_value_vec_init(&args);
    for (size_t i = 0; i < tup->op_count; ++i) {
        LLVMValueRef arg = gen_constant(codegen, tup->ops[i]);
        small_llvm_value_vec_push(&args, &arg);
    }
    return LLVMConstStructInContext(codegen->llvm_context, args.elems, args.elem_count, false);
}

static LLVMValueRef gen_constant(
    struct llvm_codegen* codegen,
    const struct fir_node* node)
{
    assert(can_be_llvm_constant(node));
    void*const* constant_ptr = node_map_find(&codegen->llvm_constants, &node);
    if (constant_ptr)
        return *constant_ptr;

    LLVMValueRef llvm_constant = NULL;
    switch (node->tag) {
        case FIR_CONST:
            if (fir_node_is_int_const(node)) {
                llvm_constant = LLVMConstInt(convert_ty(codegen, node->ty), node->data.int_val, false);
            } else if (fir_node_is_float_const(node)) {
                llvm_constant = LLVMConstReal(convert_ty(codegen, node->ty), node->data.float_val);
            } else {
                assert(false && "unsupported literal constant");
                return NULL;
            }
            break;
        case FIR_TUP:
            llvm_constant = gen_constant_tup(codegen, node);
            break;
        default:
            assert(false && "unsupported constant");
            return NULL;
    }
    assert(llvm_constant);

    [[maybe_unused]] bool was_inserted = node_map_insert(&codegen->llvm_constants, &node, (void*[]) { llvm_constant });
    assert(was_inserted);
    return llvm_constant;
}

static struct graph_node* find_op_block(
    struct llvm_codegen* codegen,
    struct graph_node* use_block,
    const struct fir_node* op)
{
    const struct block_list* def_blocks = schedule_find_blocks(&codegen->schedule, op);
    for (size_t i = 0; i < def_blocks->elem_count; ++i) {
        if (cfg_is_dominated_by(use_block, def_blocks->elems[i]))
            return def_blocks->elems[i];
    }
    return NULL;
}

static LLVMValueRef find_op(
    struct llvm_codegen* codegen,
    struct graph_node* use_block,
    const struct fir_node* op)
{
    if (can_be_llvm_constant(op))
        return gen_constant(codegen, op);
    if (can_be_llvm_param(op)) {
        void*const* llvm_param_ptr = node_map_find(&codegen->llvm_params, &op);
        assert(llvm_param_ptr && "unknown function or basic-block parameter");
        return *llvm_param_ptr;
    }

    struct graph_node* block = find_op_block(codegen, use_block, op);
    assert(block && "could not find definition for operand");
    void* const* llvm_value = scheduled_node_map_find(&codegen->llvm_values,
        &(struct scheduled_node) { .node = op, .block = block });
    assert(llvm_value && "cannot find scheduled node");
    return *llvm_value;
}

static void gen_params_or_phis(
    struct llvm_codegen* codegen,
    const struct fir_node* param_or_phi,
    void (*gen_param_or_phi)(struct llvm_codegen*, const struct fir_node*, size_t))
{
    if (param_or_phi->ty->tag == FIR_TUP_TY) {
        for (size_t i = 0, j = 0; i < param_or_phi->ty->op_count; ++i) {
            if (!can_be_llvm_type(param_or_phi->ty->ops[i]))
                continue;
            gen_param_or_phi(codegen, fir_ext_at(param_or_phi, i), j++);
        }
    } else if (can_be_llvm_type(param_or_phi->ty)) {
        gen_param_or_phi(codegen, param_or_phi, 0);
    }
}

static void gen_phi(struct llvm_codegen* codegen, const struct fir_node* param, [[maybe_unused]] size_t index) {
    char* phi_name = fir_node_unique_name(param);
    LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, convert_ty(codegen, param->ty), phi_name);
    free(phi_name);
    node_map_insert(&codegen->llvm_params, &param, (void*[]) { phi });
}

static void gen_param(struct llvm_codegen* codegen, const struct fir_node* param, size_t index) {
    LLVMValueRef llvm_param = LLVMGetParam(codegen->llvm_func, index);
    char* param_name = fir_node_unique_name(param);
    LLVMSetValueName2(llvm_param, param_name, strlen(param_name));
    free(param_name);
    node_map_insert(&codegen->llvm_params, &param, (void*[]) { llvm_param });
}

static void gen_return(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* ret)
{
    if (fir_node_is_unit(FIR_CALL_ARG(ret))) {
        LLVMBuildRetVoid(codegen->llvm_builder);
    } else {
        LLVMValueRef arg = find_op(codegen, block, FIR_CALL_ARG(ret));
        LLVMBuildRet(codegen->llvm_builder, arg);
    }
}

static void gen_branch(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* branch)
{
    assert(fir_node_jump_target_count(branch) == 2);
    const struct fir_node* const* targets = fir_node_jump_targets(branch);
    LLVMValueRef cond = find_op(codegen, block, fir_node_switch_cond(branch));
    LLVMBasicBlockRef true_block = *graph_node_map_find(&codegen->llvm_blocks,
        (struct graph_node*[]) { cfg_find(&codegen->cfg, targets[1]) });
    LLVMBasicBlockRef false_block = *graph_node_map_find(&codegen->llvm_blocks,
        (struct graph_node*[]) { cfg_find(&codegen->cfg, targets[0]) });
    LLVMBuildCondBr(codegen->llvm_builder, cond, true_block, false_block);
}

static void gen_switch(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* switch_)
{
    // TODO
    assert(false && "unimplemented");
}

static void gen_jump(
    struct llvm_codegen* codegen,
    const struct fir_node* jump)
{
    const struct fir_node* const* targets = fir_node_jump_targets(jump);
    assert(fir_node_jump_target_count(jump) == 1);
    LLVMBasicBlockRef llvm_block = *graph_node_map_find(&codegen->llvm_blocks,
        (struct graph_node*[]) { cfg_find(&codegen->cfg, targets[0]) });
    LLVMBuildBr(codegen->llvm_builder, llvm_block);
}

static LLVMValueRef gen_call(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* call)
{
    assert(false && "unimplemented");
    return NULL;
}

static LLVMValueRef gen_tup_from_args(
    struct llvm_codegen* codegen,
    LLVMTypeRef llvm_type,
    const LLVMValueRef* args,
    size_t arg_count)
{
    assert(arg_count != 0);
    if (arg_count == 1)
        return args[0];

    LLVMValueRef llvm_val = LLVMGetUndef(llvm_type);
    for (size_t i = 0; i < arg_count; ++i)
        llvm_val = LLVMBuildInsertValue(codegen->llvm_builder, llvm_val, args[i], i, "tup");
    return llvm_val;
}

static LLVMValueRef gen_tup(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* tup)
{
    struct small_llvm_value_vec args;
    small_llvm_value_vec_init(&args);
    for (size_t i = 0; i < tup->op_count; ++i) {
        if (!can_be_llvm_type(tup->ops[i]->ty))
            continue;
        LLVMValueRef arg = find_op(codegen, block, tup->ops[i]);
        small_llvm_value_vec_push(&args, &arg);
    }
    LLVMValueRef llvm_val = gen_tup_from_args(codegen,
        convert_ty(codegen, tup->ty), args.elems, args.elem_count);
    small_llvm_value_vec_destroy(&args);
    return llvm_val;
}

static LLVMValueRef gen_param_as_tup(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* param)
{
    assert(param->ty->tag == FIR_TUP_TY);
    struct small_llvm_value_vec args;
    small_llvm_value_vec_init(&args);
    for (size_t i = 0; i < param->ty->op_count; ++i) {
        if (!can_be_llvm_type(param->ty->ops[i]))
            continue;
        LLVMValueRef arg = find_op(codegen, block, fir_ext_at(param, i));
        small_llvm_value_vec_push(&args, &arg);
    }
    LLVMValueRef llvm_val = gen_tup_from_args(codegen,
        convert_ty(codegen, param->ty), args.elems, args.elem_count);
    small_llvm_value_vec_destroy(&args);
    return llvm_val;
}

static LLVMValueRef gen_alloca(
    struct llvm_codegen* codegen,
    const struct fir_node* ty,
    const char* name)
{
    LLVMBasicBlockRef old_block   = LLVMGetInsertBlock(codegen->llvm_builder);
    LLVMBasicBlockRef entry_block = LLVMGetEntryBasicBlock(codegen->llvm_func);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, entry_block);
    LLVMValueRef alloca = LLVMBuildAlloca(codegen->llvm_builder, convert_ty(codegen, ty), name);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, old_block);
    return alloca;
}

static LLVMValueRef gen_ext_or_ins_via_alloca(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* elem_ty,
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem)
{
    // Write the aggregate into a temporary alloca
    LLVMTypeRef aggr_type = convert_ty(codegen, aggr->ty);
    LLVMValueRef llvm_index = find_op(codegen, block, index);
    LLVMValueRef ptr = gen_alloca(codegen, aggr->ty, "tmp_alloca");
    LLVMBuildStore(codegen->llvm_builder, find_op(codegen, block, aggr), ptr);
    LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(codegen->llvm_builder, aggr_type, ptr,
        (LLVMValueRef[]) { LLVMConstNull(LLVMTypeOf(llvm_index)), llvm_index }, 2, "elem_addr"); 
    if (elem) {
        LLVMValueRef llvm_elem = find_op(codegen, block, elem);
        LLVMBuildStore(codegen->llvm_builder, llvm_elem, elem_ptr);
        return LLVMBuildLoad2(codegen->llvm_builder, aggr_type, ptr, "ins_load");
    }
    LLVMTypeRef elem_type = convert_ty(codegen, elem_ty);
    return LLVMBuildLoad2(codegen->llvm_builder, elem_type, elem_ptr, "ext_load");
}

static LLVMValueRef gen_ext(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* ext)
{
    if (FIR_EXT_INDEX(ext)->tag == FIR_CONST) {
        LLVMValueRef aggr = find_op(codegen, block, FIR_EXT_AGGR(ext));
        return LLVMBuildExtractValue(codegen->llvm_builder, aggr, FIR_EXT_INDEX(ext)->data.int_val, "ext");
    }
    return gen_ext_or_ins_via_alloca(codegen, block, ext->ty, FIR_EXT_AGGR(ext), FIR_EXT_INDEX(ext), NULL);
}

static LLVMValueRef gen_ins(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* ins)
{
    if (FIR_INS_INDEX(ins)->tag == FIR_CONST) {
        LLVMValueRef aggr = find_op(codegen, block, FIR_INS_AGGR(ins));
        LLVMValueRef elem = find_op(codegen, block, FIR_INS_ELEM(ins));
        return LLVMBuildInsertValue(codegen->llvm_builder, aggr, elem, FIR_INS_INDEX(ins)->data.int_val, "ins");
    }
    return gen_ext_or_ins_via_alloca(codegen, block,
        FIR_INS_ELEM(ins)->ty, FIR_INS_AGGR(ins), FIR_INS_INDEX(ins), FIR_INS_ELEM(ins));
}

static LLVMValueRef gen_store(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* store)
{
    LLVMValueRef ptr = find_op(codegen, block, FIR_STORE_PTR(store));
    LLVMValueRef val = find_op(codegen, block, FIR_STORE_VAL(store));
    LLVMValueRef llvm_store = LLVMBuildStore(codegen->llvm_builder, val, ptr);
    if (store->data.mem_flags & FIR_MEM_VOLATILE)
        LLVMSetVolatile(llvm_store, 1);
    return NULL;
}

static LLVMValueRef gen_load(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* load)
{
    LLVMValueRef ptr = find_op(codegen, block, FIR_LOAD_PTR(load));
    LLVMTypeRef load_type = convert_ty(codegen, load->ty);
    LLVMValueRef llvm_load = LLVMBuildLoad2(codegen->llvm_builder, load_type, ptr, "load");
    if (load->data.mem_flags & FIR_MEM_VOLATILE)
        LLVMSetVolatile(llvm_load, 1);
    return llvm_load;
}

static LLVMValueRef gen_local(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* local)
{
    LLVMValueRef ptr = gen_alloca(codegen, FIR_LOCAL_INIT(local)->ty, "local");
    if (FIR_LOCAL_INIT(local)->tag != FIR_BOT)
        LLVMBuildStore(codegen->llvm_builder, find_op(codegen, block, FIR_LOCAL_INIT(local)), ptr);
    return ptr;
}

static LLVMValueRef gen_node(
    struct llvm_codegen* codegen,
    struct graph_node* block,
    const struct fir_node* node)
{
    assert(!can_be_llvm_constant(node) && "constants do not need to be placed in a specific block");
    assert(!can_be_llvm_param(node) && "parameters are not instructions in LLVM IR");
    LLVMBasicBlockRef llvm_block = *graph_node_map_find(&codegen->llvm_blocks, &block);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, llvm_block);

    switch (node->tag) {
        case FIR_LOCAL:
            return gen_local(codegen, block, node);
        case FIR_LOAD:
            return gen_load(codegen, block, node);
        case FIR_STORE:
            return gen_store(codegen, block, node);
        case FIR_CALL:
            if (FIR_CALL_CALLEE(node) == fir_node_func_return(codegen->scope.func)) {
                gen_return(codegen, block, node);
                return NULL;
            } else if (fir_node_is_jump(node)) {
                if (fir_node_is_branch(node)) {
                    gen_branch(codegen, block, node);
                } else if (fir_node_is_choice(node)) {
                    gen_switch(codegen, block, node);
                } else {
                    gen_jump(codegen, node);
                }
                return NULL;
            } else {
                return gen_call(codegen, block, node);
            }
        case FIR_TUP:
            return gen_tup(codegen, block, node);
        case FIR_PARAM:
            return gen_param_as_tup(codegen, block, node);
        case FIR_EXT:
            return gen_ext(codegen, block, node);
        case FIR_INS:
            return gen_ins(codegen, block, node);
        default:
            assert(false && "invalid node tag");
            return NULL;
    }
}

static bool enqueue_node(struct llvm_codegen* codegen, const struct fir_node* node) {
    if (node->tag == FIR_FUNC ||
        node->tag == FIR_GLOBAL ||
        can_be_llvm_param(node) ||
        can_be_llvm_constant(node))
        return true;
    return !unique_node_stack_push(&codegen->node_stack, &node);
}

static bool enqueue_nodes(
    struct llvm_codegen* codegen,
    const struct fir_node* const* nodes,
    size_t node_count)
{
    for (size_t i = 0; i < node_count; ++i) {
        if (!enqueue_node(codegen, nodes[i]))
            return false;
    }
    return true;
}

static bool enqueue_ops(struct llvm_codegen* codegen, const struct fir_node* node) {
    return enqueue_nodes(codegen, node->ops, node->op_count);
}

static bool enqueue_call_deps(struct llvm_codegen* codegen, const struct fir_node* call) {
    // Calls and jumps do not need to have an LLVM tuple as an argument, since we can set
    // each individual function argument for function calls, and each individual phi node
    // for jumps. However, returns still need a tuple when multiple values are returned.
    bool is_return = FIR_CALL_CALLEE(call) == fir_node_func_return(codegen->scope.func);
    if (FIR_CALL_ARG(call)->tag == FIR_TUP && !is_return) {
        if (!enqueue_ops(codegen, FIR_CALL_ARG(call)))
            return false;
    } else if (!enqueue_node(codegen, FIR_CALL_ARG(call))) {
        return false;
    }

    // For the callee, we need to be careful not to generate arrays and extracts, as branches are
    // implemented as `call(ext(array(b0, b1, b2, ...), c), ...)`.
    if (fir_node_is_jump(call)) {
        if (!enqueue_nodes(codegen, fir_node_jump_targets(call), fir_node_jump_target_count(call)))
            return false;
        if (fir_node_is_switch(call) &&
            !enqueue_node(codegen, fir_node_switch_cond(call)))
            return false;
    } else if (!enqueue_node(codegen, FIR_CALL_CALLEE(call))) {
        return false;
    }
    return true;
}

static bool enqueue_node_deps(struct llvm_codegen* codegen, const struct fir_node* node) {
    if (node->tag == FIR_CALL)
        return enqueue_call_deps(codegen, node);
    return enqueue_ops(codegen, node);
}

static void gen_nodes_in_block(struct llvm_codegen* codegen, struct graph_node* block) {
    const struct fir_node* block_func = cfg_block_func(block);
    assert(unique_node_stack_is_empty(&codegen->node_stack));
    unique_node_stack_push(&codegen->node_stack, &FIR_FUNC_BODY(block_func));
    while (!unique_node_stack_is_empty(&codegen->node_stack)) {
        const struct fir_node* node = *unique_node_stack_last(&codegen->node_stack);
        if (!enqueue_node_deps(codegen, node))
            continue;

        unique_node_stack_pop(&codegen->node_stack);

        const struct block_list* target_blocks = schedule_find_blocks(&codegen->schedule, node);
        for (size_t i = 0; i < target_blocks->elem_count; ++i) {
            struct graph_node* target_block = target_blocks->elems[i];
            struct scheduled_node scheduled_node = { .node = node, .block = target_block };
            assert(!scheduled_node_map_find(&codegen->llvm_values, &scheduled_node));
            LLVMValueRef llvm_value = gen_node(codegen, target_block, node);
            if (!llvm_value)
                continue;
            [[maybe_unused]] bool was_inserted =
                scheduled_node_map_insert(&codegen->llvm_values, &scheduled_node, (void*[]) { llvm_value });
            assert(was_inserted);
        }
    }
}

static void gen_block(struct llvm_codegen* codegen, struct graph_node* block) {
    const struct fir_node* block_func = cfg_block_func(block);
    char* block_name = fir_node_unique_name(block_func);
    LLVMBasicBlockRef llvm_block = LLVMAppendBasicBlockInContext(codegen->llvm_context, codegen->llvm_func, block_name);
    free(block_name);

    graph_node_map_insert(&codegen->llvm_blocks, &block, (void*[]) { llvm_block });

    LLVMPositionBuilderAtEnd(codegen->llvm_builder, llvm_block);
    bool is_entry_block = block == codegen->cfg.graph.source;
    if (!is_entry_block)
        gen_params_or_phis(codegen, fir_param(block_func), gen_phi);
}

static void gen_func(struct llvm_codegen* codegen, const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);

    char* func_name = fir_node_unique_name(func);
    codegen->llvm_func = LLVMAddFunction(
        codegen->llvm_module, func_name, convert_func_ty(codegen, func->ty));
    free(func_name);

    gen_params_or_phis(codegen, fir_param(func), gen_param);

    codegen->scope = scope_create(func);
    codegen->cfg = cfg_create(&codegen->scope);
    codegen->schedule = schedule_create(&codegen->cfg);

    VEC_REV_FOREACH(struct graph_node*, block_ptr, codegen->cfg.post_order) {
        if (*block_ptr != codegen->cfg.graph.sink)
            gen_block(codegen, *block_ptr);
    }

    VEC_FOREACH(struct graph_node*, block_ptr, codegen->cfg.post_order) {
        if (*block_ptr != codegen->cfg.graph.sink)
            gen_nodes_in_block(codegen, *block_ptr);
    }

    schedule_destroy(&codegen->schedule);
    cfg_destroy(&codegen->cfg);
    scope_destroy(&codegen->scope);
}

static void llvm_codegen_destroy(struct fir_codegen* codegen) {
    struct llvm_codegen* llvm_codegen = (struct llvm_codegen*)codegen;
    node_map_destroy(&llvm_codegen->llvm_constants);
    node_map_destroy(&llvm_codegen->llvm_types);
    node_map_destroy(&llvm_codegen->llvm_params);
    graph_node_map_destroy(&llvm_codegen->llvm_blocks);
    scheduled_node_map_destroy(&llvm_codegen->llvm_values);
    unique_node_stack_destroy(&llvm_codegen->node_stack);
    LLVMDisposeBuilder(llvm_codegen->llvm_builder);
    LLVMContextDispose(llvm_codegen->llvm_context);
    free(codegen);
}

static bool llvm_codegen_run(
    struct fir_codegen* codegen,
    struct fir_mod* mod,
    const char* output_file)
{
    struct llvm_codegen* llvm_codegen = (struct llvm_codegen*)codegen;
    node_map_clear(&llvm_codegen->llvm_types);
    node_map_clear(&llvm_codegen->llvm_constants);
    llvm_codegen->llvm_module = LLVMModuleCreateWithNameInContext(fir_mod_name(mod), llvm_codegen->llvm_context);

    struct fir_node* const* funcs = fir_mod_funcs(mod);
    for (size_t i = 0, func_count = fir_mod_func_count(mod); i < func_count; ++i) {
        if (fir_node_is_cont_ty(funcs[i]->ty))
            continue;

        scheduled_node_map_clear(&llvm_codegen->llvm_values);
        graph_node_map_clear(&llvm_codegen->llvm_blocks);
        node_map_clear(&llvm_codegen->llvm_params);
        gen_func(llvm_codegen, funcs[i]);
    }

    bool status = true;
    char* err = NULL;
    if (LLVMPrintModuleToFile(llvm_codegen->llvm_module, output_file, &err) == 1) {
        fputs(err, stderr);
        LLVMDisposeMessage(err);
        status = false;
    }

    LLVMDisposeModule(llvm_codegen->llvm_module);
    return status;
}

struct fir_codegen* llvm_codegen_create(
    [[maybe_unused]] const char** options,
    [[maybe_unused]] size_t option_count)
{
    struct llvm_codegen* llvm_codegen = xcalloc(1, sizeof(struct llvm_codegen));
    llvm_codegen->base.destroy = llvm_codegen_destroy;
    llvm_codegen->base.run = llvm_codegen_run;
    llvm_codegen->llvm_context = LLVMContextCreate();
    llvm_codegen->llvm_builder = LLVMCreateBuilderInContext(llvm_codegen->llvm_context);
    llvm_codegen->llvm_types = node_map_create();
    llvm_codegen->llvm_constants = node_map_create();
    llvm_codegen->llvm_blocks = graph_node_map_create();
    llvm_codegen->llvm_params = node_map_create();
    llvm_codegen->llvm_values = scheduled_node_map_create();
    llvm_codegen->node_stack = unique_node_stack_create();
    return &llvm_codegen->base;
}
