#include "ast.h"
#include "types.h"

#include "support/datatypes.h"

#include <fir/node.h>
#include <fir/block.h>
#include <fir/module.h>

#include <assert.h>

struct emitter {
    struct fir_mod* mod;
    struct fir_block* block;
};

static const struct fir_node* emit(struct emitter*, struct ast*);
static void emit_pattern(struct emitter*, struct ast*, const struct fir_node*);

static const struct fir_node* convert_type(struct emitter* emitter, const struct type* type) {
    switch (type->tag) {
#define x(tag, ...) case TYPE_##tag:
        PRIM_TYPE_LIST(x)
#undef x
            if (type_is_int(type) || type->tag == TYPE_BOOL)
                return fir_int_ty(emitter->mod, type_bitwidth(type));
            if (type_is_float(type))
                return fir_float_ty(emitter->mod, type_bitwidth(type));
            assert(false && "invalid prim type");
            return NULL;
        case TYPE_FUNC:
            return fir_mem_func_ty(
                convert_type(emitter, type->func_type.param_type),
                convert_type(emitter, type->func_type.ret_type));
        case TYPE_TUPLE: {
            struct small_node_vec args_ty;
            small_node_vec_init(&args_ty);
            for (size_t i = 0; i < type->tuple_type.arg_count; ++i) {
                const struct fir_node* arg_ty = convert_type(emitter, type->tuple_type.arg_types[i]);
                small_node_vec_push(&args_ty, &arg_ty);
            }
            const struct fir_node* tup_ty = fir_tup_ty(emitter->mod, args_ty.elems, args_ty.elem_count);
            small_node_vec_destroy(&args_ty);
            return tup_ty;
        }
        default:
            assert(false && "invalid type");
            return NULL;
    }
}

static const struct fir_node* emit_func_decl(struct emitter* emitter, struct ast* ast) {
    assert(ast->tag == AST_FUNC_DECL);
    struct fir_node* func = fir_func(convert_type(emitter, ast->type));
    struct fir_block entry;
    const struct fir_node* param = fir_block_start(&entry, func);
    emitter->block = &entry;
    emit_pattern(emitter, ast->func_decl.param, param);
    const struct fir_node* ret_val = emit(emitter, ast->func_decl.body);
    fir_block_return(emitter->block, ret_val);
    emitter->block = NULL;
    return func;
}

static const struct fir_node* emit(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUNC_DECL:
            return ast->node = emit_func_decl(emitter, ast);
        case AST_IDENT_EXPR:
            return ast->node = ast->ident_expr.bound_to->node;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

static void emit_pattern(
    struct emitter* emitter,
    struct ast* pattern,
    const struct fir_node* val)
{
    switch (pattern->tag) {
        case AST_TUPLE_PATTERN: {
            size_t i = 0;
            for (struct ast* arg = pattern->tuple_pattern.args; arg; arg = arg->next)
                emit_pattern(emitter, arg, fir_ext_at(val, i++));
            break;
        }
        case AST_IDENT_PATTERN:
            pattern->node = val;
            break;
        default:
            assert(false && "invalid pattern");
            break;
    }
}

void ast_emit(struct ast* ast, struct fir_mod* mod) {
    assert(ast->tag == AST_PROGRAM);
    struct emitter emitter = {
        .mod = mod
    };
    for (struct ast* decl = ast->program.decls; decl; decl = decl->next)
        emit(&emitter, decl);
}
