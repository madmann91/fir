#include "ast.h"

#include "support/term.h"

#include <assert.h>
#include <inttypes.h>

static inline uint32_t hash_ast(uint32_t h, struct ast* const* ast_ptr) {
    return hash_uint64(h, (uintptr_t)*ast_ptr);
}

static inline bool cmp_ast(struct ast* const* ast_ptr, struct ast* const* other_ptr) {
    return *ast_ptr == *other_ptr;
}

SET_IMPL(ast_set, struct ast*, hash_ast, cmp_ast, PUBLIC)

static inline void print_indent(FILE* file, size_t indent, const char* tab) {
    for (size_t i = 0; i < indent; ++i)
        fputs(tab, file);
}

static inline void print_many(
    FILE* file,
    const char* begin,
    const char* sep,
    const char* end,
    const struct ast* ast,
    const struct fir_print_options* options)
{
    fprintf(file, "%s", begin);
    for (; ast; ast = ast->next) {
        ast_print(file, ast, options);
        if (ast->next)
            fprintf(file, "%s", sep);
    }
    fprintf(file, "%s", end);
}

static void print_prim_type(FILE* file, enum prim_type_tag tag) {
    switch (tag) {
#define x(tag, str) case PRIM_TYPE_##tag: fprintf(file, str); break;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid primitive type");
            break;
    }
}

static void print_literal(FILE* file, const struct literal* literal) {
    if (literal->tag == LITERAL_BOOL)
        fprintf(file, literal->bool_val ? "true" : "false");
    else if (literal->tag == LITERAL_INT)
        fprintf(file, "%"PRIuMAX, literal->int_val);
    else if (literal->tag == LITERAL_FLOAT)
        fprintf(file, "%g", literal->float_val);
}

static void print_with_parens(
    FILE* file,
    const struct ast* param,
    const struct fir_print_options* options)
{
    if (param->tag == AST_TUPLE_TYPE ||
        param->tag == AST_TUPLE_EXPR ||
        param->tag == AST_TUPLE_PATTERN)
        param = param->tuple_type.args;
    print_many(file, "(", ", ", ")", param, options);
}

void ast_print(FILE* file, const struct ast* ast, const struct fir_print_options* options) {
    const char* keyword_style = options->disable_colors ? "" : TERM2(TERM_FG_GREEN, TERM_BOLD);
    const char* literal_style = options->disable_colors ? "" : TERM1(TERM_FG_CYAN);
    const char* error_style = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD);
    const char* reset_style = options->disable_colors ? "" : TERM1(TERM_RESET);
    switch (ast->tag) {
        case AST_ERROR:
            fprintf(file, "%s<ERROR>%s", error_style, reset_style);
            break;
        case AST_PROGRAM:
            print_many(file, "", "\n", "", ast->program.decls, options);
            fprintf(file, "\n");
            break;
        case AST_LITERAL:
            fprintf(file, "%s", literal_style);
            print_literal(file, &ast->literal);
            fprintf(file, "%s", reset_style);
            break;
        case AST_PRIM_TYPE:
            fprintf(file, "%s", keyword_style);
            print_prim_type(file, ast->prim_type.tag);
            fprintf(file, "%s", reset_style);
            break;
        case AST_IDENT_EXPR:
            fprintf(file, "%s", ast->ident_expr.name);
            break;
        case AST_IDENT_PATTERN:
            fprintf(file, "%s", ast->ident_pattern.name);
            if (ast->ident_pattern.type) {
                fprintf(file, ": ");
                ast_print(file, ast->ident_pattern.type, options);
            }
            break;
        case AST_IMPLICIT_CAST_EXPR:
            ast_print(file, ast->implicit_cast_expr.expr, options);
            break;
        case AST_FIELD_TYPE:
        case AST_FIELD_EXPR:
        case AST_FIELD_PATTERN:
            if (ast->field_type.name)
                fprintf(file, ast->tag == AST_FIELD_TYPE ? "%s: " : "%s = ", ast->field_type.name);
            ast_print(file, ast->field_type.arg, options);
            break;
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            print_many(file, "[", ", ", "]", ast->record_type.fields, options);
            break;
        case AST_TUPLE_TYPE:
        case AST_TUPLE_EXPR:
        case AST_TUPLE_PATTERN:
            print_many(file, "(", ", ", ")", ast->tuple_type.args, options);
            break;
        case AST_BLOCK_EXPR:
            if (!ast->block_expr.stmts) {
                fprintf(file, "{}");
            } else {
                struct fir_print_options block_options = *options;
                block_options.indent++;
                fprintf(file, "{\n");
                for (struct ast* stmt = ast->block_expr.stmts; stmt; stmt = stmt->next) {
                    print_indent(file, block_options.indent, block_options.tab);
                    ast_print(file, stmt, &block_options);
                    if ((stmt->next && ast_needs_semicolon(stmt)) ||
                        (!stmt->next && ast->block_expr.ends_with_semicolon))
                        fprintf(file, ";");
                    fprintf(file, "\n");
                }
                print_indent(file, options->indent, options->tab);
                fprintf(file, "}");
            }
            break;
        case AST_FUNC_DECL:
            fprintf(file, "%sfunc%s %s", keyword_style, reset_style, ast->func_decl.name);
            print_with_parens(file, ast->func_decl.param, options);
            if (ast->func_decl.ret_type) {
                fprintf(file, " -> ");
                ast_print(file, ast->func_decl.ret_type, options);
            }
            if (ast->func_decl.body) {
                fprintf(file, " = ");
                ast_print(file, ast->func_decl.body, options);
            }
            fprintf(file, ";");
            break;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

void ast_dump(const struct ast* ast) {
    struct fir_print_options options = fir_print_options_default(stdout);
    ast_print(stdout, ast, &options);
    fflush(stdout);
}

const char* binary_expr_tag_to_string(enum binary_expr_tag tag) {
    switch (tag) {
#define x(tag, str) case BINARY_EXPR_##tag: return str;
        BINARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid binary expression");
            return "";
    }
}

const char* unary_expr_tag_to_string(enum unary_expr_tag tag) {
    switch (tag) {
#define x(tag, str) case UNARY_EXPR_##tag: return str;
        UNARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid unary expression");
            return "";
    }
}

bool ast_needs_semicolon(const struct ast* ast) {
    switch (ast->tag) {
        case AST_LITERAL:
        case AST_TUPLE_EXPR:
        case AST_BLOCK_EXPR:
            return true;
        default:
            return false;
    }
}
