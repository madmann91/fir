#pragma once

#include <fir/dbg_info.h>

#include "support/str.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define KEYWORD_LIST(x) \
    x(FUNC, "func") \
    x(VAR, "var") \
    x(CONST, "const") \
    x(IF, "if") \
    x(ELSE, "else") \
    x(WHILE, "while") \
    x(RETURN, "return") \
    x(BREAK, "break") \
    x(CONTINUE, "continue")

#define SYMBOL_LIST(x) \
    x(SEMICOLON, ";") \
    x(COLON, ":") \
    x(COMMA, ",") \
    x(DOT, ".") \
    x(LPAREN, "(") \
    x(RPAREN, ")") \
    x(LBRACKET, "[") \
    x(RBRACKET, "]") \
    x(LBRACE, "{") \
    x(RBRACE, "}") \
    x(EQ, "=") \
    x(CMP_EQ, "==") \
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

struct str_view token_str(const char* data, const struct token*);
