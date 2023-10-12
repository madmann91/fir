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
    fprintf(file, "%s", begin);
    for (; ast; ast = ast->next) {
        ast_print(file, ast);
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

void ast_print(FILE* file, const struct ast* ast) {
    switch (ast->tag) {
        case AST_ERROR:
            fprintf(file, "<ERROR>");
            break;
        case AST_PROGRAM:
            print_many(file, "", "\n", "", ast->program.decls);
            break;
        case AST_LITERAL:
            print_literal(file, &ast->literal);
            break;
        case AST_PRIM_TYPE:
            print_prim_type(file, ast->prim_type.tag);
            break;
        case AST_IDENT_EXPR:
            fprintf(file, "%s", ast->ident_expr.name);
            break;
        case AST_IDENT_PATTERN:
            fprintf(file, "%s", ast->ident_pattern.name);
            if (ast->ident_pattern.type) {
                fprintf(file, ": ");
                ast_print(file, ast->ident_pattern.type);
            }
            break;
        case AST_FIELD_TYPE:
        case AST_FIELD_EXPR:
        case AST_FIELD_PATTERN:
            if (ast->field_type.name)
                fprintf(file, ast->tag == AST_FIELD_TYPE ? "%s: " : "%s = ", ast->field_type.name);
            ast_print(file, ast->field_type.arg);
            break;
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            print_many(file, "[", ", ", "]", ast->record_type.fields);
            break;
        case AST_FUNC_DECL:
            fprintf(file, "func %s", ast->func_decl.name);
            print_many(file, "(", ", ", ")", ast->func_decl.params);
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
