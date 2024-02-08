#pragma once

#include "token.h"

#include "support/set.h"

#include <fir/node.h>

#include <stdbool.h>
#include <stdio.h>

#define BINARY_EXPR_LIST(x) \
    x(MUL,           "*",   1) \
    x(DIV,           "/",   1) \
    x(REM,           "%",   1) \
    x(ADD,           "+",   2) \
    x(SUB,           "-",   2) \
    x(LSHIFT,        "<<",  3) \
    x(RSHIFT,        ">>",  3) \
    x(CMP_GT,        ">",   4) \
    x(CMP_LT,        "<",   4) \
    x(CMP_GE,        ">=",  4) \
    x(CMP_LE,        "<=",  4) \
    x(CMP_NE,        "!=",  5) \
    x(CMP_EQ,        "==",  5) \
    x(AND,           "&",   6) \
    x(XOR,           "^",   7) \
    x(OR,            "|",   8) \
    x(LOGIC_AND,     "&&",  9) \
    x(LOGIC_OR,      "||",  10) \
    x(ASSIGN,        "=",   11) \
    x(ADD_ASSIGN,    "+=",  11) \
    x(SUB_ASSIGN,    "-=",  11) \
    x(MUL_ASSIGN,    "*=",  11) \
    x(DIV_ASSIGN,    "/=",  11) \
    x(REM_ASSIGN,    "%=",  11) \
    x(RSHIFT_ASSIGN, ">>=", 11) \
    x(LSHIFT_ASSIGN, "<<=", 11) \
    x(AND_ASSIGN,    "&=",  11) \
    x(XOR_ASSIGN,    "^=",  11) \
    x(OR_ASSIGN,     "|=",  11)

#define UNARY_EXPR_LIST(x) \
    x(PLUS,     "+") \
    x(NEG,      "-") \
    x(NOT,      "!") \
    x(PRE_INC,  "++") \
    x(PRE_DEC,  "--") \
    x(POST_INC, "++") \
    x(POST_DEC, "--") \

enum ast_tag {
    AST_ERROR,
    AST_PROGRAM,
    AST_LITERAL,
    AST_PROJ_ELEM,

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
    AST_CAST_EXPR,
    AST_IDENT_EXPR,
    AST_FIELD_EXPR,
    AST_RECORD_EXPR,
    AST_ARRAY_EXPR,
    AST_TUPLE_EXPR,
    AST_UNARY_EXPR,
    AST_BINARY_EXPR,
    AST_BLOCK_EXPR,
    AST_IF_EXPR,
    AST_CALL_EXPR,
    AST_PROJ_EXPR,

    // Statements
    AST_WHILE_LOOP,
};

enum prim_type_tag {
#define x(tag, ...) PRIM_TYPE_##tag = TOK_##tag,
    PRIM_TYPE_LIST(x)
#undef x
};

enum binary_expr_tag {
    BINARY_EXPR_INVALID = 0,
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
            bool is_var;
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
            struct ast* arg;
            struct ast* type;
        } cast_expr;
        struct {
            enum binary_expr_tag tag;
            struct ast* left;
            struct ast* right;
        } binary_expr;
        struct {
            enum unary_expr_tag tag;
            struct ast* arg;
        } unary_expr;
        struct {
            const char* name;
            size_t index;
        } proj_elem;
        struct {
            struct ast* arg;
            struct ast* elems;
        } proj_expr;
        struct {
            struct ast* stmts;
            bool ends_with_semicolon;
        } block_expr;
        struct {
            struct ast* cond;
            struct ast* then_block;
            struct ast* else_block;
        } if_expr;
        struct {
            struct ast* callee;
            struct ast* arg;
        } call_expr;
        struct {
            struct ast* cond;
            struct ast* body;
        } while_loop;
    };
};

struct mem_pool;
struct type_set;
struct log;
struct fir_mod;

SET_DECL(ast_set, struct ast*, PUBLIC)

void ast_print(FILE*, const struct ast*, const struct fir_print_options*);
void ast_dump(const struct ast*);

void ast_check(struct ast*, struct mem_pool*, struct type_set*, struct log*);
void ast_bind(struct ast*, struct log*);
void ast_emit(struct ast*, struct fir_mod*);

const char* unary_expr_tag_to_string(enum unary_expr_tag);
const char* binary_expr_tag_to_string(enum binary_expr_tag);

bool unary_expr_tag_is_prefix(enum unary_expr_tag);
bool unary_expr_tag_is_inc(enum unary_expr_tag);
bool unary_expr_tag_is_dec(enum unary_expr_tag);
bool unary_expr_tag_is_inc_or_dec(enum unary_expr_tag);
int binary_expr_tag_to_precedence(enum binary_expr_tag);
bool binary_expr_tag_is_assign(enum binary_expr_tag);
bool binary_expr_tag_is_cmp(enum binary_expr_tag);
enum binary_expr_tag binary_expr_tag_remove_assign(enum binary_expr_tag);

bool ast_is_implicit_cast(const struct ast*);
bool ast_needs_semicolon(const struct ast*);
bool ast_is_irrefutable_pattern(const struct ast*);
