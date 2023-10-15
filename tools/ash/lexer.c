#include "lexer.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>

struct lexer lexer_create(const char* data, size_t size) {
    return (struct lexer) {
        .data = data,
        .bytes_left = size,
        .source_pos = { .row = 1, .col = 1, .bytes = 0 }
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

static inline struct token make_token(
    struct lexer* lexer,
    struct fir_source_pos* begin_pos,
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

static inline bool accept_digit(struct lexer* lexer, int base) {
    if (is_eof(lexer))
        return false;
    char c = cur_char(lexer);
    if ((base ==  2 && (c == '0' || c == '1')) ||
        (base == 10 && isdigit(c)) ||
        (base == 16 && isxdigit(c)))
    {
        eat_char(lexer);
        return true;
    }
    return false;
}

static inline bool accept_exp(struct lexer* lexer, int base) {
    return
        (base == 10 && (accept_char(lexer, 'e') || accept_char(lexer, 'E'))) ||
        (base == 16 && (accept_char(lexer, 'p') || accept_char(lexer, 'P')));
}

static inline struct token parse_literal(struct lexer* lexer) {
    struct fir_source_pos begin_pos = lexer->source_pos;

    int base = 10;
    size_t prefix_len = 0;
    if (accept_char(lexer, '0')) {
        if (accept_char(lexer, 'b'))      base = 2, prefix_len = 2;
        else if (accept_char(lexer, 'x')) base = 16, prefix_len = 2;
    }

    while (accept_digit(lexer, base)) ;

    bool has_dot = false;
    if (accept_char(lexer, '.')) {
        has_dot = true;
        while (accept_digit(lexer, base)) ;
    }

    bool has_exp = false;
    if (accept_exp(lexer, base)) {
        if (!accept_char(lexer, '+'))
            accept_char(lexer, '-');
        while (accept_digit(lexer, 10)) ;
    }

    bool is_float = has_exp || has_dot;
    struct token token = make_token(lexer, &begin_pos, is_float ? TOK_FLOAT : TOK_INT);
    if (is_float)
        token.float_val = strtod(token_str_view(lexer->data, &token).data, NULL);
    else
        token.int_val = strtoumax(token_str_view(lexer->data, &token).data + prefix_len, NULL, base);
    return token;
}

static inline enum token_tag find_keyword(struct str_view ident) {
#define x(tag, str) if (str_view_cmp(&ident, &STR_VIEW(str))) return TOK_##tag;
    KEYWORD_LIST(x)
#undef x
    return TOK_ERR;
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
        if (accept_char(lexer, ';')) return make_token(lexer, &begin_pos, TOK_SEMICOLON);
        if (accept_char(lexer, ':')) return make_token(lexer, &begin_pos, TOK_COLON);
        if (accept_char(lexer, ',')) return make_token(lexer, &begin_pos, TOK_COMMA);
        if (accept_char(lexer, '.')) return make_token(lexer, &begin_pos, TOK_DOT);

        if (accept_char(lexer, '=')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_CMP_EQ);
            return make_token(lexer, &begin_pos, TOK_EQ);
        }

        if (accept_char(lexer, '>')) {
            if (accept_char(lexer, '>')) {
                if (accept_char(lexer, '='))
                    return make_token(lexer, &begin_pos, TOK_RSHIFT_EQ);
                return make_token(lexer, &begin_pos, TOK_RSHIFT);
            }
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_CMP_GE);
            return make_token(lexer, &begin_pos, TOK_CMP_GT);
        }

        if (accept_char(lexer, '<')) {
            if (accept_char(lexer, '<')) {
                if (accept_char(lexer, '='))
                    return make_token(lexer, &begin_pos, TOK_LSHIFT_EQ);
                return make_token(lexer, &begin_pos, TOK_LSHIFT);
            }
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_CMP_LE);
            return make_token(lexer, &begin_pos, TOK_CMP_LT);
        }

        if (accept_char(lexer, '+')) {
            if (accept_char(lexer, '+'))
                return make_token(lexer, &begin_pos, TOK_INC);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_ADD_EQ);
            return make_token(lexer, &begin_pos, TOK_ADD);
        }

        if (accept_char(lexer, '-')) {
            if (accept_char(lexer, '-'))
                return make_token(lexer, &begin_pos, TOK_DEC);
            if (accept_char(lexer, '>'))
                return make_token(lexer, &begin_pos, TOK_THIN_ARROW);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_SUB_EQ);
            return make_token(lexer, &begin_pos, TOK_SUB);
        }

        if (accept_char(lexer, '*')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_MUL_EQ);
            return make_token(lexer, &begin_pos, TOK_MUL);
        }

        if (accept_char(lexer, '/')) {
            if (accept_char(lexer, '/')) {
                while (!is_eof(lexer) && cur_char(lexer) != '\n')
                    eat_char(lexer);
                continue;
            }
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_DIV_EQ);
            return make_token(lexer, &begin_pos, TOK_DIV);
        }

        if (accept_char(lexer, '%')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_REM_EQ);
            return make_token(lexer, &begin_pos, TOK_REM);
        }

        if (accept_char(lexer, '&')) {
            if (accept_char(lexer, '&'))
                return make_token(lexer, &begin_pos, TOK_LOGIC_AND);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_AND_EQ);
            return make_token(lexer, &begin_pos, TOK_AND);
        }

        if (accept_char(lexer, '|')) {
            if (accept_char(lexer, '|'))
                return make_token(lexer, &begin_pos, TOK_LOGIC_OR);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_OR_EQ);
            return make_token(lexer, &begin_pos, TOK_OR);
        }

        if (accept_char(lexer, '^')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOK_XOR_EQ);
            return make_token(lexer, &begin_pos, TOK_XOR);
        }

        if (isdigit(cur_char(lexer)))
            return parse_literal(lexer);

        if (isalpha(cur_char(lexer)) || cur_char(lexer) == '_') {
            while (!is_eof(lexer) && (isalnum(cur_char(lexer)) || cur_char(lexer) == '_'))
                eat_char(lexer);
            struct token token = make_token(lexer, &begin_pos, TOK_IDENT);
            enum token_tag keyword_tag = find_keyword(token_str_view(lexer->data, &token));
            if (keyword_tag != TOK_ERR)
                token.tag = keyword_tag;
            return token;
        }

        eat_char(lexer);
        return make_token(lexer, &begin_pos, TOK_ERR);
    }
}
