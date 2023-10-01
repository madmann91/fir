#pragma once

#include "fir/dbg_info.h"
#include "fir/node_list.h"
#include "fir/node.h"

#include "str.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>

struct scanner {
    const char* data;
    size_t bytes_left;
    struct fir_source_pos source_pos;
};

enum token_tag {
#define x(tag, str) TOK_##tag = FIR_##tag,
    FIR_NODE_LIST(x)
#undef x
    TOK_EOF,
    TOK_ERR,
    TOK_IDENT,
    TOK_INT,
    TOK_FLOAT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_PLUS,
    TOK_MINUS,
    TOK_EQ
};

struct token {
    enum token_tag tag;
    struct fir_source_range source_range;
    union {
        uint64_t int_val;
        double float_val;
    };
};

struct scanner scanner_create(const char* data, size_t size);
struct token scanner_advance(struct scanner*);

struct str_view token_str(const char* data, const struct token*);

const char* token_tag_to_string(enum token_tag);
bool token_tag_is_node_tag(enum token_tag);
bool token_tag_is_ty(enum token_tag);
