#include "ast.h"

#include <assert.h>
#include <inttypes.h>

static inline void print_many(
    FILE* file,
    const char* begin,
    const char* sep,
    const char* end,
    const struct ast* ast)
{
    fputs(begin, file);
    for (; ast; ast = ast->next) {
        ast_print(file, ast);
        if (ast->next)
            fputs(sep, file);
    }
    fputs(end, file);
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

void ast_print(FILE* file, const struct ast* ast) {
    switch (ast->tag) {
        case AST_ERROR:
            fprintf(file, "<ERROR>");
            break;
        case AST_PROGRAM:
            print_many(file, "", "", "", ast->program.decls);
            break;
        case AST_LITERAL:
            print_literal(file, &ast->literal);
            break;
        case AST_PRIM_TYPE:
            print_prim_type(file, ast->prim_type.tag);
            break;
        case AST_IDENT:
            fprintf(file, "%s", ast->ident.name);
            break;
        case AST_TYPED_PATTERN:
            ast_print(file, ast->typed_pattern.pattern);
            fprintf(file, ": ");
            ast_print(file, ast->typed_pattern.type);
            break;
        case AST_FIELD_TYPE:
        case AST_FIELD_EXPR:
        case AST_FIELD_PATTERN:
            if (ast->field_expr.name) {
                ast_print(file, ast->field_expr.name);
                fprintf(file, ast->tag == AST_FIELD_TYPE ? ": " : " = ");
            }
            ast_print(file, ast->field_expr.arg);
            break;
        case AST_RECORD_EXPR:
        case AST_RECORD_TYPE:
        case AST_RECORD_PATTERN:
            print_many(file, "[", ", ", "]", ast->record_type.fields);
            break;
        case AST_FUNC_DECL:
            fprintf(file, "func ");
            ast_print(file, ast->func_decl.name);
            fprintf(file, "(");
            ast_print(file, ast->func_decl.param);
            fprintf(file, ")");
            if (ast->func_decl.ret_type) {
                fprintf(file, " -> ");
                ast_print(file, ast->func_decl.ret_type);
            }
            if (ast->func_decl.body) {
                fprintf(file, " = ");
                ast_print(file, ast->func_decl.body);
            }
            fprintf(file, ";");
            break;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

void ast_dump(const struct ast* ast) {
    ast_print(stdout, ast);
    fprintf(stdout, "\n");
    fflush(stdout);
}

const char* binary_expr_tag_to_string(enum ast_tag ast_tag) {
    switch (ast_tag) {
#define x(tag, str) case AST_##tag##_EXPR: return str;
#define y(tag, str) case AST_ASSIGN_##tag##_EXPR: return str"=";
        BINARY_EXPR_LIST(x)
        BINARY_EXPR_LIST(y)
#undef x
#undef y
        default:
            assert(false && "invalid binary expression");
            return "";
    }
}

const char* unary_expr_tag_to_string(enum ast_tag ast_tag) {
    switch (ast_tag) {
#define x(tag, str) case AST_##tag##_EXPR: return str;
        UNARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid unary expression");
            return "";
    }
}
