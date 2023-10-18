#pragma once

#include "token.h"

#include <fir/node.h>

#include <stdbool.h>
#include <stdio.h>

#define BINARY_EXPR_LIST(x) \
    x(ASSIGN, "=") \
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

    // Declarations
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_CONST_DECL,

    // Types
    AST_PRIM_TYPE,
    AST_FIELD_TYPE,
    AST_RECORD_TYPE,
    AST_ARRAY_TYPE,
    AST_TUPLE_TYPE,
    AST_DYN_ARRAY_TYPE,

    // Patterns
    AST_IDENT_PATTERN,
    AST_FIELD_PATTERN,
    AST_RECORD_PATTERN,
    AST_ARRAY_PATTERN,
    AST_TUPLE_PATTERN,

    // Expressions
    AST_IMPLICIT_CAST_EXPR,
    AST_IDENT_EXPR,
    AST_FIELD_EXPR,
    AST_RECORD_EXPR,
    AST_ARRAY_EXPR,
    AST_TUPLE_EXPR,
    AST_UNARY_EXPR,
    AST_BINARY_EXPR
};

enum prim_type_tag {
#define x(tag, ...) PRIM_TYPE_##tag = TOK_##tag,
    PRIM_TYPE_LIST(x)
#undef x
};

enum binary_expr_tag {
#define x(tag, ...) BINARY_EXPR_##tag,
    BINARY_EXPR_LIST(x)
#undef x
};

enum unary_expr_tag {
#define x(tag, ...) UNARY_EXPR_##tag,
    UNARY_EXPR_LIST(x)
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
    const struct fir_node* node;
    struct ast* next;
    union {
        struct {
            struct ast* decls;
        } program;
        struct literal literal;
        struct {
            const char* name;
            struct ast* bound_to;
        } ident_expr;
        struct {
            const char* name;
            struct ast* type;
        } ident_pattern;
        struct {
            enum prim_type_tag tag;
        } prim_type;
        struct {
            struct ast* elem_type;
            size_t elem_count;
        } array_type;
        struct {
            struct ast* elem_type;
        } dyn_array_type;
        struct {
            const char* name;
            struct ast* param;
            struct ast* ret_type;
            struct ast* body;
        } func_decl;
        struct {
            struct ast* pattern;
            struct ast* init;
        } const_decl, var_decl;
        struct {
            const char* name;
            struct ast* arg;
        } field_pattern, field_expr, field_type;
        struct {
            struct ast* fields;
        } record_pattern, record_expr, record_type;
        struct {
            struct ast* elems;
        } array_expr, array_pattern;
        struct {
            struct ast* args;
        } tuple_type, tuple_expr, tuple_pattern;
        struct {
            struct ast* expr;
        } implicit_cast_expr;
        struct {
            enum binary_expr_tag tag;
            bool is_assign;
            struct ast* left;
            struct ast* right;
        } binary_expr;
        struct {
            enum unary_expr_tag tag;
            struct ast* arg;
        } unary_expr;
    };
};

struct mem_pool;
struct type_set;
struct log;
struct fir_mod;

void ast_print(FILE*, const struct ast*, const struct fir_print_options*);
void ast_dump(const struct ast*);

void ast_check(struct ast*, struct mem_pool*, struct type_set*, struct log*);
void ast_bind(struct ast*, struct log*);
void ast_emit(struct ast*, struct fir_mod*);

const char* unary_expr_tag_to_string(enum unary_expr_tag);
const char* binary_expr_tag_to_string(enum binary_expr_tag);
