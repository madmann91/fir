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

MAP_DEFINE(symbol_table, struct str_view, const struct fir_node*, str_view_hash, str_view_cmp, PRIVATE)

struct delayed_op {
    size_t op_index;
    struct str_view op_name;
    struct fir_source_range source_range;
    struct fir_node* nominal_node;
};

VEC_DEFINE(delayed_op_vec, struct delayed_op, PRIVATE)

struct parser {
    struct fir_mod* mod;
    struct fir_dbg_info_pool* dbg_pool;
    struct symbol_table symbol_table;
    struct delayed_op_vec delayed_ops;
    struct lexer lexer;
    struct token ahead[TOKEN_LOOKAHEAD];
    struct log log;
};

static inline void next_token(struct parser* parser) {
    for (size_t i = 1; i < TOKEN_LOOKAHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->ahead[TOKEN_LOOKAHEAD - 1] = lexer_advance(&parser->lexer);
}

static inline void eat_token(struct parser* parser, [[maybe_unused]] enum token_tag tag) {
    assert(parser->ahead[0].tag == tag);
    next_token(parser);
}

static inline bool accept_token(struct parser* parser, enum token_tag tag) {
    if (parser->ahead->tag == tag) {
        next_token(parser);
        return true;
    }
    return false;
}

static inline bool expect_token(struct parser* parser, enum token_tag tag) {
    if (!accept_token(parser, tag)) {
        struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
        log_error(&parser->log,
            &parser->ahead->source_range,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str_view.length, str_view.data);
        return false;
    }
    return true;
}

static inline void invalid_token(struct parser* parser, const char* msg) {
    struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
    log_error(&parser->log,
        &parser->ahead->source_range,
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

static inline void invalid_fp_flag(
    struct parser* parser,
    const struct fir_source_range* source_range,
    struct str_view fp_flag)
{
    log_error(&parser->log, source_range,
        "invalid floating-point flag '%.*s'",
        (int)fp_flag.length, fp_flag.data);
}

static inline void invalid_linkage(
    struct parser* parser,
    const struct fir_source_range* source_range,
    struct str_view linkage)
{
    log_error(&parser->log, source_range,
        "invalid linkage '%.*s'",
        (int)linkage.length, linkage.data);
}

static inline const struct fir_node* parse_ty(struct parser*);

static inline uint64_t parse_int_val(struct parser* parser) {
    uint64_t int_val = parser->ahead->int_val;
    expect_token(parser, TOK_INT);
    return int_val;
}

static inline double parse_float_val(struct parser* parser) {
    double float_val = parser->ahead->float_val;
    expect_token(parser, TOK_FLOAT);
    return float_val;
}

static inline enum fir_linkage parse_linkage(struct parser* parser) {
    enum fir_linkage linkage = FIR_LINKAGE_INTERNAL;
    struct str_view ident = token_str_view(parser->lexer.data, parser->ahead);
    if (str_view_cmp(&ident, &STR_VIEW("internal")))      linkage = FIR_LINKAGE_INTERNAL;
    else if (str_view_cmp(&ident, &STR_VIEW("imported"))) linkage = FIR_LINKAGE_IMPORTED;
    else if (str_view_cmp(&ident, &STR_VIEW("exported"))) linkage = FIR_LINKAGE_EXPORTED;
    else invalid_linkage(parser, &parser->ahead->source_range, ident);
    expect_token(parser, TOK_IDENT);
    return linkage;
}

static inline enum fir_fp_flags parse_fp_flags(struct parser* parser) {
    enum fir_fp_flags fp_flags = FIR_FP_STRICT;
    while (accept_token(parser, TOK_PLUS)) {
        struct str_view ident = token_str_view(parser->lexer.data, parser->ahead);
        if (str_view_cmp(&ident, &STR_VIEW("fo")))       fp_flags |= FIR_FP_FINITE_ONLY;
        else if (str_view_cmp(&ident, &STR_VIEW("nsz"))) fp_flags |= FIR_FP_NO_SIGNED_ZERO;
        else if (str_view_cmp(&ident, &STR_VIEW("a")))   fp_flags |= FIR_FP_ASSOCIATIVE;
        else if (str_view_cmp(&ident, &STR_VIEW("d")))   fp_flags |= FIR_FP_DISTRIBUTIVE;
        else invalid_fp_flag(parser, &parser->ahead->source_range, ident);
        expect_token(parser, TOK_IDENT);
    }
    return fp_flags;
}

static inline uint64_t parse_array_dim(struct parser* parser) {
    return parse_int_val(parser);
}

static inline uint32_t parse_bitwidth(struct parser* parser) {
    return parse_int_val(parser);
}

static inline const struct fir_node* parse_int_or_float_ty(struct parser* parser) {
    bool is_int = parser->ahead->tag == TOK_INT_TY;
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
    return fir_array_ty(elem_ty, array_dim);
}

static inline const struct fir_node* parse_dynarray_ty(struct parser* parser) {
    eat_token(parser, TOK_DYNARRAY_TY);
    expect_token(parser, TOK_LPAREN);
    const struct fir_node* elem_ty = parse_ty(parser);
    expect_token(parser, TOK_RPAREN);
    return fir_dynarray_ty(elem_ty);
}

static inline const struct fir_node* parse_func_ty(struct parser* parser) {
    eat_token(parser, TOK_FUNC_TY);
    expect_token(parser, TOK_LPAREN);
    const struct fir_node* param_ty = parse_ty(parser);
    expect_token(parser, TOK_COMMA);
    const struct fir_node* ret_ty = parse_ty(parser);
    expect_token(parser, TOK_RPAREN);
    return fir_func_ty(param_ty, ret_ty);
}

static inline const struct fir_node* parse_tup_ty(struct parser* parser) {
    eat_token(parser, TOK_TUP_TY);
    struct small_node_vec ops;
    small_node_vec_init(&ops);
    expect_token(parser, TOK_LPAREN);
    while (parser->ahead->tag != TOK_RPAREN) {
        const struct fir_node* op = parse_ty(parser);
        small_node_vec_push(&ops, &op);
        if (!accept_token(parser, TOK_COMMA))
            break;
    }
    expect_token(parser, TOK_RPAREN);
    const struct fir_node* tup_ty = fir_tup_ty(parser->mod, ops.elems, ops.elem_count);
    small_node_vec_destroy(&ops);
    return tup_ty;
}

static inline const struct fir_node* parse_ty(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_ARRAY_TY:    return parse_array_ty(parser);
        case TOK_DYNARRAY_TY: return parse_dynarray_ty(parser);
        case TOK_FUNC_TY:     return parse_func_ty(parser);
        case TOK_TUP_TY:      return parse_tup_ty(parser);
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
    struct str_view ident = token_str_view(parser->lexer.data, parser->ahead);
    expect_token(parser, TOK_IDENT);
    return ident;
}

static inline const struct fir_node* parse_op(
    struct parser* parser,
    struct fir_node* nominal_node,
    size_t op_index)
{
    if (token_tag_is_ty(parser->ahead->tag))
        return parse_ty(parser);
    struct fir_source_range ident_range = parser->ahead->source_range; 
    struct str_view ident = parse_ident(parser);
    const struct fir_node* const* symbol = symbol_table_find(&parser->symbol_table, &ident);
    if (symbol)
        return *symbol;
    if (nominal_node) {
        delayed_op_vec_push(&parser->delayed_ops, &(struct delayed_op) {
            .nominal_node = nominal_node,
            .op_index = op_index,
            .source_range = ident_range,
            .op_name = ident
        });
    } else {
        unknown_identifier(parser, &ident_range, ident);
    }
    return NULL;
}

static inline union fir_node_data parse_node_data(
    struct parser* parser,
    enum fir_node_tag tag,
    const struct fir_node* ty)
{
    union fir_node_data data = {};
    if (tag == FIR_CONST || fir_node_tag_has_fp_flags(tag)) {
        expect_token(parser, TOK_LBRACKET);
        if (tag == FIR_CONST && ty->tag == FIR_INT_TY) {
            data.int_val = parse_int_val(parser) & make_bitmask(ty->data.bitwidth);
        } else if (tag == FIR_CONST && ty->tag == FIR_FLOAT_TY) {
            data.float_val = parse_float_val(parser);
        } else {
            assert(fir_node_tag_has_fp_flags(tag));
            data.fp_flags = parse_fp_flags(parser);
        }
        expect_token(parser, TOK_RBRACKET);
    } else if (fir_node_tag_is_nominal(tag) && accept_token(parser, TOK_LBRACKET)) {
        data.linkage = parse_linkage(parser);
        expect_token(parser, TOK_RBRACKET);
    }
    return data;
}

static inline const struct fir_node* parse_node_body(
    struct parser* parser,
    const struct fir_node* ty)
{
    if (!token_tag_is_node_tag(parser->ahead->tag)) {
        invalid_token(parser, "node tag");
        return NULL;
    }

    enum fir_node_tag tag = (enum fir_node_tag)parser->ahead->tag;
    next_token(parser);

    union fir_node_data data = parse_node_data(parser, tag, ty);

    struct fir_node* nominal_node = NULL;
    if (fir_node_tag_is_nominal(tag)) {
        nominal_node = fir_node_clone(
            parser->mod, &(struct fir_node) {
                .tag = tag,
                .data.linkage = data.linkage
            }, ty);
    }

    bool valid_ops = true;
    size_t op_count = 0;
    struct small_node_vec ops;
    small_node_vec_init(&ops);
    if (accept_token(parser, TOK_LPAREN)) {
        while (parser->ahead->tag != TOK_RPAREN) {
            const struct fir_node* op = parse_op(parser, nominal_node, op_count);
            if (!nominal_node) {
                small_node_vec_push(&ops, &op);
                valid_ops &= op != NULL;
            } else if (op) {
                fir_node_set_op(nominal_node, op_count, op);
            }
            op_count++;
            if (!accept_token(parser, TOK_COMMA))
                break;
        }
        expect_token(parser, TOK_RPAREN);
    }

    if (nominal_node || !valid_ops) {
        small_node_vec_destroy(&ops);
        return nominal_node;
    }

    const struct fir_node* node = fir_node_rebuild(
        parser->mod,
        &(struct fir_node) {
            .tag = tag,
            .data = data,
            .op_count = ops.elem_count
        }, ty, ops.elems);
    assert(node->ty == ty);

    small_node_vec_destroy(&ops);
    return node;
}

static inline const struct fir_node* parse_node(struct parser* parser) {
    const struct fir_node* ty = parse_ty(parser);
    if (!ty)
        return NULL;
    struct fir_source_range ident_range = parser->ahead->source_range;
    struct str_view ident = parse_ident(parser);
    expect_token(parser, TOK_EQ);
    const struct fir_node* body = parse_node_body(parser, ty);
    if (body) {
        if (!symbol_table_insert(&parser->symbol_table, &ident, &body)) {
            log_error(&parser->log,
                &ident_range,
                "identifier '%.*s' already exists",
                (int)ident.length, ident.data);
        }
    }
    return body;
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
        .delayed_ops = delayed_op_vec_create(),
        .symbol_table = symbol_table_create(),
        .lexer = lexer_create(input->file_data, input->file_size)
    };

    for (size_t i = 0; i < TOKEN_LOOKAHEAD; ++i)
        next_token(&parser);

    while (parser.ahead->tag != TOK_EOF)
        parse_node(&parser);

    VEC_FOREACH(const struct delayed_op, delayed_op, parser.delayed_ops) {
        const struct fir_node* const* op = symbol_table_find(&parser.symbol_table, &delayed_op->op_name);
        if (op)
            fir_node_set_op(delayed_op->nominal_node, delayed_op->op_index, *op);
        else
            unknown_identifier(&parser, &delayed_op->source_range, delayed_op->op_name);
    }

    delayed_op_vec_destroy(&parser.delayed_ops);
    symbol_table_destroy(&parser.symbol_table);
    return parser.log.error_count == 0;
}
