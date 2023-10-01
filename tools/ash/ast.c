#include "ast.h"

#include <assert.h>

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
