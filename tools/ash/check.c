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
    log_error(type_checker->log, source_range, "cannot infer type for recursive symbol '%s'", name);
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

static const struct type* check_block(
    struct type_checker* type_checker,
    struct ast* block,
    const struct type* expected_type)
{
    struct ast* last_stmt = NULL;
    for (struct ast* stmt = block->block_expr.stmts; stmt; stmt = stmt->next) {
        if (stmt->next || block->block_expr.ends_with_semicolon)
            deref(type_checker, &stmt);
        else
            last_stmt = stmt;
    }
    if (last_stmt) {
        if (expected_type)
            return coerce(type_checker, &last_stmt, expected_type);
        return deref(type_checker, &last_stmt);
    }
    return type_unit(type_checker->type_set);
}

static const struct type* check(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    switch (ast->tag) {
        case AST_BLOCK_EXPR:
            return ast->type = check_block(type_checker, ast->block_expr.stmts, expected_type);
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

static const struct type* infer_tuple(struct type_checker* type_checker, struct ast* args) {
    struct small_type_vec arg_types;
    small_type_vec_init(&arg_types);
    for (struct ast* arg = args; arg; arg = arg->next) {
        const struct type* arg_type = infer(type_checker, arg);
        small_type_vec_push(&arg_types, &arg_type);
    }
    const struct type* type = type_tuple(type_checker->type_set, arg_types.elems, arg_types.elem_count);
    small_type_vec_destroy(&arg_types);
    return type;
}

static const struct type* infer_record(struct type_checker* type_checker, struct ast* fields) {
    struct small_type_vec field_types;
    struct small_string_vec field_names;
    struct str_view_set fields_seen = str_view_set_create();
    small_type_vec_init(&field_types);
    small_string_vec_init(&field_names);
    for (struct ast* field = fields; field; field = field->next) {
        const struct type* field_type = infer(type_checker, field);
        const char* field_name = field->field_type.name;
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

static const struct type* infer(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_PRIM_TYPE:
            return ast->type = type_prim(type_checker->type_set, (enum type_tag)ast->prim_type.tag);
        case AST_LITERAL:
            return ast->type = infer_literal(type_checker, &ast->literal);
        case AST_FUNC_DECL:
            return ast->type = infer_func_decl(type_checker, ast);
        case AST_IDENT_PATTERN:
            if (ast->ident_pattern.type)
                return ast->type = infer(type_checker, ast->ident_pattern.type);
            return ast->type = cannot_infer(type_checker, &ast->source_range, ast->ident_pattern.name);
        case AST_IDENT_EXPR:
            return ast->type = infer(type_checker, ast->ident_expr.bound_to);
        case AST_TUPLE_TYPE:
        case AST_TUPLE_EXPR:
        case AST_TUPLE_PATTERN:
            return ast->type = infer_tuple(type_checker, ast->tuple_type.args);
        case AST_FIELD_TYPE:
        case AST_FIELD_PATTERN:
            return ast->type = infer(type_checker, ast->field_type.arg);
        case AST_FIELD_EXPR:
            return ast->type = deref(type_checker, &ast->field_type.arg);
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            return ast->type = infer_record(type_checker, ast->record_type.fields);
        case AST_BLOCK_EXPR:
            return ast->type = check_block(type_checker, ast, NULL);
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
