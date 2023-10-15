#include "ast.h"
#include "types.h"

#include "support/datatypes.h"

#include <fir/node.h>
#include <fir/module.h>

#include <assert.h>

struct emitter {
    struct fir_mod* mod;
};

static const struct fir_node* prepend_mem(const struct fir_node* ty) {
    struct fir_mod* mod = fir_node_mod(ty);
    struct small_node_vec args_ty;
    small_node_vec_init(&args_ty);
    small_node_vec_push(&args_ty, (const struct fir_node*[]) { fir_mem_ty(mod) });
    if (ty->tag == FIR_TUP_TY) {
        for (size_t i = 0; i < ty->op_count; ++i)
            small_node_vec_push(&args_ty, &ty->ops[i]);
    } else {
        small_node_vec_push(&args_ty, &ty);
    }
    const struct fir_node* tup_ty = args_ty.elem_count != 1
        ? fir_tup_ty(mod, args_ty.elems, args_ty.elem_count)
        : args_ty.elems[0];
    small_node_vec_destroy(&args_ty);
    return tup_ty;
}

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
            return fir_func_ty(
                prepend_mem(convert_type(emitter, type->func_type.param_type)),
                prepend_mem(convert_type(emitter, type->func_type.ret_type)));
        default:
            assert(false && "invalid type");
            return NULL;
    }
}

static const struct fir_node* emit_func_decl(struct emitter* emitter, struct ast* ast) {
    assert(ast->tag == AST_FUNC_DECL);
    struct fir_node* func = fir_func(convert_type(emitter, ast->type));
    return func;
}

static const struct fir_node* emit(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUNC_DECL:
            return ast->node = emit_func_decl(emitter, ast);
        default:
            assert(false && "invalid AST node");
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
