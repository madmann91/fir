#include "lexer.h"

#include <ctype.h>
#include <string.h>
#include <math.h>
#include <assert.h>

struct lexer lexer_create(const char* data, size_t size) {
    return (struct lexer) {
        .data = data,
        .bytes_left = size,
        .source_pos = (struct fir_source_pos) { .row = 1, .col = 1 }
    };
}

static inline bool is_eof(const struct lexer* lexer) {
    return lexer->bytes_left == 0;
}

static inline char cur_char(const struct lexer* lexer) {
    assert(!is_eof(lexer));
    return lexer->data[lexer->source_pos.bytes];
}

static inline void eat_char(struct lexer* lexer) {
    assert(!is_eof(lexer));
    if (cur_char(lexer) == '\n') {
        lexer->source_pos.row++;
        lexer->source_pos.col = 1;
    } else {
        lexer->source_pos.col++;
    }
    lexer->source_pos.bytes++;
    lexer->bytes_left--;
}

static inline bool accept_char(struct lexer* lexer, char c) {
    if (!is_eof(lexer) && cur_char(lexer) == c) {
        eat_char(lexer);
        return true;
    }
    return false;
}

static inline void eat_spaces(struct lexer* lexer) {
    while (!is_eof(lexer) && isspace(cur_char(lexer)))
        eat_char(lexer);
}

static inline void eat_digits(struct lexer* lexer, int base) {
    if (base == 2) {
        while (!is_eof(lexer) && (cur_char(lexer) == '0' || cur_char(lexer) == '1'))
            eat_char(lexer);
    } else if (base == 16) {
        while (!is_eof(lexer) && isxdigit(cur_char(lexer)))
            eat_char(lexer);
    } else {
        assert(base == 10);
        while (!is_eof(lexer) && isdigit(cur_char(lexer)))
            eat_char(lexer);
    }
}

static inline struct token make_token(
    struct lexer* lexer,
    const struct fir_source_pos* begin_pos,
    enum token_tag tag)
{
    return (struct token) {
        .tag = tag,
        .source_range = {
            .begin = *begin_pos,
            .end = lexer->source_pos
        }
    };
}

static inline enum token_tag find_keyword(struct str_view ident) {
#define x(tag, str) if (str_view_is_equal(&ident, &STR_VIEW(str))) return TOK_##tag;
    FIR_NODE_LIST(x)
#undef x
    if (str_view_is_equal(&ident, &STR_VIEW("mod")))    return TOK_MOD;
    if (str_view_is_equal(&ident, &STR_VIEW("extern"))) return TOK_EXTERN;
    return TOK_ERR;
}

static inline struct token parse_literal(struct lexer* lexer, bool has_minus) {
    bool is_float = false;

    int base = 10;
    int prefix_len = 0;
    struct fir_source_pos begin_pos = lexer->source_pos;
    if (accept_char(lexer, '0')) {
        if (accept_char(lexer, 'b')) { base = 2; prefix_len = 2; }
        else if (accept_char(lexer, 'x')) { base = 16; prefix_len = 2; }
    }

    eat_digits(lexer, base);
    if (accept_char(lexer, '.')) {
        eat_digits(lexer, base);
        is_float = true;
    }

    if ((base == 10 && accept_char(lexer, 'e')) ||
        (base == 16 && accept_char(lexer, 'p')))
    {
        is_float = true;
        if (!accept_char(lexer, '-'))
            accept_char(lexer, '+');
        eat_digits(lexer, 10);
    }

    struct token token = make_token(lexer, &begin_pos, is_float ? TOK_FLOAT : TOK_INT);
    if (is_float) {
        token.float_val = copysign(strtod(token_str_view(lexer->data, &token).data, NULL), has_minus ? -1.0 : 1.0);
    } else if (has_minus) {
        long long int signed_int = -strtoll(token_str_view(lexer->data, &token).data + prefix_len, NULL, base);
        token.int_val = (uint64_t)signed_int;
    } else {
        token.int_val = strtoull(token_str_view(lexer->data, &token).data + prefix_len, NULL, base);
    }
    return token;
}

struct token lexer_advance(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);

        struct fir_source_pos begin_pos = lexer->source_pos;
        if (is_eof(lexer))
            return make_token(lexer, &begin_pos, TOK_EOF);

        if (accept_char(lexer, '(')) return make_token(lexer, &begin_pos, TOK_LPAREN);
        if (accept_char(lexer, ')')) return make_token(lexer, &begin_pos, TOK_RPAREN);
        if (accept_char(lexer, '[')) return make_token(lexer, &begin_pos, TOK_LBRACKET);
        if (accept_char(lexer, ']')) return make_token(lexer, &begin_pos, TOK_RBRACKET);
        if (accept_char(lexer, '{')) return make_token(lexer, &begin_pos, TOK_LBRACE);
        if (accept_char(lexer, '}')) return make_token(lexer, &begin_pos, TOK_RBRACE);
        if (accept_char(lexer, ',')) return make_token(lexer, &begin_pos, TOK_COMMA);
        if (accept_char(lexer, '=')) return make_token(lexer, &begin_pos, TOK_EQ);

        if (accept_char(lexer, '\"')) {
            while (true) {
                if (is_eof(lexer) || cur_char(lexer) == '\n')
                    return make_token(lexer, &begin_pos, TOK_ERR);
                if (accept_char(lexer, '\"'))
                    break;
                eat_char(lexer);
            }
            return make_token(lexer, &begin_pos, TOK_STR);
        }

        if (accept_char(lexer, '-')) {
            if (!is_eof(lexer) && isdigit(cur_char(lexer)))
                return parse_literal(lexer, true);
            return make_token(lexer, &begin_pos, TOK_MINUS);
        }

        if (accept_char(lexer, '+')) {
            if (!is_eof(lexer) && isdigit(cur_char(lexer)))
                return parse_literal(lexer, false);
            return make_token(lexer, &begin_pos, TOK_PLUS);
        }

        if (accept_char(lexer, '#')) {
            while (!is_eof(lexer) && cur_char(lexer) != '\n')
                eat_char(lexer);
            continue;
        }

        if (isalpha(cur_char(lexer)) || cur_char(lexer) == '_') {
            while (!is_eof(lexer) && (isalnum(cur_char(lexer)) || cur_char(lexer) == '_'))
                eat_char(lexer);
            struct token token = make_token(lexer, &begin_pos, TOK_IDENT);
            enum token_tag keyword_tag = find_keyword(token_str_view(lexer->data, &token));
            if (keyword_tag != TOK_ERR)
                token.tag = keyword_tag;
            return token;
        }

        if (isdigit(cur_char(lexer)))
            return parse_literal(lexer, false);

        eat_char(lexer);
        return make_token(lexer, &begin_pos, TOK_ERR);
    }
}
