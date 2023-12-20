#include "ast.h"
#include "types.h"

#include "support/hash.h"
#include "support/log.h"
#include "support/datatypes.h"
#include "support/mem_pool.h"

#include <stdint.h>
#include <assert.h>

struct type_checker {
    struct ast_set visited_decls;
    struct type_set* type_set;
    struct mem_pool* mem_pool;
    struct log* log;
};

static const struct type* deref(struct type_checker*, struct ast**);
static const struct type* coerce(struct type_checker*, struct ast**, const struct type*);
static const struct type* infer(struct type_checker*, struct ast*);
static const struct type* check(struct type_checker*, struct ast*, const struct type*);

static const struct type* cannot_infer(
    struct type_checker* type_checker,
    const struct fir_source_range* source_range,
    const char* name)
{
    log_error(type_checker->log, source_range, "cannot infer type for symbol '%s'", name);
    return type_top(type_checker->type_set);
}

static const struct type* expect_type(
    struct type_checker* type_checker,
    const struct fir_source_range* source_range,
    const struct type* type,
    const struct type* expected_type)
{
    if (!type_is_subtype(type, expected_type)) {
        char* type_str = type_to_string(type);
        char* expected_type_str = type_to_string(expected_type);
        log_error(type_checker->log, source_range, "expected type '%s', but got '%s'",
            expected_type_str, type_str);
        free(type_str);
        free(expected_type_str);
        return type_top(type_checker->type_set);
    }
    return type;
}

static const struct type* implicit_cast(
    struct type_checker* type_checker,
    struct ast** expr,
    const struct type* type)
{
    assert(type_is_subtype((*expr)->type, type));
    struct ast* cast_expr = MEM_POOL_ALLOC(*type_checker->mem_pool, struct ast);
    cast_expr->tag = AST_IMPLICIT_CAST_EXPR;
    cast_expr->implicit_cast_expr.expr = *expr;
    cast_expr->type = type;
    cast_expr->next = (*expr)->next;
    (*expr)->next = NULL;
    *expr = cast_expr;
    return type;
}

static const struct type* deref(
    struct type_checker* type_checker,
    struct ast** expr)
{
    const struct type* type = infer(type_checker, *expr); 
    if (type->tag == TYPE_REF)
        return implicit_cast(type_checker, expr, type->ref_type.pointee_type);
    return type;
}

static const struct type* coerce(
    struct type_checker* type_checker,
    struct ast** expr,
    const struct type* type)
{
    const struct type* expr_type = check(type_checker, *expr, type); 
    if (expr_type != type && type_is_subtype(expr_type, type))
        return implicit_cast(type_checker, expr, type);
    return expr_type;
}

static const struct type* check_block_expr(
    struct type_checker* type_checker,
    struct ast* block_expr,
    const struct type* expected_type)
{
    struct ast* last_stmt = NULL;
    for (struct ast* stmt = block_expr->block_expr.stmts; stmt; stmt = stmt->next) {
        if (stmt->next || block_expr->block_expr.ends_with_semicolon)
            deref(type_checker, &stmt);
        else
            last_stmt = stmt;
    }
    if (!last_stmt)
        return type_unit(type_checker->type_set);
    return expected_type
        ? check(type_checker, last_stmt, expected_type)
        : infer(type_checker, last_stmt);
}

static const struct type* check_tuple(
    struct type_checker* type_checker,
    struct ast* args,
    const struct type* expected_type)
{
    struct small_type_vec arg_types;
    small_type_vec_init(&arg_types);
    for (struct ast* arg = args; arg; arg = arg->next) {
        const struct type* arg_type = NULL;
        if (expected_type &&
            expected_type->tag == TYPE_TUPLE &&
            arg_types.elem_count < expected_type->tuple_type.arg_count) {
            arg_type = check(type_checker, arg, expected_type->tuple_type.arg_types[arg_types.elem_count]);
        } else {
            arg_type = infer(type_checker, arg);
        }
        small_type_vec_push(&arg_types, &arg_type);
    }
    const struct type* type = type_tuple(type_checker->type_set, arg_types.elems, arg_types.elem_count);
    small_type_vec_destroy(&arg_types);
    return type;
}

static const struct type* check_record(
    struct type_checker* type_checker,
    struct ast* fields,
    const struct type* expected_type)
{
    struct small_type_vec field_types;
    struct small_string_vec field_names;
    struct str_view_set fields_seen = str_view_set_create();
    small_type_vec_init(&field_types);
    small_string_vec_init(&field_names);
    for (struct ast* field = fields; field; field = field->next) {
        size_t field_index = SIZE_MAX;
        const char* field_name = field->field_type.name;
        if (expected_type && expected_type->tag == TYPE_RECORD)
            field_index = type_find_field(expected_type, field_name);
        const struct type* field_type = field_index < SIZE_MAX
            ? check(type_checker, field, expected_type->record_type.field_types[field_index])
            : infer(type_checker, field);
        small_type_vec_push(&field_types, &field_type);
        small_string_vec_push(&field_names, (char**)&field_name);
        if (!str_view_set_insert(&fields_seen, &STR_VIEW(field_name))) {
            log_error(type_checker->log, &field->source_range, "field '%s' mentioned more than once", field_name);
            return type_top(type_checker->type_set);
        }
    }

    const struct type* type = type_record(
        type_checker->type_set,
        field_types.elems,
        (const char* const*)field_names.elems,
        field_types.elem_count);

    small_type_vec_destroy(&field_types);
    small_string_vec_destroy(&field_names);
    str_view_set_destroy(&fields_seen);
    return type;
}

static const struct type* check_if_expr(
    struct type_checker* type_checker,
    struct ast* if_expr,
    const struct type* expected_type)
{
    coerce(type_checker, &if_expr->if_expr.cond, type_bool(type_checker->type_set));
    if (expected_type) {
        check(type_checker, if_expr->if_expr.then_block, expected_type);
        if (if_expr->if_expr.else_block)
            check(type_checker, if_expr->if_expr.else_block, expected_type);
        return expected_type;
    } else {
        const struct type* then_type = infer(type_checker, if_expr->if_expr.then_block);
        if (if_expr->if_expr.else_block)
            check(type_checker, if_expr->if_expr.else_block, then_type);
        return then_type;
    }
}

static const struct type* check(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    assert(expected_type);
    switch (ast->tag) {
        case AST_BLOCK_EXPR:
            return ast->type = check_block_expr(type_checker, ast, expected_type);
        case AST_IF_EXPR:
            return ast->type = check_if_expr(type_checker, ast, expected_type);
        case AST_TUPLE_EXPR:
            return ast->type = check_tuple(type_checker, ast->block_expr.stmts, expected_type);
        case AST_RECORD_EXPR:
            return ast->type = check_record(type_checker, ast->record_type.fields, expected_type);
        case AST_IDENT_PATTERN:
            return ast->type = expected_type;
        default:
            return ast->type = expect_type(type_checker, &ast->source_range, infer(type_checker, ast), expected_type);
    }
}

static const struct type* infer_func_decl(struct type_checker* type_checker, struct ast* func_decl) {
    if (!ast_set_insert(&type_checker->visited_decls, &func_decl))
        return cannot_infer(type_checker, &func_decl->source_range, func_decl->func_decl.name);
    const struct type* param_type = infer(type_checker, func_decl->func_decl.param);
    const struct type* ret_type = NULL;
    if (func_decl->func_decl.ret_type) {
        // Set type before entering the function, in order to allow typing recursive
        // functions with an explicit return type annotation.
        ret_type = infer(type_checker, func_decl->func_decl.ret_type);
        func_decl->type = type_func(type_checker->type_set, param_type, ret_type);
        coerce(type_checker, &func_decl->func_decl.body, ret_type);
    } else {
        ret_type = deref(type_checker, &func_decl->func_decl.body);
        func_decl->type = type_func(type_checker->type_set, param_type, ret_type);
    }
    return func_decl->type;
}

static const struct type* infer_literal(struct type_checker* type_checker, struct literal* literal) {
    switch (literal->tag) {
        case LITERAL_INT:   return type_prim(type_checker->type_set, TYPE_I32);
        case LITERAL_FLOAT: return type_prim(type_checker->type_set, TYPE_F32);
        case LITERAL_BOOL:  return type_prim(type_checker->type_set, TYPE_BOOL);
        default:
            assert(false && "invalid literal");
            return NULL;
    }
}

static void check_binding(
    struct type_checker* type_checker,
    struct ast* pattern,
    struct ast* expr)
{
    if (pattern->tag == AST_IDENT_PATTERN && pattern->type) {
        coerce(type_checker, &expr, infer(type_checker, pattern));
    } else {
        check(type_checker, pattern, infer(type_checker, expr));
    }
}

static const struct type* infer_const_or_var_decl(struct type_checker* type_checker, struct ast* decl) {
    if (decl->const_decl.init)
        check_binding(type_checker, decl->const_decl.pattern, decl->const_decl.init);
    else
        infer(type_checker, decl->const_decl.pattern);
    return type_unit(type_checker->type_set);
}

static const struct type* infer(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_PRIM_TYPE:
            return ast->type = type_prim(type_checker->type_set, (enum type_tag)ast->prim_type.tag);
        case AST_LITERAL:
            return ast->type = infer_literal(type_checker, &ast->literal);
        case AST_FUNC_DECL:
            return ast->type = infer_func_decl(type_checker, ast);
        case AST_CONST_DECL:
            return ast->type = infer_const_or_var_decl(type_checker, ast);
        case AST_IDENT_PATTERN:
            if (ast->ident_pattern.type)
                return ast->type = infer(type_checker, ast->ident_pattern.type);
            return ast->type = cannot_infer(type_checker, &ast->source_range, ast->ident_pattern.name);
        case AST_IDENT_EXPR:
            assert(ast->ident_expr.bound_to);
            if (ast->ident_expr.bound_to->type)
                return ast->type = ast->ident_expr.bound_to->type;
            return ast->type = infer(type_checker, ast->ident_expr.bound_to);
        case AST_TUPLE_TYPE:
        case AST_TUPLE_EXPR:
        case AST_TUPLE_PATTERN:
            return ast->type = check_tuple(type_checker, ast->tuple_type.args, NULL);
        case AST_FIELD_TYPE:
        case AST_FIELD_PATTERN:
            return ast->type = infer(type_checker, ast->field_type.arg);
        case AST_FIELD_EXPR:
            return ast->type = deref(type_checker, &ast->field_type.arg);
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            return ast->type = check_record(type_checker, ast->record_type.fields, NULL);
        case AST_BLOCK_EXPR:
            return ast->type = check_block_expr(type_checker, ast, NULL);
        case AST_IF_EXPR:
            return ast->type = check_if_expr(type_checker, ast, NULL);
        default:
            assert(false && "invalid AST node");
            return ast->type = type_top(type_checker->type_set);
    }
}

void ast_check(
    struct ast* ast,
    struct mem_pool* mem_pool,
    struct type_set* type_set,
    struct log* log)
{
    assert(ast->tag == AST_PROGRAM);
    struct type_checker type_checker = {
        .visited_decls = ast_set_create(),
        .mem_pool = mem_pool,
        .type_set = type_set,
        .log = log
    };

    for (struct ast* decl = ast->program.decls; decl; decl = decl->next)
        infer(&type_checker, decl);

    ast_set_destroy(&type_checker.visited_decls);
}
