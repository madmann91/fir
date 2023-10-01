#pragma once

#include "token.h"

#include <stdbool.h>

#define BINARY_EXPR_LIST(x) \
    x(ADD, "+") \
    x(SUB, "-") \
    x(MUL, "*") \
    x(DIV, "/") \
    x(REM, "%") \
    x(AND, "&") \
    x(OR,  "|") \
    x(XOR, "^") \
    x(LSHIFT, "<<") \
    x(RSHIFT, ">>")

#define UNARY_EXPR_LIST(x) \
    x(PLUS, "+") \
    x(NEG, "-") \
    x(NOT, "!") \
    x(PRE_INC, "++") \
    x(PRE_DEC, "--") \
    x(POST_INC, "++") \
    x(POST_DEC, "--") \

enum ast_tag {
    AST_ERROR,
    AST_PROGRAM,

    // Types
    AST_INT_TYPE,
    AST_FLOAT_TYPE,
    AST_TUPLE_TYPE,

    // Declarations
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_CONST_DECL,

    // Patterns
    AST_IDENT_PATTERN,
    AST_TYPED_PATTERN,
    AST_TUPLE_PATTERN,

    // Expressions
    AST_IDENT_EXPR,
    AST_TUPLE_EXPR,
#define x(tag, ...) AST_##tag##_EXPR,
#define y(tag, ...) AST_ASSIGN_##tag##_EXPR,
    UNARY_EXPR_LIST(x)
    BINARY_EXPR_LIST(x)
    BINARY_EXPR_LIST(y)
#undef x
#undef y
};

struct type;

struct ast {
    enum ast_tag tag;
    struct fir_source_range range;
    const struct type* type;
    struct ast* next;
    union {
        struct {
            struct ast* decls;
        } program;
        struct {
            size_t bitwidth;
        } int_type, float_type;
        struct {
            const char* name;
            struct ast* param;
            struct ast* ret_type;
            struct ast* body;
        } func_decl;
        struct {
            const char* name;
        } ident_pattern, ident_expr;
        struct {
            struct ast* pattern;
            struct ast* type;
        } typed_pattern;
        struct {
            struct ast* args;
        } tuple_pattern, tuple_expr, tuple_type;
        struct {
            struct ast* left;
            struct ast* right;
        } binary_expr;
        struct {
            struct ast* arg;
        } unary_expr;
    };
};

const char* unary_expr_tag_to_string(enum ast_tag);
const char* binary_expr_tag_to_string(enum ast_tag);
