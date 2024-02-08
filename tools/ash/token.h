#pragma once

#include <fir/dbg_info.h>

#include "support/str.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PRIM_TYPE_LIST(x) \
    x(BOOL, "bool") \
    x(I8,   "i8") \
    x(I16,  "i16") \
    x(I32,  "i32") \
    x(I64,  "i64") \
    x(U8,   "u8") \
    x(U16,  "u16") \
    x(U32,  "u32") \
    x(U64,  "u64") \
    x(F32,  "f32") \
    x(F64,  "f64")

#define KEYWORD_LIST(x) \
    PRIM_TYPE_LIST(x) \
    x(AS, "as") \
    x(FUNC, "func") \
    x(VAR, "var") \
    x(CONST, "const") \
    x(IF, "if") \
    x(ELSE, "else") \
    x(WHILE, "while") \
    x(RETURN, "return") \
    x(BREAK, "break") \
    x(CONTINUE, "continue") \
    x(TRUE, "true") \
    x(FALSE, "false")

#define SYMBOL_LIST(x) \
    x(SEMICOLON, ";") \
    x(COLON, ":") \
    x(COMMA, ",") \
    x(DOT, ".") \
    x(THIN_ARROW, "->") \
    x(FAT_ARROW, "=>") \
    x(LPAREN, "(") \
    x(RPAREN, ")") \
    x(LBRACKET, "[") \
    x(RBRACKET, "]") \
    x(LBRACE, "{") \
    x(RBRACE, "}") \
    x(EQ, "=") \
    x(CMP_EQ, "==") \
    x(CMP_NE, "!=") \
    x(CMP_GT, ">") \
    x(CMP_GE, ">=") \
    x(CMP_LT, "<") \
    x(CMP_LE, "<=") \
    x(INC, "++") \
    x(DEC, "--") \
    x(ADD, "+") \
    x(SUB, "-") \
    x(MUL, "*") \
    x(DIV, "/") \
    x(REM, "%") \
    x(AND, "&") \
    x(OR, "|") \
    x(XOR, "^") \
    x(NOT, "!") \
    x(LOGIC_AND, "&&") \
    x(LOGIC_OR, "||") \
    x(LSHIFT, "<<") \
    x(RSHIFT, ">>") \
    x(ADD_EQ, "+=") \
    x(SUB_EQ, "-=") \
    x(MUL_EQ, "*=") \
    x(DIV_EQ, "/=") \
    x(REM_EQ, "%=") \
    x(AND_EQ, "&=") \
    x(OR_EQ, "|=") \
    x(XOR_EQ, "^=") \
    x(LSHIFT_EQ, "<<=") \
    x(RSHIFT_EQ, ">>=")

#define TOKEN_LIST(x) \
    SYMBOL_LIST(x) \
    KEYWORD_LIST(x) \
    x(IDENT, "<identifier>") \
    x(EOF, "<end-of-file>") \
    x(ERR, "<invalid token>") \
    x(INT, "<integer literal>") \
    x(FLOAT, "<floating-point literal>")

enum token_tag {
#define x(tag, ...) TOK_##tag,
    TOKEN_LIST(x)
#undef x
};

struct token {
    enum token_tag tag;
    struct fir_source_range source_range;
    union {
        uintmax_t int_val;
        double float_val;
    };
};

const char* token_tag_to_string(enum token_tag);
bool token_tag_is_symbol(enum token_tag);
bool token_tag_is_keyword(enum token_tag);

struct str_view token_str_view(const char* data, const struct token*);
