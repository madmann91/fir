#include "token.h"

#include <assert.h>

const char* token_tag_to_string(enum token_tag tag) {
    switch (tag) {
#define x(tag, str) case TOK_##tag: return str;
        FIR_NODE_LIST(x)
#undef x
        case TOK_EOF:      return "<end-of-file>";
        case TOK_ERR:      return "<invalid token>";
        case TOK_IDENT:    return "<identifier>";
        case TOK_INT:      return "<integer literal>";
        case TOK_FLOAT:    return "<floating-point literal>";
        case TOK_LPAREN:   return "(";
        case TOK_RPAREN:   return ")";
        case TOK_LBRACKET: return "[";
        case TOK_RBRACKET: return "]";
        case TOK_LBRACE:   return "{";
        case TOK_RBRACE:   return "}";
        case TOK_COMMA:    return ",";
        case TOK_PLUS:     return "+";
        case TOK_MINUS:    return "-";
        case TOK_EQ:       return "=";
        default:
            assert("invalid token tag");
            return "";
    }
}

bool token_tag_is_node_tag(enum token_tag tag) {
    switch (tag) {
#define x(tag, ...) case TOK_##tag:
        FIR_NODE_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

bool token_tag_is_ty(enum token_tag tag) {
    return token_tag_is_node_tag(tag) && fir_node_tag_is_ty((enum fir_node_tag)tag);
}

struct str_view token_str(const char* data, const struct token* token) {
    return (struct str_view) {
        .data   = data + token->source_range.begin.bytes,
        .length = token->source_range.end.bytes - token->source_range.begin.bytes
    };
}
