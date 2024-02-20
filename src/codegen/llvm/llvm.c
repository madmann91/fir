#include "fir/codegen.h"
#include "fir/module.h"
#include "fir/node.h"

#include "support/alloc.h"
#include "support/datatypes.h"
#include "codegen/codegen.h"
#include "analysis/scope.h"
#include "analysis/cfg.h"
#include "analysis/schedule.h"

#include <stdlib.h>
#include <assert.h>

#include <llvm-c/Core.h>

struct llvm_codegen {
    struct fir_codegen base;

    LLVMContextRef llvm_context;
    LLVMModuleRef  llvm_module;
    LLVMBuilderRef llvm_builder;
    LLVMValueRef   llvm_func;

    struct node_map llvm_types;
    struct node_map llvm_constants;
    struct node_map llvm_blocks;
    struct node_map llvm_params;
    struct use_map  llvm_values;

    struct scope scope;
    struct cfg cfg;
    struct schedule schedule;
};

SMALL_VEC_DEFINE(small_llvm_type_vec,  LLVMTypeRef, PRIVATE)
SMALL_VEC_DEFINE(small_llvm_value_vec, LLVMValueRef, PRIVATE)

static LLVMTypeRef convert_ty(struct llvm_codegen*, const struct fir_node*);
static LLVMValueRef gen_constant(struct llvm_codegen*, const struct fir_node*);

static inline bool is_convertible_ty(const struct fir_node* ty) {
    return ty->tag != FIR_MEM_TY && ty->tag != FIR_FRAME_TY;
}

static void convert_tup_ty_args(
    struct llvm_codegen* codegen,
    const struct fir_node* ty,
    struct small_llvm_type_vec* types)
{
    if (ty->tag == FIR_TUP_TY) {
        for (size_t i = 0; i < ty->op_count; ++i) {
            if (!is_convertible_ty(ty->ops[i]))
                continue;
            LLVMTypeRef type = convert_ty(codegen, ty->ops[i]);
            small_llvm_type_vec_push(types, &type);
        }
    } else if (is_convertible_ty(ty)) {
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
    convert_tup_ty_args(codegen, ty->ops[0], &param_types);
    convert_tup_ty_args(codegen, ty->ops[1], &ret_types);
    LLVMTypeRef ret_type = ret_types.elem_count == 0
        ? LLVMVoidTypeInContext(codegen->llvm_context)
        : convert_ty(codegen, ty->ops[1]);
    LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types.elems, param_types.elem_count, 0);
    small_llvm_type_vec_destroy(&ret_types);
    small_llvm_type_vec_destroy(&param_types);
    return func_type;
}

static LLVMTypeRef convert_tup_ty(struct llvm_codegen* codegen, const struct fir_node* ty) {
    struct small_llvm_type_vec arg_types;
    small_llvm_type_vec_init(&arg_types);
    convert_tup_ty_args(codegen, ty, &arg_types);
    LLVMTypeRef tup_type = LLVMStructTypeInContext(codegen->llvm_context, arg_types.elems, arg_types.elem_count, 0);
    small_llvm_type_vec_destroy(&arg_types);
    return tup_type;
}

static LLVMTypeRef convert_ty(struct llvm_codegen* codegen, const struct fir_node* ty) {
    assert(fir_node_is_ty(ty));
    void* const* type_ptr = node_map_find(&codegen->llvm_types, &ty);
    if (type_ptr)
        return *type_ptr;

    LLVMTypeRef llvm_type = NULL;
    switch (ty->tag) {
        case FIR_INT_TY:
            llvm_type = LLVMIntTypeInContext(codegen->llvm_context, ty->data.bitwidth);
            break;
        case FIR_FLOAT_TY:
            switch (ty->data.bitwidth) {
                case 16: llvm_type = LLVMHalfTypeInContext(codegen->llvm_context);   break;
                case 32: llvm_type = LLVMFloatTypeInContext(codegen->llvm_context);  break;
                case 64: llvm_type = LLVMDoubleTypeInContext(codegen->llvm_context); break;
                default:
                    assert(false && "unsupported floating-point bitwidth");
                    return NULL;
            }
            break;
        case FIR_PTR_TY:
            llvm_type = LLVMPointerTypeInContext(codegen->llvm_context, 0);
            break;
        case FIR_TUP_TY:
            llvm_type = convert_tup_ty(codegen, ty);
            break;
        default:
            assert(false && "unsupported type");
            return NULL;
    }
    assert(llvm_type);

    [[maybe_unused]] bool was_inserted = node_map_insert(&codegen->llvm_types, &ty, (void*[]) { llvm_type });
    assert(was_inserted);
    return llvm_type;
}

static bool is_constant(const struct fir_node* node) {
    return (node->props & FIR_PROP_INVARIANT) != 0 &&
        (node->tag == FIR_CONST || node->tag == FIR_TUP);
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
    assert(is_constant(node));
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
    if (is_constant(op))
        return gen_constant(codegen, op);
    if (op->tag == FIR_PARAM) {
        void*const* llvm_param_ptr = node_map_find(&codegen->llvm_params, &op);
        assert(llvm_param_ptr && "unknown function or basic-block parameter");
        return *llvm_param_ptr;
    }

    struct graph_node* block = find_op_block(codegen, use_block, op);
    assert(block && "could not find definition for operand");

    assert(false && "unimplemented"); // TODO
    return NULL;
}

static void gen_tup(
    struct llvm_codegen* codegen,
    const struct fir_node* tuple,
    void (*gen_arg)(struct llvm_codegen*, const struct fir_node*, size_t))
{
    if (tuple->ty->tag == FIR_TUP_TY) {
        for (size_t i = 0, j = 0; i < tuple->ty->op_count; ++i) {
            if (!is_convertible_ty(tuple->ty->ops[i]))
                continue;
            gen_arg(codegen, fir_ext_at(tuple, i), j++);
        }
    } else if (is_convertible_ty(tuple->ty)) {
        gen_arg(codegen, tuple, 0);
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

static void gen_block(struct llvm_codegen* codegen, const struct fir_node* block, bool is_entry_block) {
    char* block_name = fir_node_unique_name(block);
    LLVMBasicBlockRef llvm_block = LLVMAppendBasicBlockInContext(codegen->llvm_context, codegen->llvm_func, block_name); 
    free(block_name);

    node_map_insert(&codegen->llvm_blocks, &block, (void*[]) { llvm_block });

    LLVMPositionBuilderAtEnd(codegen->llvm_builder, llvm_block);
    if (!is_entry_block)
        gen_tup(codegen, fir_param(block), gen_phi);
}

static void gen_func(struct llvm_codegen* codegen, const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);

    char* func_name = fir_node_unique_name(func);
    codegen->llvm_func = LLVMAddFunction(
        codegen->llvm_module, func_name, convert_func_ty(codegen, func->ty));
    free(func_name);

    gen_tup(codegen, fir_param(func), gen_param);

    codegen->scope = scope_create(func);
    codegen->cfg = cfg_create(&codegen->scope);
    codegen->schedule = schedule_create(&codegen->cfg);

    VEC_FOREACH(struct graph_node*, block_ptr, codegen->cfg.post_order) {
        if (*block_ptr == codegen->cfg.graph.sink)
            continue;
        bool is_entry_block = *block_ptr == codegen->cfg.graph.source;
        gen_block(codegen, cfg_block_func(*block_ptr), is_entry_block);
    }

    schedule_destroy(&codegen->schedule);
    cfg_destroy(&codegen->cfg);
    scope_destroy(&codegen->scope);
}

static void llvm_codegen_destroy(struct fir_codegen* codegen) {
    struct llvm_codegen* llvm_codegen = (struct llvm_codegen*)codegen;
    node_map_destroy(&llvm_codegen->llvm_constants);
    node_map_destroy(&llvm_codegen->llvm_types);
    node_map_destroy(&llvm_codegen->llvm_blocks);
    node_map_destroy(&llvm_codegen->llvm_params);
    use_map_destroy(&llvm_codegen->llvm_values);
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

        use_map_clear(&llvm_codegen->llvm_values);
        node_map_clear(&llvm_codegen->llvm_blocks);
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
    llvm_codegen->llvm_blocks = node_map_create();
    llvm_codegen->llvm_params = node_map_create();
    llvm_codegen->llvm_values = use_map_create();
    return &llvm_codegen->base;
}
