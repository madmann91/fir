#pragma once

#include "token.h"

#include <stdbool.h>
#include <stdio.h>

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
    AST_LITERAL,
    AST_IDENT,

    // Types
    AST_PRIM_TYPE,
    AST_FIELD_TYPE,
    AST_RECORD_TYPE,

    // Declarations
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_CONST_DECL,

    // Patterns
    AST_TYPED_PATTERN,
    AST_FIELD_PATTERN,
    AST_RECORD_PATTERN,

    // Expressions
    AST_FIELD_EXPR,
    AST_RECORD_EXPR,
#define x(tag, ...) AST_##tag##_EXPR,
#define y(tag, ...) AST_ASSIGN_##tag##_EXPR,
    UNARY_EXPR_LIST(x)
    BINARY_EXPR_LIST(x)
    BINARY_EXPR_LIST(y)
#undef x
#undef y
};

enum prim_type_tag {
#define x(tag, ...) PRIM_TYPE_##tag = TOK_##tag,
    PRIM_TYPE_LIST(x)
#undef x
};

enum literal_tag {
    LITERAL_BOOL,
    LITERAL_INT,
    LITERAL_FLOAT
};

struct literal {
    enum literal_tag tag;
    union {
        uintmax_t int_val;
        double float_val;
        bool bool_val;
    };
};

struct type;

struct ast {
    enum ast_tag tag;
    struct fir_source_range source_range;
    const struct type* type;
    struct ast* next;
    union {
        struct {
            struct ast* decls;
        } program;
        struct {
            const char* name;
        } ident;
        struct literal literal;
        struct {
            enum prim_type_tag tag;
        } prim_type;
        struct {
            struct ast* name;
            struct ast* param;
            struct ast* ret_type;
            struct ast* body;
        } func_decl;
        struct {
            struct ast* pattern;
            struct ast* init;
        } const_decl, var_decl;
        struct {
            struct ast* pattern;
            struct ast* type;
        } typed_pattern;
        struct {
            struct ast* fields;
        } record_pattern, record_expr, record_type;
        struct {
            struct ast* name;
            struct ast* arg;
        } field_pattern, field_expr, field_type;
        struct {
            struct ast* left;
            struct ast* right;
        } binary_expr;
        struct {
            struct ast* arg;
        } unary_expr;
    };
};

void ast_print(FILE*, const struct ast*);
void ast_dump(const struct ast*);

const char* unary_expr_tag_to_string(enum ast_tag);
const char* binary_expr_tag_to_string(enum ast_tag);
