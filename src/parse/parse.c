#include "fir/module.h"

#include "lexer.h"

#include "support/io.h"
#include "support/log.h"
#include "support/vec.h"
#include "support/map.h"
#include "support/str.h"
#include "support/hash.h"
#include "support/bits.h"
#include "support/datatypes.h"

#include <stdio.h>
#include <stdarg.h>

#define TOKEN_LOOKAHEAD 1

MAP_DEFINE(symbol_table, struct str_view, const struct fir_node*, str_view_hash, str_view_is_equal, PRIVATE)

struct parser_state {
    struct lexer lexer;
    struct token ahead[TOKEN_LOOKAHEAD];
};

struct delayed_nominal_node {
    struct fir_node* nominal_node;
    struct parser_state parser_state;
};

VEC_DEFINE(delayed_nominal_node_vec, struct delayed_nominal_node, PRIVATE)

struct parser {
    struct fir_mod* mod;
    struct fir_dbg_info_pool* dbg_pool;
    struct symbol_table symbol_table;
    struct delayed_nominal_node_vec delayed_nominal_nodes;
    struct parser_state state;
    struct log log;
};

static inline void next_token(struct parser* parser) {
    for (size_t i = 1; i < TOKEN_LOOKAHEAD; ++i)
        parser->state.ahead[i - 1] = parser->state.ahead[i];
    parser->state.ahead[TOKEN_LOOKAHEAD - 1] = lexer_advance(&parser->state.lexer);
}

static inline void eat_token(struct parser* parser, [[maybe_unused]] enum token_tag tag) {
    assert(parser->state.ahead[0].tag == tag);
    next_token(parser);
}

static inline bool accept_token(struct parser* parser, enum token_tag tag) {
    if (parser->state.ahead->tag == tag) {
        next_token(parser);
        return true;
    }
    return false;
}

static inline bool expect_token(struct parser* parser, enum token_tag tag) {
    if (!accept_token(parser, tag)) {
        struct str_view str_view = token_str_view(parser->state.lexer.data, parser->state.ahead);
        log_error(&parser->log,
            &parser->state.ahead->source_range,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str_view.length, str_view.data);
        return false;
    }
    return true;
}

static inline void invalid_token(struct parser* parser, const char* msg) {
    struct str_view str_view = token_str_view(parser->state.lexer.data, parser->state.ahead);
    log_error(&parser->log,
        &parser->state.ahead->source_range,
        "expected %s, but got '%.*s'",
        msg, (int)str_view.length, str_view.data);
    next_token(parser);
}

static inline void unknown_identifier(
    struct parser* parser,
    const struct fir_source_range* ident_range,
    struct str_view ident)
{
    log_error(&parser->log,
        ident_range,
        "unknown identifier '%.*s'",
        (int)ident.length, ident.data);
}

static inline void invalid_flag(
    struct parser* parser,
    const struct fir_source_range* source_range,
    const char* flag_type,
    struct str_view flag_name)
{
    log_error(&parser->log, source_range,
        "invalid %s flag '%.*s'", flag_type, (int)flag_name.length, flag_name.data);
}

static inline const struct fir_node* parse_ty(struct parser*);

static inline uint64_t parse_int_val(struct parser* parser) {
    uint64_t int_val = parser->state.ahead->int_val;
    expect_token(parser, TOK_INT);
    return int_val;
}

static inline double parse_float_val(struct parser* parser) {
    double float_val = parser->state.ahead->float_val;
    expect_token(parser, TOK_FLOAT);
    return float_val;
}

static inline enum fir_fp_flags parse_fp_flags(struct parser* parser) {
    enum fir_fp_flags fp_flags = FIR_FP_STRICT;
    while (accept_token(parser, TOK_PLUS)) {
        struct str_view ident = token_str_view(parser->state.lexer.data, parser->state.ahead);
        if (str_view_is_equal(&ident, &STR_VIEW("fo")))       fp_flags |= FIR_FP_FINITE_ONLY;
        else if (str_view_is_equal(&ident, &STR_VIEW("nsz"))) fp_flags |= FIR_FP_NO_SIGNED_ZERO;
        else if (str_view_is_equal(&ident, &STR_VIEW("a")))   fp_flags |= FIR_FP_ASSOCIATIVE;
        else if (str_view_is_equal(&ident, &STR_VIEW("d")))   fp_flags |= FIR_FP_DISTRIBUTIVE;
        else invalid_flag(parser, &parser->state.ahead->source_range, "floating-point", ident);
        expect_token(parser, TOK_IDENT);
    }
    return fp_flags;
}

static inline enum fir_mem_flags parse_mem_flags(struct parser* parser) {
    enum fir_mem_flags mem_flags = 0;
    while (accept_token(parser, TOK_PLUS)) {
        struct str_view ident = token_str_view(parser->state.lexer.data, parser->state.ahead);
        if (str_view_is_equal(&ident, &STR_VIEW("nn")))     mem_flags |= FIR_MEM_NON_NULL;
        else if (str_view_is_equal(&ident, &STR_VIEW("v"))) mem_flags |= FIR_MEM_VOLATILE;
        else invalid_flag(parser, &parser->state.ahead->source_range, "memory", ident);
        expect_token(parser, TOK_IDENT);
    }
    return mem_flags;
}

static inline uint64_t parse_array_dim(struct parser* parser) {
    return parse_int_val(parser);
}

static inline uint32_t parse_bitwidth(struct parser* parser) {
    return parse_int_val(parser);
}

static inline const struct fir_node* parse_int_or_float_ty(struct parser* parser) {
    bool is_int = parser->state.ahead->tag == TOK_INT_TY;
    next_token(parser);
    expect_token(parser, TOK_LBRACKET);
    uint32_t bitwidth = parse_bitwidth(parser);
    expect_token(parser, TOK_RBRACKET);
    return is_int ? fir_int_ty(parser->mod, bitwidth) : fir_float_ty(parser->mod, bitwidth);
}

static inline const struct fir_node* parse_array_ty(struct parser* parser) {
    eat_token(parser, TOK_ARRAY_TY);
    expect_token(parser, TOK_LBRACKET);
    uint64_t array_dim = parse_array_dim(parser);
    expect_token(parser, TOK_RBRACKET);
    expect_token(parser, TOK_LPAREN);
    const struct fir_node* elem_ty = parse_ty(parser);
    expect_token(parser, TOK_RPAREN);
    if (!elem_ty)
        return NULL;
    return fir_array_ty(elem_ty, array_dim);
}

static inline const struct fir_node* parse_dynarray_ty(struct parser* parser) {
    eat_token(parser, TOK_DYNARRAY_TY);
    expect_token(parser, TOK_LPAREN);
    const struct fir_node* elem_ty = parse_ty(parser);
    expect_token(parser, TOK_RPAREN);
    if (!elem_ty)
        return NULL;
    return fir_dynarray_ty(elem_ty);
}

static inline const struct fir_node* parse_func_ty(struct parser* parser) {
    eat_token(parser, TOK_FUNC_TY);
    expect_token(parser, TOK_LPAREN);
    const struct fir_node* param_ty = parse_ty(parser);
    expect_token(parser, TOK_COMMA);
    const struct fir_node* ret_ty = parse_ty(parser);
    expect_token(parser, TOK_RPAREN);
    if (!param_ty || !ret_ty)
        return NULL;
    return fir_func_ty(param_ty, ret_ty);
}

static inline const struct fir_node* parse_tup_ty(struct parser* parser) {
    eat_token(parser, TOK_TUP_TY);

    bool valid_ops = true;
    struct small_node_vec ops;
    small_node_vec_init(&ops);

    if (accept_token(parser, TOK_LPAREN)) {
        while (parser->state.ahead->tag != TOK_RPAREN) {
            const struct fir_node* op = parse_ty(parser);
            valid_ops &= op != NULL;
            small_node_vec_push(&ops, &op);
            if (!accept_token(parser, TOK_COMMA))
                break;
        }
        expect_token(parser, TOK_RPAREN);
    }

    const struct fir_node* tup_ty = valid_ops ? fir_tup_ty(parser->mod, ops.elems, ops.elem_count) : NULL;
    small_node_vec_destroy(&ops);
    return tup_ty;
}

static inline const struct fir_node* parse_ty(struct parser* parser) {
    switch (parser->state.ahead->tag) {
        case TOK_ARRAY_TY:    return parse_array_ty(parser);
        case TOK_DYNARRAY_TY: return parse_dynarray_ty(parser);
        case TOK_FUNC_TY:     return parse_func_ty(parser);
        case TOK_TUP_TY:      return parse_tup_ty(parser);
        case TOK_FRAME_TY:    return next_token(parser), fir_frame_ty(parser->mod);
        case TOK_CTRL_TY:     return next_token(parser), fir_ctrl_ty(parser->mod);
        case TOK_NORET_TY:    return next_token(parser), fir_noret_ty(parser->mod);
        case TOK_MEM_TY:      return next_token(parser), fir_mem_ty(parser->mod);
        case TOK_PTR_TY:      return next_token(parser), fir_ptr_ty(parser->mod);
        case TOK_INT_TY:
        case TOK_FLOAT_TY:
            return parse_int_or_float_ty(parser);
        default:
            invalid_token(parser, "type");
            return NULL;
    }
}

static inline struct str_view parse_ident(struct parser* parser) {
    struct str_view ident = token_str_view(parser->state.lexer.data, parser->state.ahead);
    expect_token(parser, TOK_IDENT);
    return ident;
}

static inline const struct fir_node* parse_node_body(struct parser*, const struct fir_node*);

static inline const struct fir_node* parse_op(struct parser* parser) {
    if (token_tag_is_ty_tag(parser->state.ahead->tag)) {
        const struct fir_node* ty = parse_ty(parser);
        if (!ty)
            return NULL;
        if (token_tag_is_node_tag(parser->state.ahead->tag))
            return parse_node_body(parser, ty);
        return ty;
    }

    struct fir_source_range ident_range = parser->state.ahead->source_range;
    struct str_view ident = parse_ident(parser);
    const struct fir_node* const* symbol = symbol_table_find(&parser->symbol_table, &ident);
    if (!symbol) {
        unknown_identifier(parser, &ident_range, ident);
        return NULL;
    }
    return *symbol;
}

static inline union fir_node_data parse_node_data(
    struct parser* parser,
    enum fir_node_tag tag,
    const struct fir_node* ty)
{
    union fir_node_data data = {};
    if (tag == FIR_CONST || fir_node_tag_has_fp_flags(tag) || fir_node_tag_has_mem_flags(tag)) {
        expect_token(parser, TOK_LBRACKET);
        if (tag == FIR_CONST && ty->tag == FIR_INT_TY) {
            data.int_val = parse_int_val(parser) & make_bitmask(ty->data.bitwidth);
        } else if (tag == FIR_CONST && ty->tag == FIR_FLOAT_TY) {
            data.float_val = parse_float_val(parser);
        } else if (fir_node_tag_has_fp_flags(tag)) {
            data.fp_flags = parse_fp_flags(parser);
        } else {
            assert(fir_node_tag_has_mem_flags(tag));
            data.mem_flags = parse_mem_flags(parser);
        }
        expect_token(parser, TOK_RBRACKET);
    }
    return data;
}

static inline void skip_parens(struct parser* parser) {
    size_t paren_depth = 1;
    while (paren_depth > 0) {
        if (parser->state.ahead->tag == TOK_LPAREN)
            paren_depth++;
        else if (parser->state.ahead->tag == TOK_RPAREN)
            paren_depth--;
        next_token(parser);
    }
}

static inline struct fir_node* parse_nominal_node(
    struct parser* parser,
    enum fir_node_tag tag,
    const struct fir_node* ty,
    const union fir_node_data* data)
{
    struct fir_node* nominal_node = fir_node_clone(
        parser->mod, &(struct fir_node) { .tag = tag, .data = *data }, ty);
    if (accept_token(parser, TOK_LPAREN)) {
        delayed_nominal_node_vec_push(&parser->delayed_nominal_nodes, &(struct delayed_nominal_node) {
            .nominal_node = nominal_node,
            .parser_state = parser->state
        });
        skip_parens(parser);
    }
    return nominal_node;
}

static inline const struct fir_node* parse_node_body(struct parser* parser, const struct fir_node* ty) {
    bool is_external = accept_token(parser, TOK_EXTERN);
    if (!token_tag_is_node_tag(parser->state.ahead->tag)) {
        invalid_token(parser, "node tag");
        return NULL;
    }

    enum fir_node_tag tag = (enum fir_node_tag)parser->state.ahead->tag;
    next_token(parser);
    if (is_external && !fir_node_tag_can_be_external(tag)) {
        log_error(&parser->log,
            &parser->state.ahead->source_range,
            "node cannot be external");
    }

    union fir_node_data data = parse_node_data(parser, tag, ty);
    if (fir_node_tag_is_nominal(tag)) {
        struct fir_node* nominal_node = parse_nominal_node(parser, tag, ty, &data);
        if (is_external)
            fir_node_make_external(nominal_node);
        return nominal_node;
    }

    bool valid_ops = true;
    struct small_node_vec ops;
    small_node_vec_init(&ops);
    if (accept_token(parser, TOK_LPAREN)) {
        while (parser->state.ahead->tag != TOK_RPAREN) {
            const struct fir_node* op = parse_op(parser);
            valid_ops &= op != NULL;
            small_node_vec_push(&ops, &op);
            if (!accept_token(parser, TOK_COMMA))
                break;
        }
        expect_token(parser, TOK_RPAREN);
    }

    if (!valid_ops)
        return NULL;

    const struct fir_node* ctrl = NULL;
    if (accept_token(parser, TOK_AT))
        ctrl = parse_op(parser);

    const struct fir_node* node = fir_node_rebuild(parser->mod, tag, &data, ctrl, ty, ops.elems, ops.elem_count);
    assert(node->ty == ty);

    small_node_vec_destroy(&ops);
    return node;
}

static inline const struct fir_node* parse_node(struct parser* parser) {
    const struct fir_node* ty = parse_ty(parser);
    if (!ty)
        return NULL;

    struct fir_source_range ident_range = parser->state.ahead->source_range;
    struct str_view ident = parse_ident(parser);
    expect_token(parser, TOK_EQ);

    const struct fir_node* node = parse_node_body(parser, ty);
    if (node && !symbol_table_insert(&parser->symbol_table, &ident, &node)) {
        const struct fir_node* const* symbol = symbol_table_find(&parser->symbol_table, &ident);
        assert(symbol);
        if (node != *symbol) {
            log_error(&parser->log,
                &ident_range,
                "identifier '%.*s' already exists",
                (int)ident.length, ident.data);
        }
    }

    return node;
}

static inline void parse_delayed_nominal_nodes(struct parser* parser) {
    VEC_FOREACH(const struct delayed_nominal_node, delayed_nominal_node, parser->delayed_nominal_nodes) {
        parser->state = delayed_nominal_node->parser_state;
        size_t i = 0;
        while (parser->state.ahead->tag != TOK_RPAREN) {
            const struct fir_node* op = parse_op(parser);
            if (op)
                fir_node_set_op(delayed_nominal_node->nominal_node, i++, op);
            if (!accept_token(parser, TOK_COMMA))
                break;
        }
        expect_token(parser, TOK_RPAREN);
    }
}

static inline void parse_header(struct parser* parser) {
    if (!accept_token(parser, TOK_MOD))
        return;

    struct str_view name = token_str_view(parser->state.lexer.data, parser->state.ahead);
    if (name.length >= 2)
        name = str_view_shrink(name, 1, 1);
    expect_token(parser, TOK_STR);
    fir_mod_set_name_with_length(parser->mod, name.data, name.length);
}

bool fir_mod_parse(struct fir_mod* mod, const struct fir_parse_input* input) {
    bool disable_colors = input->error_log ? !is_terminal(input->error_log) : true;
    struct parser parser = {
        .mod = mod,
        .log = {
            .file = input->error_log,
            .disable_colors = disable_colors,
            .max_errors = SIZE_MAX,
            .source_name = input->file_name,
            .source_data = {
                .data = input->file_data,
                .length = input->file_size
            }
        },
        .dbg_pool = input->dbg_pool,
        .delayed_nominal_nodes = delayed_nominal_node_vec_create(),
        .symbol_table = symbol_table_create(),
        .state.lexer = lexer_create(input->file_data, input->file_size),
    };

    for (size_t i = 0; i < TOKEN_LOOKAHEAD; ++i)
        next_token(&parser);

    parse_header(&parser);
    while (parser.state.ahead->tag != TOK_EOF)
        parse_node(&parser);

    parse_delayed_nominal_nodes(&parser);

    delayed_nominal_node_vec_destroy(&parser.delayed_nominal_nodes);
    symbol_table_destroy(&parser.symbol_table);
    return parser.log.error_count == 0;
}
