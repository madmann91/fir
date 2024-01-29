#pragma once

#include "fir/dbg_info.h"
#include "fir/node_list.h"
#include "fir/node.h"

#include "support/str.h"

#include <stdbool.h>
#include <stdint.h>

enum token_tag {
#define x(tag, str) TOK_##tag = FIR_##tag,
    FIR_NODE_LIST(x)
#undef x
    TOK_EXTERN,
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

const char* token_tag_to_string(enum token_tag);
bool token_tag_is_node_tag(enum token_tag);
bool token_tag_is_ty(enum token_tag);

struct str_view token_str_view(const char* data, const struct token*);
