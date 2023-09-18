#include "scanner.h"

#include <string.h>

struct scanner scanner_init(const char* data, size_t size) {
    return (struct scanner) {
        .data = data,
        .bytes_left = size,
        .bytes_read = 0,
        .source_pos = (struct fir_source_pos) { .row = 1, .col = 1 }
    };
}

static inline bool is_eof(const struct scanner* scanner) {
    return scanner->bytes_left == 0;
}

static inline char cur_char(const struct scanner* scanner) {
    assert(!is_eof(scanner));
    return scanner->data[scanner->bytes_read];
}

static inline void eat_char(struct scanner* scanner) {
    assert(!is_eof(scanner));
    scanner->bytes_read++;
    scanner->bytes_left--;
    if (cur_char(scanner) == '\n') {
        scanner->source_pos.row++;
        scanner->source_pos.col = 1;
    } else {
        scanner->source_pos.col++;
    }
}

static inline bool accept_char(struct scanner* scanner, char c) {
    if (!is_eof(scanner) && cur_char(scanner) == c) {
        eat_char(scanner);
        return true;
    }
    return false;
}

static inline void eat_spaces(struct scanner* scanner) {
    while (!is_eof(scanner) && isspace(cur_char(scanner)))
        eat_char(scanner);
}

static inline struct token make_token(
    struct scanner* scanner,
    size_t first_byte,
    struct fir_source_pos begin_pos,
    enum token_tag tag)
{
    return (struct token) {
        .tag = tag,
        .str = {
            .data = scanner->data + first_byte,
            .length = scanner->bytes_read - first_byte
        },
        .source_range = {
            .begin = begin_pos,
            .end = scanner->source_pos
        }
    };
}

static inline enum token_tag find_keyword(struct str_view view) {
#define x(tag, str) if (!strncmp(str, view.data, view.length)) return TOK_##tag;
    FIR_NODE_LIST(x)
#undef x
    return TOK_ERR;
}

struct token scanner_advance(struct scanner* scanner) {
    while (true) {
        eat_spaces(scanner);
        size_t first_byte = scanner->bytes_read;
        struct fir_source_pos begin_pos = scanner->source_pos;

        if (is_eof(scanner))
            return make_token(scanner, first_byte, begin_pos, TOK_EOF);

        if (accept_char(scanner, '(')) return make_token(scanner, first_byte, begin_pos, TOK_LPAREN);
        if (accept_char(scanner, ')')) return make_token(scanner, first_byte, begin_pos, TOK_RPAREN);
        if (accept_char(scanner, '[')) return make_token(scanner, first_byte, begin_pos, TOK_LBRACKET);
        if (accept_char(scanner, ']')) return make_token(scanner, first_byte, begin_pos, TOK_RBRACKET);
        if (accept_char(scanner, '{')) return make_token(scanner, first_byte, begin_pos, TOK_LBRACKET);
        if (accept_char(scanner, '}')) return make_token(scanner, first_byte, begin_pos, TOK_RBRACKET);
        if (accept_char(scanner, ',')) return make_token(scanner, first_byte, begin_pos, TOK_COMMA);
        if (accept_char(scanner, '=')) return make_token(scanner, first_byte, begin_pos, TOK_EQ);

        if (accept_char(scanner, '#')) {
            while (!is_eof(scanner) && cur_char(scanner) != '\n')
                eat_char(scanner);
            continue;
        }

        if (isalpha(cur_char(scanner)) || cur_char(scanner) == '_') {
            while (!is_eof(scanner) && isalnum(cur_char(scanner)))
                eat_char(scanner);
            struct token token = make_token(scanner, first_byte, begin_pos, TOK_IDENT);
            enum token_tag keyword_tag = find_keyword(token.str);
            if (keyword_tag != TOK_ERR)
                token.tag = keyword_tag;
            return token;
        }

        eat_char(scanner);
        return make_token(scanner, first_byte, begin_pos, TOK_ERR);
    }
}

const char* token_tag_to_string(enum token_tag tag) {
    switch (tag) {
#define x(tag, str) case TOK_##tag: return str;
        FIR_NODE_LIST(x)
#undef x
        case TOK_EOF:      return "<end-of-file>";
        case TOK_ERR:      return "<invalid token>";
        case TOK_IDENT:    return "<identifier>";
        case TOK_LITERAL:  return "<literal>";
        case TOK_LPAREN:   return "(";
        case TOK_RPAREN:   return ")";
        case TOK_LBRACKET: return "[";
        case TOK_RBRACKET: return "]";
        case TOK_LBRACE:   return "{";
        case TOK_RBRACE:   return "}";
        case TOK_COMMA:    return ",";
        case TOK_EQ:       return "=";
        default:
            assert("invalid token tag");
            return "";
    }
}
