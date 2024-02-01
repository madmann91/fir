#include "ast.h"
#include "types.h"

#include "support/datatypes.h"

#include <fir/node.h>
#include <fir/block.h>
#include <fir/module.h>

#include <assert.h>

struct emitter {
    struct fir_mod* mod;
    struct fir_block block;
};

static const struct fir_node* emit(struct emitter*, struct ast*);

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
            if (pattern->ident_pattern.is_var) {
                const struct fir_node* alloc = fir_block_alloc(&emitter->block, val->ty);
                fir_block_store(&emitter->block, alloc, val, FIR_MEM_NON_NULL);
                pattern->node = alloc;
            } else {
                pattern->node = val;
            }
            break;
        default:
            assert(false && "invalid pattern");
            break;
    }
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
            return fir_mem_func_ty(
                convert_type(emitter, type->func_type.param_type),
                convert_type(emitter, type->func_type.ret_type));
        case TYPE_RECORD:
        case TYPE_TUPLE: {
            struct small_node_vec args_ty;
            small_node_vec_init(&args_ty);
            const size_t arg_count = type->tag == TYPE_TUPLE
                ? type->tuple_type.arg_count
                : type->record_type.field_count;
            const struct type* const* arg_types = type->tag == TYPE_TUPLE
                ? type->tuple_type.arg_types
                : type->record_type.field_types;
            for (size_t i = 0; i < arg_count; ++i) {
                const struct fir_node* arg_ty = convert_type(emitter, arg_types[i]);
                small_node_vec_push(&args_ty, &arg_ty);
            }
            const struct fir_node* tup_ty = fir_tup_ty(emitter->mod, args_ty.elems, args_ty.elem_count);
            small_node_vec_destroy(&args_ty);
            return tup_ty;
        }
        case TYPE_REF:
        case TYPE_PTR:
            return fir_ptr_ty(emitter->mod);
        default:
            assert(false && "invalid type");
            return NULL;
    }
}

static const struct fir_node* emit_func_decl(struct emitter* emitter, struct ast* func_decl) {
    assert(func_decl->tag == AST_FUNC_DECL);
    struct fir_node* func = fir_func(convert_type(emitter, func_decl->type));
    const struct fir_node* param = fir_block_start(&emitter->block, func);
    emit_pattern(emitter, func_decl->func_decl.param, param);
    const struct fir_node* ret_val = emit(emitter, func_decl->func_decl.body);
    fir_block_return(&emitter->block, ret_val);
    fir_node_make_external(func);
    return func;
}

static const struct fir_node* emit_tuple_expr(struct emitter* emitter, struct ast* tuple_expr) {
    struct small_node_vec args;
    small_node_vec_init(&args);
    for (struct ast* arg = tuple_expr->tuple_expr.args; arg; arg = arg->next)
        small_node_vec_push(&args, (const struct fir_node*[]) { emit(emitter, arg) });
    const struct fir_node* tup = fir_tup(emitter->mod, args.elems, args.elem_count);
    small_node_vec_destroy(&args);
    return tup;
}

static const struct fir_node* emit_record_expr(struct emitter* emitter, struct ast* record_expr) {
    const struct type* record_type = record_expr->type;
    assert(record_type->tag == TYPE_RECORD);

    struct small_node_vec args;
    small_node_vec_init(&args);
    small_node_vec_resize(&args, record_type->record_type.field_count);
    for (struct ast* field = record_expr->record_expr.fields; field; field = field->next) {
        size_t index = type_find_field(record_type, field->field_expr.name);
        assert(index < record_type->record_type.field_count);
        args.elems[index] = emit(emitter, field);
    }

    const struct fir_node* tup = fir_tup(emitter->mod, args.elems, args.elem_count);
    small_node_vec_destroy(&args);
    return tup;
}

static const struct fir_node* emit_literal(
    struct emitter* emitter,
    struct literal* literal,
    const struct type* type)
{
    if (literal->tag == LITERAL_BOOL)
        return fir_int_const(fir_bool_ty(emitter->mod), literal->bool_val ? 1 : 0);
    else if (literal->tag == LITERAL_INT)
        return fir_int_const(convert_type(emitter, type), literal->int_val);
    else if (literal->tag == LITERAL_FLOAT)
        return fir_float_const(convert_type(emitter, type), literal->float_val);
    assert(false && "invalid literal");
    return NULL;
}

static const struct fir_node* emit_block_expr(struct emitter* emitter, struct ast* block_expr) {
    const struct fir_node* last_val = NULL;
    for (struct ast* stmt = block_expr->block_expr.stmts; stmt; stmt = stmt->next) {
        last_val = emit(emitter, stmt);
        if (stmt->next || block_expr->block_expr.ends_with_semicolon)
            last_val = NULL;
    }
    return last_val ? last_val : fir_unit(emitter->mod);
}

static const struct fir_node* emit_if_expr(struct emitter* emitter, struct ast* if_expr) {
    const struct fir_node* cond = emit(emitter, if_expr->if_expr.cond);
    const struct fir_node* alloc_ty = NULL;
    const struct fir_node* alloc = NULL;

    if (!type_is_unit(if_expr->type)) {
        alloc_ty = convert_type(emitter, if_expr->type);
        alloc = fir_block_alloc(&emitter->block, alloc_ty);
    }

    struct fir_block then_block;
    struct fir_block else_block;
    struct fir_block merge_block = fir_block_merge(emitter->block.func);
    fir_block_branch(&emitter->block, cond, &then_block, &else_block, &merge_block);

    emitter->block = then_block;
    const struct fir_node* then_val = emit(emitter, if_expr->if_expr.then_block);
    if (alloc)
        fir_block_store(&emitter->block, alloc, then_val, FIR_MEM_NON_NULL);
    fir_block_jump(&emitter->block, &merge_block);

    emitter->block = else_block;
    if (if_expr->if_expr.else_block) {
        const struct fir_node* else_val = emit(emitter, if_expr->if_expr.else_block);
        if (alloc)
            fir_block_store(&emitter->block, alloc, else_val, FIR_MEM_NON_NULL);
    }
    fir_block_jump(&emitter->block, &merge_block);

    emitter->block = merge_block;
    return alloc ? fir_block_load(&emitter->block, alloc, alloc_ty, FIR_MEM_NON_NULL) : fir_unit(emitter->mod);
}

static const struct fir_node* emit_const_or_var_decl(struct emitter* emitter, struct ast* decl) {
    const struct fir_node* val = decl->const_decl.init
        ? emit(emitter, decl->const_decl.init)
        : fir_bot(convert_type(emitter, decl->const_decl.pattern->type));
    emit_pattern(emitter, decl->const_decl.pattern, val);
    return fir_unit(emitter->mod);
}

static const struct fir_node* emit_implicit_cast(
    struct emitter* emitter,
    const struct fir_node* val,
    const struct type* source_type,
    const struct type* target_type)
{
    if (source_type->tag == TYPE_REF && target_type->tag != TYPE_REF) {
        const struct fir_node* load_ty = convert_type(emitter, source_type->ref_type.pointee_type);
        const struct fir_node* loaded_val = fir_block_load(&emitter->block, val, load_ty, 0);
        return emit_implicit_cast(emitter, loaded_val, source_type->ref_type.pointee_type, target_type);
    }
    assert(source_type == target_type);
    return val;
}

static const struct fir_node* emit_implicit_cast_expr(struct emitter* emitter, struct ast* cast_expr) {
    const struct fir_node* arg = emit(emitter, cast_expr->implicit_cast_expr.expr);
    return emit_implicit_cast(emitter, arg, cast_expr->implicit_cast_expr.expr->type, cast_expr->type);
}

static const struct fir_node* emit_unary_expr(struct emitter* emitter, struct ast* unary_expr) {
    assert(false && "unimplemented");
    (void)emitter;
    (void)unary_expr;
    return NULL;
}

static const struct fir_node* emit_binary_expr(struct emitter* emitter, struct ast* binary_expr) {
    if (binary_expr->binary_expr.tag == BINARY_EXPR_ASSIGN) {
        assert(binary_expr->binary_expr.left->type->tag == TYPE_REF);
        const struct fir_node* ptr = emit(emitter, binary_expr->binary_expr.left);
        const struct fir_node* val = emit(emitter, binary_expr->binary_expr.right);
        fir_block_store(&emitter->block, ptr, val, 0);
        return fir_unit(emitter->mod);
    }
    assert(false && "unimplemented");
    return NULL;
}

static const struct fir_node* emit_while_loop(struct emitter* emitter, struct ast* while_loop) {
    struct fir_block continue_block;
    struct fir_block break_block = fir_block_merge(emitter->block.func);
    fir_block_loop(&emitter->block, &continue_block, &break_block);

    emitter->block = continue_block;
    const struct fir_node* cond = emit(emitter, while_loop->while_loop.cond);
    struct fir_block cond_true_block;
    struct fir_block cond_false_block;
    fir_block_branch(&emitter->block, cond, &cond_true_block, &cond_false_block, &break_block);

    emitter->block = cond_true_block;
    emit(emitter, while_loop->while_loop.body);
    fir_block_jump(&emitter->block, &continue_block);

    emitter->block = cond_false_block;
    fir_block_jump(&emitter->block, &break_block);

    emitter->block = break_block;
    return fir_unit(emitter->mod);
}

static const struct fir_node* emit(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_LITERAL:
            return ast->node = emit_literal(emitter, &ast->literal, ast->type);
        case AST_FUNC_DECL:
            return ast->node = emit_func_decl(emitter, ast);
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            return ast->node = emit_const_or_var_decl(emitter, ast);
        case AST_IDENT_EXPR:
            return ast->node = ast->ident_expr.bound_to->node;
        case AST_TUPLE_EXPR:
            return ast->node = emit_tuple_expr(emitter, ast);
        case AST_RECORD_EXPR:
            return ast->node = emit_record_expr(emitter, ast);
        case AST_BLOCK_EXPR:
            return ast->node = emit_block_expr(emitter, ast);
        case AST_IF_EXPR:
            return ast->node = emit_if_expr(emitter, ast);
        case AST_FIELD_EXPR:
            return ast->node = emit(emitter, ast->field_expr.arg);
        case AST_IMPLICIT_CAST_EXPR:
            return ast->node = emit_implicit_cast_expr(emitter, ast);
        case AST_UNARY_EXPR:
            return ast->node = emit_unary_expr(emitter, ast);
        case AST_BINARY_EXPR:
            return ast->node = emit_binary_expr(emitter, ast);
        case AST_WHILE_LOOP:
            return ast->node = emit_while_loop(emitter, ast);
        default:
            assert(false && "invalid AST node");
            return NULL;
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
