#include "ast.h"
#include "types.h"
#include "datatypes.h"

#include <overture/hash.h>
#include <overture/log.h>
#include <overture/mem_pool.h>

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

static void cannot_infer(
    struct type_checker* type_checker,
    const struct source_range* source_range,
    const char* name)
{
    log_error(type_checker->log, source_range, "cannot infer type for symbol '%s'", name);
}

static void invalid_type(
    struct type_checker* type_checker,
    const struct source_range* source_range,
    const char* type_name,
    const struct type* type)
{
    if (!type->contains_top) {
        char* type_str = type_to_string(type);
        log_error(type_checker->log, source_range, "expected %s type, but got type '%s'",
            type_name, type_str);
        free(type_str);
    }
}

static void invalid_cast(
    struct type_checker* type_checker,
    const struct source_range* source_range,
    const struct type* source_type,
    const struct type* dest_type)
{
    if (!source_type->contains_top && !dest_type->contains_top) {
        char* source_type_str = type_to_string(source_type);
        char* dest_type_str = type_to_string(dest_type);
        log_error(type_checker->log, source_range, "cannot cast type '%s' into type '%s'", source_type_str, dest_type_str);
        free(source_type_str);
        free(dest_type_str);
    }
}

static const struct type* expect_type(
    struct type_checker* type_checker,
    const struct source_range* source_range,
    const struct type* type,
    const struct type* expected_type)
{
    if (!type_is_subtype(type, expected_type) && !expected_type->contains_top && !type->contains_top) {
        char* type_str = type_to_string(type);
        char* expected_type_str = type_to_string(expected_type);
        log_error(type_checker->log, source_range, "expected type '%s', but got type '%s'",
            expected_type_str, type_str);
        free(type_str);
        free(expected_type_str);
        return type_top(type_checker->type_set);
    }
    return type;
}

static void expect_mutable(
    struct type_checker* type_checker,
    const struct source_range* source_range,
    const struct type* type)
{
    if ((type->tag != TYPE_REF || type->ref_type.is_const) && !type->contains_top) {
        char* type_str = type_to_string(type);
        log_error(type_checker->log, source_range,
            "expected mutable expression, but got expression of type '%s'", type_str);
        free(type_str);
    }
}

static void expect_irrefutable_pattern(
    struct type_checker* type_checker,
    const char* context,
    const struct ast* pattern)
{
    if (!ast_is_irrefutable_pattern(pattern))
        log_error(type_checker->log, &pattern->source_range, "invalid %s pattern", context);
}

static const struct type* implicit_cast(
    struct type_checker* type_checker,
    struct ast** expr,
    const struct type* type)
{
    assert(type_is_subtype((*expr)->type, type));
    struct ast* cast_expr = MEM_POOL_ALLOC(*type_checker->mem_pool, struct ast);
    cast_expr->tag = AST_CAST_EXPR;
    cast_expr->cast_expr.arg = *expr;
    cast_expr->cast_expr.type = NULL;
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
    const struct type* type = (*expr)->type ? (*expr)->type : infer(type_checker, *expr);
    if (type->tag == TYPE_REF)
        return implicit_cast(type_checker, expr, type->ref_type.pointee_type);
    return type;
}

static const struct type* coerce(
    struct type_checker* type_checker,
    struct ast** expr,
    const struct type* expected_type)
{
    const struct type* expr_type = (*expr)->type ? (*expr)->type : check(type_checker, *expr, expected_type);
    if (expr_type != expected_type) {
        if (type_is_subtype(expr_type, expected_type))
            return implicit_cast(type_checker, expr, expected_type);
        return expect_type(type_checker, &(*expr)->source_range, expr_type, expected_type);
    }
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
    if (!last_stmt) {
        const struct type* block_type = type_unit(type_checker->type_set);
        if (expected_type)
            return expect_type(type_checker, &block_expr->source_range, block_type, expected_type);
        return block_type;
    }
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

static void join_exprs(
    struct type_checker* type_checker,
    struct ast** left_expr,
    struct ast** right_expr)
{
    assert((*left_expr)->type);
    assert((*right_expr)->type);
    if ((*left_expr)->type == (*right_expr)->type)
        return;
    if (type_is_subtype((*left_expr)->type, (*right_expr)->type))
        coerce(type_checker, left_expr, (*right_expr)->type);
    else
        coerce(type_checker, right_expr, (*left_expr)->type);
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
        if (if_expr->if_expr.else_block) {
            infer(type_checker, if_expr->if_expr.else_block);
            join_exprs(type_checker, &if_expr->if_expr.then_block, &if_expr->if_expr.else_block);
        }
        return then_type;
    }
}

static const struct type* check_unary_expr(
    struct type_checker* type_checker,
    struct ast* unary_expr,
    const struct type* expected_type)
{
    const struct type* arg_type = expected_type
        ? check(type_checker, unary_expr->unary_expr.arg, expected_type)
        : infer(type_checker, unary_expr->unary_expr.arg);
    bool is_inc_or_dec = unary_expr_tag_is_inc_or_dec(unary_expr->unary_expr.tag);
    if (is_inc_or_dec)
        expect_mutable(type_checker, &unary_expr->unary_expr.arg->source_range, arg_type);
    arg_type = type_remove_ref(arg_type);

    switch (unary_expr->unary_expr.tag) {
        case UNARY_EXPR_PLUS:
        case UNARY_EXPR_NEG:
            if (!type_is_signed_int(arg_type) && !type_is_float(arg_type)) {
                invalid_type(type_checker, &unary_expr->unary_expr.arg->source_range, "integer or floating-point", arg_type);
                return type_top(type_checker->type_set);
            }
            break;
        case UNARY_EXPR_PRE_INC:
        case UNARY_EXPR_PRE_DEC:
        case UNARY_EXPR_POST_INC:
        case UNARY_EXPR_POST_DEC:
            if (!type_is_int(arg_type)) {
                invalid_type(type_checker, &unary_expr->unary_expr.arg->source_range, "integer", arg_type);
                return type_top(type_checker->type_set);
            }
            break;
        case UNARY_EXPR_NOT:
            if (!type_is_int(arg_type) && arg_type->tag != TYPE_BOOL) {
                invalid_type(type_checker, &unary_expr->unary_expr.arg->source_range, "integer or boolean", arg_type);
                return type_top(type_checker->type_set);
            }
            break;
        default:
            assert(false && "invalid unary operation");
            return type_top(type_checker->type_set);
    }

    if (!is_inc_or_dec)
        coerce(type_checker, &unary_expr->unary_expr.arg, arg_type);
    return arg_type;
}

static const struct type* check_binary_expr(
    struct type_checker* type_checker,
    struct ast* binary_expr,
    const struct type* expected_type)
{
    if (binary_expr->binary_expr.tag == BINARY_EXPR_ASSIGN) {
        const struct type* left_type = infer(type_checker, binary_expr->binary_expr.left);
        if (left_type->tag == TYPE_REF)
            coerce(type_checker, &binary_expr->binary_expr.right, left_type->ref_type.pointee_type);
        else {
            invalid_type(type_checker, &binary_expr->binary_expr.left->source_range, "reference", left_type);
            deref(type_checker, &binary_expr->binary_expr.right);
        }
        return type_unit(type_checker->type_set);
    }

    const struct type* left_type  = NULL;
    const struct type* right_type = NULL;
    if (!binary_expr_tag_is_cmp(binary_expr->binary_expr.tag) && expected_type) {
        left_type = check(type_checker, binary_expr->binary_expr.left, expected_type);
        right_type = coerce(type_checker, &binary_expr->binary_expr.right, expected_type);
    } else {
        left_type = infer(type_checker, binary_expr->binary_expr.left);
        right_type = deref(type_checker, &binary_expr->binary_expr.right);
    }

    bool is_assign = binary_expr_tag_is_assign(binary_expr->binary_expr.tag);
    if (is_assign)
        expect_mutable(type_checker, &binary_expr->binary_expr.left->source_range, left_type);
    left_type = type_remove_ref(left_type);

    const struct type* joined_type = type_is_subtype(right_type, left_type) || is_assign ? left_type : right_type;
    const struct type* result_type = joined_type;
    switch (binary_expr_tag_remove_assign(binary_expr->binary_expr.tag)) {
        case BINARY_EXPR_MUL:
        case BINARY_EXPR_DIV:
        case BINARY_EXPR_REM:
        case BINARY_EXPR_ADD:
        case BINARY_EXPR_SUB:
            if (!type_is_int(joined_type) && !type_is_float(joined_type)) {
                invalid_type(type_checker, &binary_expr->source_range, "integer or floating-point", joined_type);
                return type_top(type_checker->type_set);
            }
            break;
        case BINARY_EXPR_LSHIFT:
        case BINARY_EXPR_RSHIFT:
            if (!type_is_int(joined_type)) {
                invalid_type(type_checker, &binary_expr->source_range, "integer", joined_type);
                return type_top(type_checker->type_set);
            }
            break;
        case BINARY_EXPR_CMP_GT:
        case BINARY_EXPR_CMP_LT:
        case BINARY_EXPR_CMP_GE:
        case BINARY_EXPR_CMP_LE:
        case BINARY_EXPR_CMP_NE:
        case BINARY_EXPR_CMP_EQ:
            result_type = type_bool(type_checker->type_set);
            break;
        case BINARY_EXPR_AND:
        case BINARY_EXPR_XOR:
        case BINARY_EXPR_OR:
            if (!type_is_int(joined_type) && joined_type->tag != TYPE_BOOL) {
                invalid_type(type_checker, &binary_expr->source_range, "integer or boolean", joined_type);
                return type_top(type_checker->type_set);
            }
            break;
        case BINARY_EXPR_LOGIC_AND:
        case BINARY_EXPR_LOGIC_OR:
            if (joined_type->tag != TYPE_BOOL) {
                invalid_type(type_checker, &binary_expr->source_range, "boolean", joined_type);
                return type_top(type_checker->type_set);
            }
            break;
        default:
            assert(false && "invalid binary expression");
            return type_top(type_checker->type_set);
    }

    coerce(type_checker, &binary_expr->binary_expr.right, joined_type);
    if (is_assign)
        return type_unit(type_checker->type_set);

    coerce(type_checker, &binary_expr->binary_expr.left, joined_type);
    return result_type;
}

static const struct type* check_call_expr(
    struct type_checker* type_checker,
    struct ast* call_expr,
    const struct type* expected_type)
{
    const struct type* callee_type = deref(type_checker, &call_expr->call_expr.callee);
    if (callee_type->tag != TYPE_FUNC) {
        invalid_type(type_checker, &call_expr->call_expr.callee->source_range, "function", callee_type);
        deref(type_checker, &call_expr->call_expr.arg);
        return type_top(type_checker->type_set);
    }

    const struct type* param_type = callee_type->func_type.param_type;
    const struct type* ret_type = callee_type->func_type.ret_type;
    coerce(type_checker, &call_expr->call_expr.arg, param_type);

    if (expected_type)
        expect_type(type_checker, &call_expr->source_range, ret_type, expected_type);
    return ret_type;
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
        case AST_CALL_EXPR:
            return ast->type = check_call_expr(type_checker, ast, expected_type);
        case AST_TUPLE_PATTERN:
        case AST_TUPLE_EXPR:
            return ast->type = check_tuple(type_checker, ast->block_expr.stmts, expected_type);
        case AST_RECORD_EXPR:
            return ast->type = check_record(type_checker, ast->record_type.fields, expected_type);
        case AST_UNARY_EXPR:
            return ast->type = check_unary_expr(type_checker, ast, expected_type);
        case AST_BINARY_EXPR:
            return ast->type = check_binary_expr(type_checker, ast, expected_type);
        case AST_IDENT_PATTERN: {
            if (ast->ident_pattern.type) {
                const struct type* ident_type = infer(type_checker, ast->ident_pattern.type);
                expect_type(type_checker, &ast->source_range, expected_type, ident_type);
                ast->type = ident_type;
            } else {
                ast->type = expected_type;
            }
            if (ast->ident_pattern.is_var)
                ast->type = type_ref(type_checker->type_set, ast->type, false);
            return ast->type;
        }
        default:
            return ast->type = expect_type(type_checker, &ast->source_range, infer(type_checker, ast), expected_type);
    }
}

static const struct type* infer_func_decl(struct type_checker* type_checker, struct ast* func_decl) {
    if (!ast_set_insert(&type_checker->visited_decls, &func_decl)) {
        cannot_infer(type_checker, &func_decl->source_range, func_decl->func_decl.name);
        return type_top(type_checker->type_set);
    }
    const struct type* param_type = infer(type_checker, func_decl->func_decl.param);
    expect_irrefutable_pattern(type_checker, "function parameter", func_decl->func_decl.param);
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

static void mark_var_patterns(struct ast* pattern) {
    if (pattern->tag == AST_IDENT_PATTERN) {
        pattern->ident_pattern.is_var = true;
    } else if (pattern->tag == AST_TUPLE_PATTERN) {
        for (struct ast* arg = pattern->tuple_pattern.args; arg; arg = arg->next)
            mark_var_patterns(arg);
    }
}

static const struct type* infer_const_or_var_decl(struct type_checker* type_checker, struct ast* decl) {
    expect_irrefutable_pattern(type_checker,
        decl->tag == AST_CONST_DECL ? "const" : "variable",
        decl->const_decl.pattern);

    if (decl->tag == AST_VAR_DECL)
        mark_var_patterns(decl->var_decl.pattern);

    if (decl->const_decl.init)
        check_binding(type_checker, decl->const_decl.pattern, decl->const_decl.init);
    else
        infer(type_checker, decl->const_decl.pattern);
    return type_unit(type_checker->type_set);
}

static const struct type* infer_while_loop(struct type_checker* type_checker, struct ast* while_loop) {
    coerce(type_checker, &while_loop->while_loop.cond, type_prim(type_checker->type_set, TYPE_BOOL));
    check(type_checker, while_loop->while_loop.body, type_unit(type_checker->type_set));
    return type_unit(type_checker->type_set);
}

static inline bool is_cast_possible(const struct type* source_type, const struct type* dest_type) {
    if (type_is_subtype(source_type, dest_type))
        return true;
    if (type_is_prim(source_type) && type_is_prim(dest_type))
        return true;
    return false;
}

static const struct type* infer_cast_expr(struct type_checker* type_checker, struct ast* cast_expr) {
    assert(!ast_is_implicit_cast(cast_expr));
    const struct type* dest_type = infer(type_checker, cast_expr->cast_expr.type);
    const struct type* source_type = deref(type_checker, &cast_expr->cast_expr.arg);
    if (!is_cast_possible(source_type, dest_type))
        invalid_cast(type_checker, &cast_expr->source_range, source_type, dest_type);
    return dest_type;
}

static const struct type* infer_proj_elem(
    struct type_checker* type_checker,
    struct ast* proj_elem,
    const struct type* arg_type,
    const struct type* ref_type)
{
    if (proj_elem->proj_elem.name) {
        proj_elem->proj_elem.index = type_find_field(arg_type, proj_elem->proj_elem.name);
    } else if (arg_type->tag != TYPE_TUPLE) {
        log_error(type_checker->log, &proj_elem->source_range, "cannot use integer indices on records");
        return type_top(type_checker->type_set);
    }

    if (proj_elem->proj_elem.index >= type_elem_count(arg_type)) {
        char* type_str = type_to_string(arg_type);
        if (proj_elem->proj_elem.name) {
            log_error(type_checker->log, &proj_elem->source_range,
                "no member named '%s' in '%s'", proj_elem->proj_elem.name, type_str);
        } else {
            log_error(type_checker->log, &proj_elem->source_range,
                "invalid member index '%zu' for tuple type '%s'", proj_elem->proj_elem.index, type_str);
        }
        free(type_str);
        return type_top(type_checker->type_set);
    }

    const struct type* elem_type = type_elem(arg_type, proj_elem->proj_elem.index);
    if (ref_type)
        elem_type = type_ref(type_checker->type_set, elem_type, ref_type->ref_type.is_const);
    return elem_type;
}

static const struct type* infer_proj_expr(struct type_checker* type_checker, struct ast* proj_expr) {
    bool has_one_elem = proj_expr->proj_expr.elems && !proj_expr->proj_expr.elems->next;
    const struct type* arg_type = infer(type_checker, proj_expr->proj_expr.arg);
    const struct type* ref_type = arg_type->tag == TYPE_REF && has_one_elem ? arg_type : NULL;
    arg_type = type_remove_ref(arg_type);

    if (!type_is_aggregate(arg_type)) {
        invalid_type(type_checker, &proj_expr->proj_expr.arg->source_range, "record or tuple", arg_type);
        return type_top(type_checker->type_set);
    }

    struct small_type_vec elem_types;
    small_type_vec_init(&elem_types);
    for (struct ast* elem = proj_expr->proj_expr.elems; elem; elem = elem->next) {
        elem->type = infer_proj_elem(type_checker, elem, arg_type, ref_type);
        small_type_vec_push(&elem_types, &elem->type);
    }

    const struct type* proj_type = elem_types.elem_count != 1
        ? type_tuple(type_checker->type_set, elem_types.elems, elem_types.elem_count)
        : elem_types.elems[0];
    small_type_vec_destroy(&elem_types);
    return proj_type;
}

static const struct type* infer(struct type_checker* type_checker, struct ast* ast) {
    assert(!ast->type);
    switch (ast->tag) {
        case AST_PRIM_TYPE:
            return ast->type = type_prim(type_checker->type_set, (enum type_tag)ast->prim_type.tag);
        case AST_LITERAL:
            return ast->type = infer_literal(type_checker, &ast->literal);
        case AST_FUNC_DECL:
            return ast->type = infer_func_decl(type_checker, ast);
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            return ast->type = infer_const_or_var_decl(type_checker, ast);
        case AST_IDENT_PATTERN:
            if (!ast->ident_pattern.type) {
                cannot_infer(type_checker, &ast->source_range, ast->ident_pattern.name);
                return type_top(type_checker->type_set);
            }
            ast->type = infer(type_checker, ast->ident_pattern.type);
            if (ast->ident_pattern.is_var)
                ast->type = type_ref(type_checker->type_set, ast->type, false);
            return ast->type;
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
        case AST_UNARY_EXPR:
            return ast->type = check_unary_expr(type_checker, ast, NULL);
        case AST_BINARY_EXPR:
            return ast->type = check_binary_expr(type_checker, ast, NULL);
        case AST_IF_EXPR:
            return ast->type = check_if_expr(type_checker, ast, NULL);
        case AST_CAST_EXPR:
            return ast->type = infer_cast_expr(type_checker, ast);
        case AST_CALL_EXPR:
            return ast->type = check_call_expr(type_checker, ast, NULL);
        case AST_PROJ_EXPR:
            return ast->type = infer_proj_expr(type_checker, ast);
        case AST_WHILE_LOOP:
            return ast->type = infer_while_loop(type_checker, ast);
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
