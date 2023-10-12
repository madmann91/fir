#include "ast.h"

#include "support/format.h"

#include <assert.h>
#include <inttypes.h>

static inline void print_many(
    struct format_out* out,
    const char* begin,
    const char* sep,
    const char* end,
    const struct ast* ast)
{
    format(out, "%s", begin);
    for (; ast; ast = ast->next) {
        ast_print(out, ast);
        if (ast->next)
            format(out, "%s", sep);
    }
    format(out, "%s", end);
}

static void print_prim_type(struct format_out* out, enum prim_type_tag tag) {
    switch (tag) {
#define x(tag, str) case PRIM_TYPE_##tag: format(out, str); break;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid primitive type");
            break;
    }
}

static void print_literal(struct format_out* out, const struct literal* literal) {
    if (literal->tag == LITERAL_BOOL)
        format(out, literal->bool_val ? "true" : "false");
    else if (literal->tag == LITERAL_INT)
        format(out, "%"PRIuMAX, literal->int_val);
    else if (literal->tag == LITERAL_FLOAT)
        format(out, "%g", literal->float_val);
}

void ast_print(struct format_out* out, const struct ast* ast) {
    switch (ast->tag) {
        case AST_ERROR:
            format(out, "<ERROR>");
            break;
        case AST_PROGRAM:
            print_many(out, "", "\n", "", ast->program.decls);
            break;
        case AST_LITERAL:
            print_literal(out, &ast->literal);
            break;
        case AST_PRIM_TYPE:
            print_prim_type(out, ast->prim_type.tag);
            break;
        case AST_IDENT_EXPR:
            format(out, "%s", ast->ident_expr.name);
            break;
        case AST_IDENT_PATTERN:
            format(out, "%s", ast->ident_pattern.name);
            if (ast->ident_pattern.type) {
                format(out, ": ");
                ast_print(out, ast->ident_pattern.type);
            }
            break;
        case AST_FIELD_TYPE:
        case AST_FIELD_EXPR:
        case AST_FIELD_PATTERN:
            if (ast->field_type.name)
                format(out, ast->tag == AST_FIELD_TYPE ? "%s: " : "%s = ", ast->field_type.name);
            ast_print(out, ast->field_type.arg);
            break;
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            print_many(out, "[", ", ", "]", ast->record_type.fields);
            break;
        case AST_FUNC_DECL:
            format(out, "func %s", ast->func_decl.name);
            print_many(out, "(", ", ", ")", ast->func_decl.params);
            if (ast->func_decl.ret_type) {
                format(out, " -> ");
                ast_print(out, ast->func_decl.ret_type);
            }
            if (ast->func_decl.body) {
                format(out, " = ");
                ast_print(out, ast->func_decl.body);
            }
            format(out, ";");
            break;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

void ast_dump(const struct ast* ast) {
    ast_print(&(struct format_out) { .tag = FORMAT_OUT_FILE, .file = stdout }, ast);
    printf("\n");
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
