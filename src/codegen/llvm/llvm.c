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
    struct unique_node_stack node_stack;

    struct scope scope;
    struct cfg cfg;
    struct schedule schedule;
};

struct codegen_context {
    struct llvm_codegen* codegen;
    struct graph_node* block;
    struct unique_node_stack* node_stack;
    const struct fir_node* func;
    LLVMValueRef (*find_op)(struct codegen_context*, const struct fir_node*);
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
        case FIR_FLOAT_TY:
            switch (ty->data.bitwidth) {
                case 16: return LLVMHalfTypeInContext(codegen->llvm_context);
                case 32: return LLVMFloatTypeInContext(codegen->llvm_context);
                case 64: return LLVMDoubleTypeInContext(codegen->llvm_context);
                default:
                    assert(false && "unsupported floating-point bitwidth");
                    return NULL;
            }
        case FIR_PTR_TY:
            // Note: Older versions of LLVM do not support opaque pointers. For that reason, we just
            // create a byte pointer instead, which should be supported everywhere.
            return LLVMPointerType(LLVMIntTypeInContext(codegen->llvm_context, 8), 0);
        case FIR_TUP_TY:
            return convert_tup_ty(codegen, ty);
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

static LLVMValueRef gen_constant_uncached(
    struct llvm_codegen* codegen,
    const struct fir_node* node)
{
    switch (node->tag) {
        case FIR_CONST:
            if (fir_node_is_int_const(node)) {
                return LLVMConstInt(convert_ty(codegen, node->ty), node->data.int_val, false);
            } else if (fir_node_is_float_const(node)) {
                return LLVMConstReal(convert_ty(codegen, node->ty), node->data.float_val);
            } else {
                assert(false && "unsupported literal constant");
                return NULL;
            }
        case FIR_TUP:
            return gen_constant_tup(codegen, node);
        default:
            assert(false && "unsupported constant");
            return NULL;
    }
}

static LLVMValueRef gen_constant(
    struct llvm_codegen* codegen,
    const struct fir_node* node)
{
    assert(can_be_llvm_constant(node));
    void*const* constant_ptr = node_map_find(&codegen->llvm_constants, &node);
    if (constant_ptr)
        return *constant_ptr;

    LLVMValueRef llvm_constant = gen_constant_uncached(codegen, node);
    assert(llvm_constant);
    [[maybe_unused]] bool was_inserted = node_map_insert(&codegen->llvm_constants, &node, (void*[]) { llvm_constant });
    assert(was_inserted);
    return llvm_constant;
}

static struct graph_node* find_op_block(
    struct schedule* schedule,
    struct graph_node* use_block,
    const struct fir_node* op)
{
    const struct block_list* def_blocks = schedule_find_blocks(schedule, op);
    for (size_t i = 0; i < def_blocks->elem_count; ++i) {
        if (cfg_is_dominated_by(use_block, def_blocks->elems[i]))
            return def_blocks->elems[i];
    }
    return NULL;
}

static LLVMValueRef find_op(struct codegen_context* context, const struct fir_node* op) {
    struct llvm_codegen* codegen = context->codegen;
    if (can_be_llvm_constant(op))
        return gen_constant(codegen, op);
    if (can_be_llvm_param(op)) {
        void*const* llvm_param_ptr = node_map_find(&codegen->llvm_params, &op);
        assert(llvm_param_ptr && "unknown function or basic-block parameter");
        return *llvm_param_ptr;
    }

    struct graph_node* block = find_op_block(&codegen->schedule, context->block, op);
    assert(block && "could not find definition for operand");
    void* const* llvm_value = scheduled_node_map_find(&codegen->llvm_values,
        &(struct scheduled_node) { .node = op, .block = block });
    assert(llvm_value && "cannot find scheduled node");
    return *llvm_value;
}

static LLVMValueRef enqueue_op(struct codegen_context* context, const struct fir_node* op) {
    assert(!fir_node_is_ty(op));
    assert(op->tag != FIR_FUNC);
    assert(op->tag != FIR_GLOBAL);
    if (!context->node_stack ||
        can_be_llvm_constant(op) ||
        can_be_llvm_param(op))
        return NULL;
    if (unique_node_stack_push(context->node_stack, &op))
        context->node_stack = NULL;
    return NULL;
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

static void gen_return(struct codegen_context* context, const struct fir_node* ret) {
    struct small_llvm_value_vec args;
    small_llvm_value_vec_init(&args);

    if (FIR_CALL_ARG(ret)->tag == FIR_TUP) {
        const struct fir_node* tup = FIR_CALL_ARG(ret);
        for (size_t i = 0; i < tup->op_count; ++i) {
            if (!can_be_llvm_type(tup->ops[i]->ty))
                continue;
            LLVMValueRef arg = context->find_op(context, tup->ops[i]);
            small_llvm_value_vec_push(&args, &arg);
        }
    } else {
        LLVMValueRef arg = context->find_op(context, FIR_CALL_ARG(ret));
        small_llvm_value_vec_push(&args, &arg);
    }

    if (context->codegen) {
        if (args.elem_count == 0) {
            LLVMBuildRetVoid(context->codegen->llvm_builder);
        } else if (args.elem_count == 1) {
            LLVMBuildRet(context->codegen->llvm_builder, args.elems[0]);
        } else {
            LLVMBuildAggregateRet(context->codegen->llvm_builder, args.elems, args.elem_count);
        }
    }
    small_llvm_value_vec_destroy(&args);
}

static void gen_branch(struct codegen_context* context, const struct fir_node* branch) {
    assert(fir_node_jump_target_count(branch) == 2);
    LLVMValueRef cond = context->find_op(context, fir_node_switch_cond(branch));

    if (context->codegen) {
        const struct fir_node* const* targets = fir_node_jump_targets(branch);
        LLVMBasicBlockRef true_block = *graph_node_map_find(&context->codegen->llvm_blocks,
            (struct graph_node*[]) { cfg_find(&context->codegen->cfg, targets[1]) });
        LLVMBasicBlockRef false_block = *graph_node_map_find(&context->codegen->llvm_blocks,
            (struct graph_node*[]) { cfg_find(&context->codegen->cfg, targets[0]) });
        LLVMBuildCondBr(context->codegen->llvm_builder, cond, true_block, false_block);
    }
}

static void gen_switch(struct codegen_context*, const struct fir_node*) {
    // TODO
    assert(false && "unimplemented");
}

static void gen_jump(struct llvm_codegen* codegen, const struct fir_node* jump) {
    const struct fir_node* const* targets = fir_node_jump_targets(jump);
    assert(fir_node_jump_target_count(jump) == 1);
    LLVMBasicBlockRef llvm_block = *graph_node_map_find(&codegen->llvm_blocks,
        (struct graph_node*[]) { cfg_find(&codegen->cfg, targets[0]) });
    LLVMBuildBr(codegen->llvm_builder, llvm_block);
}

static LLVMValueRef gen_call(struct codegen_context*, const struct fir_node*) {
    assert(false && "unimplemented");
    return NULL;
}

static LLVMValueRef gen_tup_from_args(
    struct llvm_codegen* codegen,
    const struct fir_node* aggr_ty,
    const LLVMValueRef* args,
    size_t arg_count)
{
    if (arg_count == 0)
        return NULL;
    if (arg_count == 1)
        return args[0];

    LLVMValueRef llvm_val = LLVMGetUndef(convert_ty(codegen, aggr_ty));
    for (size_t i = 0; i < arg_count; ++i)
        llvm_val = LLVMBuildInsertValue(codegen->llvm_builder, llvm_val, args[i], i, "tup");
    return llvm_val;
}

static LLVMValueRef gen_tup(struct codegen_context* context, const struct fir_node* tup) {
    struct small_llvm_value_vec args;
    small_llvm_value_vec_init(&args);
    for (size_t i = 0; i < tup->op_count; ++i) {
        if (!can_be_llvm_type(tup->ops[i]->ty))
            continue;
        LLVMValueRef arg = context->find_op(context, tup->ops[i]);
        small_llvm_value_vec_push(&args, &arg);
    }

    LLVMValueRef llvm_tup = context->codegen
        ? gen_tup_from_args(context->codegen, tup->ty, args.elems, args.elem_count) : NULL;

    small_llvm_value_vec_destroy(&args);
    return llvm_tup;
}

static LLVMValueRef gen_param_as_tup(struct codegen_context* context, const struct fir_node* param) {
    assert(param->ty->tag == FIR_TUP_TY);
    struct small_llvm_value_vec args;
    small_llvm_value_vec_init(&args);
    for (size_t i = 0; i < param->ty->op_count; ++i) {
        if (!can_be_llvm_type(param->ty->ops[i]))
            continue;
        LLVMValueRef arg = context->find_op(context, fir_ext_at(param, i));
        small_llvm_value_vec_push(&args, &arg);
    }

    LLVMValueRef llvm_val = context->codegen
        ? gen_tup_from_args(context->codegen, param->ty, args.elems, args.elem_count) : NULL;

    small_llvm_value_vec_destroy(&args);
    return llvm_val;
}

static LLVMValueRef gen_alloca(struct llvm_codegen* codegen, const struct fir_node* ty, const char* name) {
    LLVMBasicBlockRef old_block   = LLVMGetInsertBlock(codegen->llvm_builder);
    LLVMBasicBlockRef entry_block = LLVMGetEntryBasicBlock(codegen->llvm_func);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, entry_block);
    LLVMValueRef alloca = LLVMBuildAlloca(codegen->llvm_builder, convert_ty(codegen, ty), name);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, old_block);
    return alloca;
}

static LLVMValueRef gen_ext_or_ins_via_alloca(
    struct codegen_context* context,
    const struct fir_node* elem_ty,
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem)
{
    // Write the aggregate into a temporary alloca
    LLVMValueRef llvm_index = context->find_op(context, index);
    LLVMValueRef llvm_aggr  = context->find_op(context, aggr);
    LLVMValueRef llvm_elem  = elem ? context->find_op(context, elem) : NULL;

    if (!context->codegen)
        return NULL;

    LLVMTypeRef aggr_type = convert_ty(context->codegen, aggr->ty);
    LLVMValueRef ptr = gen_alloca(context->codegen, aggr->ty, "tmp_alloca");
    LLVMBuildStore(context->codegen->llvm_builder, llvm_aggr, ptr);
    LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(context->codegen->llvm_builder, aggr_type, ptr,
        (LLVMValueRef[]) { LLVMConstNull(LLVMTypeOf(llvm_index)), llvm_index }, 2, "elem_addr");
    if (elem) {
        LLVMBuildStore(context->codegen->llvm_builder, llvm_elem, elem_ptr);
        return LLVMBuildLoad2(context->codegen->llvm_builder, aggr_type, ptr, "ins_load");
    }
    LLVMTypeRef elem_type = convert_ty(context->codegen, elem_ty);
    return LLVMBuildLoad2(context->codegen->llvm_builder, elem_type, elem_ptr, "ext_load");
}

static unsigned remap_index(const struct fir_node* aggr_ty, size_t index) {
    if (aggr_ty->tag != FIR_TUP_TY)
        return index;
    // Some tuple elements may not be convertible into LLVM types and are then skipped. Thus,
    // indices used in an extract or insert operation need to be remapped.
    unsigned new_index = 0;
    for (size_t i = 0; i < index; i++)
        new_index += can_be_llvm_type(aggr_ty->ops[i]) ? 1 : 0;
    return new_index;
}

static LLVMValueRef gen_ext(struct codegen_context* context, const struct fir_node* ext) {
    if (FIR_EXT_INDEX(ext)->tag == FIR_CONST) {
        LLVMValueRef aggr = context->find_op(context, FIR_EXT_AGGR(ext));
        if (!context->codegen)
            return NULL;

        unsigned index = remap_index(FIR_EXT_AGGR(ext)->ty, FIR_EXT_INDEX(ext)->data.int_val);
        return LLVMBuildExtractValue(context->codegen->llvm_builder, aggr, index, "ext");
    }

    return gen_ext_or_ins_via_alloca(context, ext->ty, FIR_EXT_AGGR(ext), FIR_EXT_INDEX(ext), NULL);
}

static LLVMValueRef gen_ins(struct codegen_context* context, const struct fir_node* ins) {
    if (FIR_INS_INDEX(ins)->tag == FIR_CONST) {
        LLVMValueRef aggr = context->find_op(context, FIR_INS_AGGR(ins));
        LLVMValueRef elem = context->find_op(context, FIR_INS_ELEM(ins));
        if (!context->codegen)
            return NULL;

        unsigned index = remap_index(ins->ty, FIR_INS_INDEX(ins)->data.int_val);
        return LLVMBuildInsertValue(context->codegen->llvm_builder, aggr, elem, index, "ins");
    }

    return gen_ext_or_ins_via_alloca(context,
        FIR_INS_ELEM(ins)->ty, FIR_INS_AGGR(ins), FIR_INS_INDEX(ins), FIR_INS_ELEM(ins));
}

static LLVMValueRef gen_store(struct codegen_context* context, const struct fir_node* store) {
    LLVMValueRef ptr = context->find_op(context, FIR_STORE_PTR(store));
    LLVMValueRef val = context->find_op(context, FIR_STORE_VAL(store));
    if (!context->codegen)
        return NULL;

    LLVMValueRef llvm_store = LLVMBuildStore(context->codegen->llvm_builder, val, ptr);
    if (store->data.mem_flags & FIR_MEM_VOLATILE)
        LLVMSetVolatile(llvm_store, 1);
    return NULL;
}

static LLVMValueRef gen_load(struct codegen_context* context, const struct fir_node* load) {
    LLVMValueRef ptr = context->find_op(context, FIR_LOAD_PTR(load));
    if (!context->codegen)
        return NULL;

    LLVMTypeRef load_type = convert_ty(context->codegen, load->ty);
    LLVMValueRef llvm_load = LLVMBuildLoad2(context->codegen->llvm_builder, load_type, ptr, "load");
    if (load->data.mem_flags & FIR_MEM_VOLATILE)
        LLVMSetVolatile(llvm_load, 1);
    return llvm_load;
}

static LLVMValueRef gen_local(struct codegen_context* context, const struct fir_node* local) {
    const bool has_init = FIR_LOCAL_INIT(local)->tag != FIR_BOT;
    LLVMValueRef init = has_init ? context->find_op(context, FIR_LOCAL_INIT(local)) : NULL;
    if (!context->codegen)
        return NULL;

    LLVMValueRef ptr = gen_alloca(context->codegen, FIR_LOCAL_INIT(local)->ty, "local");
    if (has_init)
        LLVMBuildStore(context->codegen->llvm_builder, init, ptr);
    return ptr;
}

static LLVMValueRef gen_addrof(struct codegen_context* context, const struct fir_node* addrof) {
    LLVMValueRef ptr   = context->find_op(context, FIR_ADDROF_PTR(addrof));
    LLVMValueRef index = context->find_op(context, FIR_ADDROF_INDEX(addrof));
    if (!context->codegen)
        return NULL;

    LLVMTypeRef aggr_type = convert_ty(context->codegen, FIR_ADDROF_TY(addrof));
    return LLVMBuildInBoundsGEP2(context->codegen->llvm_builder, aggr_type, ptr,
        (LLVMValueRef[]) { LLVMConstNull(LLVMTypeOf(index)), index }, 2, "addrof_elem");
}

static LLVMValueRef gen_cast_op(struct codegen_context* context, const struct fir_node* cast_op) {
    LLVMValueRef arg = context->find_op(context, FIR_CAST_OP_ARG(cast_op));
    if (!context->codegen)
        return NULL;

    struct llvm_codegen* codegen = context->codegen;
    LLVMTypeRef dest_type = convert_ty(codegen, cast_op->ty);
    switch (cast_op->tag) {
        case FIR_BITCAST: return LLVMBuildBitCast(codegen->llvm_builder, arg, dest_type, "bitcast");
        case FIR_UTOF:    return LLVMBuildUIToFP(codegen->llvm_builder, arg, dest_type, "utof");
        case FIR_STOF:    return LLVMBuildSIToFP(codegen->llvm_builder, arg, dest_type, "stof");
        case FIR_FTOU:    return LLVMBuildFPToUI(codegen->llvm_builder, arg, dest_type, "ftou");
        case FIR_FTOS:    return LLVMBuildFPToSI(codegen->llvm_builder, arg, dest_type, "ftos");
        case FIR_FEXT:    return LLVMBuildFPExt(codegen->llvm_builder, arg, dest_type, "fext");
        case FIR_ZEXT:    return LLVMBuildZExt(codegen->llvm_builder, arg, dest_type, "zext");
        case FIR_SEXT:    return LLVMBuildSExt(codegen->llvm_builder, arg, dest_type, "sext");
        case FIR_ITRUNC:  return LLVMBuildTrunc(codegen->llvm_builder, arg, dest_type, "itrunc");
        case FIR_FTRUNC:  return LLVMBuildFPTrunc(codegen->llvm_builder, arg, dest_type, "ftrunc");
        default:
            assert(false && "invalid cast type");
            return NULL;
    }
}

static LLVMValueRef gen_node(struct codegen_context* context, const struct fir_node* node) {
    assert(!can_be_llvm_constant(node) && "constants do not need to be placed in a specific block");
    assert(!can_be_llvm_param(node) && "parameters are not instructions in LLVM IR");

    if (context->codegen) {
        // Reposition the builder in the correct block when generating instructions
        LLVMBasicBlockRef llvm_block = *graph_node_map_find(&context->codegen->llvm_blocks, &context->block);
        LLVMPositionBuilderAtEnd(context->codegen->llvm_builder, llvm_block);
    }

    switch (node->tag) {
        case FIR_LOCAL:
            return gen_local(context, node);
        case FIR_LOAD:
            return gen_load(context, node);
        case FIR_STORE:
            return gen_store(context, node);
        case FIR_CALL:
            if (FIR_CALL_CALLEE(node) == fir_node_func_return(context->func)) {
                gen_return(context, node);
                return NULL;
            } else if (fir_node_is_jump(node)) {
                if (fir_node_is_branch(node)) {
                    gen_branch(context, node);
                } else if (fir_node_is_choice(node)) {
                    gen_switch(context, node);
                } else if (context->codegen) {
                    gen_jump(context->codegen, node);
                }
                return NULL;
            } else {
                return gen_call(context, node);
            }
        case FIR_TUP:
            return gen_tup(context, node);
        case FIR_PARAM:
            return gen_param_as_tup(context, node);
        case FIR_EXT:
            return gen_ext(context, node);
        case FIR_INS:
            return gen_ins(context, node);
        case FIR_ADDROF:
            return gen_addrof(context, node);
        #define x(tag, ...) case FIR_##tag:
        FIR_CAST_OP_LIST(x)
            return gen_cast_op(context, node);
        default:
            assert(false && "invalid node tag");
            return NULL;
    }
}

static void gen_nodes(struct llvm_codegen* codegen) {
    while (!unique_node_stack_is_empty(&codegen->node_stack)) {
        const struct fir_node* node = *unique_node_stack_last(&codegen->node_stack);

        struct codegen_context enqueue_context = {
            .func = codegen->scope.func,
            .find_op = enqueue_op,
            .node_stack = &codegen->node_stack
        };
        gen_node(&enqueue_context, node);
        if (!enqueue_context.node_stack)
            continue;

        unique_node_stack_pop(&codegen->node_stack);

        const struct block_list* target_blocks = schedule_find_blocks(&codegen->schedule, node);
        for (size_t i = 0; i < target_blocks->elem_count; ++i) {
            struct graph_node* target_block = target_blocks->elems[i];
            struct scheduled_node scheduled_node = { .node = node, .block = target_block };
            assert(!scheduled_node_map_find(&codegen->llvm_values, &scheduled_node));
            LLVMValueRef llvm_value = gen_node(&(struct codegen_context) {
                .func = codegen->scope.func,
                .block = target_block,
                .find_op = find_op,
                .codegen = codegen
            }, node);
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
        if (*block_ptr == codegen->cfg.graph.sink)
            continue;

        unique_node_stack_push(&codegen->node_stack, &FIR_FUNC_BODY(cfg_block_func(*block_ptr)));
        gen_nodes(codegen);
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
