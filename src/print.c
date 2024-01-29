#include "fir/node.h"
#include "fir/module.h"

#include "support/term.h"
#include "support/io.h"

#include "analysis/scope.h"
#include "analysis/cfg.h"
#include "analysis/schedule.h"

#include <inttypes.h>

static inline void print_indent(FILE* file, size_t indent, const char* tab) {
    for (size_t i = 0; i < indent; ++i)
        fputs(tab, file);
}

static void print_fp_flags(FILE* file, enum fir_fp_flags flags) {
    if (flags & FIR_FP_FINITE_ONLY)    fprintf(file, "+fo");
    if (flags & FIR_FP_NO_SIGNED_ZERO) fprintf(file, "+nsz");
    if (flags & FIR_FP_ASSOCIATIVE)    fprintf(file, "+a");
    if (flags & FIR_FP_DISTRIBUTIVE)   fprintf(file, "+d");
}
static void print_mem_flags(FILE* file, enum fir_mem_flags flags) {
    if (flags & FIR_MEM_NON_NULL) fprintf(file, "+nn");
    if (flags & FIR_MEM_VOLATILE) fprintf(file, "+v");
}

static void print_node(FILE* file, const struct fir_node* node, const struct fir_print_options* options) {
    const char* value_style   = options->disable_colors ? "" : TERM2(TERM_FG_GREEN, TERM_BOLD);
    const char* type_style    = options->disable_colors ? "" : TERM1(TERM_FG_BLUE);
    const char* keyword_style = type_style;
    const char* reset_style   = options->disable_colors ? "" : TERM1(TERM_RESET);
    const char* data_style    = options->disable_colors ? "" : TERM1(TERM_FG_CYAN);
    const char* error_style   = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD);
    if (fir_node_is_external(node))
        fprintf(file, "%sextern%s ", keyword_style, reset_style);
    fprintf(file, "%s%s%s",
        fir_node_is_ty(node) ? type_style : value_style,
        fir_node_tag_to_string(node->tag),
        reset_style);
    if (fir_node_has_bitwidth(node))
        fprintf(file, "[%s%zu%s]", data_style, node->data.bitwidth, reset_style);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        fprintf(file, "[%s%"PRIu64"%s]", data_style, node->data.int_val, reset_style);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        fprintf(file, "[%s%a%s]", data_style, node->data.float_val, reset_style);
    else if (node->tag == FIR_ARRAY_TY)
        fprintf(file, "[%s%zu%s]", data_style, node->data.array_dim, reset_style);
    else if (fir_node_has_mem_flags(node)) {
        fprintf(file, "[%s", data_style);
        print_mem_flags(file, node->data.mem_flags);
        fprintf(file, "%s]", reset_style);
    } else if (fir_node_has_fp_flags(node)) {
        fprintf(file, "[%s", data_style);
        print_fp_flags(file, node->data.fp_flags);
        fprintf(file, "%s]", reset_style);
    }
    if (node->op_count == 0)
        return;
    fprintf(file, "(");
    for (size_t i = 0; i < node->op_count; ++i) {
        if (!node->ops[i])
            fprintf(file, "%s<unset>%s", error_style, reset_style);
        else if (fir_node_is_ty(node->ops[i]))
            print_node(file, node->ops[i], options);
        else
            fprintf(file, "%s_%"PRIu64, fir_node_name(node->ops[i]), node->ops[i]->id);
        if (i != node->op_count - 1)
            fprintf(file, ", ");
    }
    fprintf(file, ")");
}

void fir_node_print(FILE* file, const struct fir_node* node, const struct fir_print_options* options) {
    print_indent(file, options->indent, options->tab);
    if (!fir_node_is_ty(node)) {
        if (options->verbosity != FIR_VERBOSITY_COMPACT) {
            print_node(file, node->ty, options);
            fprintf(file, " ");
        }
        fprintf(file, "%s_%"PRIu64" = ", fir_node_name(node), node->id);
    }
    print_node(file, node, options);
}

struct fir_print_options fir_print_options_default(FILE* file) {
    return (struct fir_print_options) {
        .tab = "    ",
        .disable_colors = !is_terminal(file),
        .verbosity = FIR_VERBOSITY_MEDIUM
    };
}

void fir_node_dump(const struct fir_node* node) {
    struct fir_print_options options = fir_print_options_default(stdout);
    fir_node_print(stdout, node, &options);
    printf("\n");
    fflush(stdout);
}

void fir_mod_print(FILE* file, const struct fir_mod* mod, const struct fir_print_options* options) {
    struct fir_node* const* globals = fir_mod_globals(mod);
    struct fir_node* const* funcs = fir_mod_funcs(mod);
    size_t func_count = fir_mod_func_count(mod);
    size_t global_count = fir_mod_global_count(mod);

    for (size_t i = 0; i < global_count; ++i) {
        print_indent(file, options->indent, options->tab);
        fir_node_print(file, globals[i], options);
        fprintf(file, "\n");
    }

    for (size_t i = 0; i < func_count; ++i) {
        if (funcs[i]->ty->ops[1]->tag == FIR_NORET_TY)
            continue;

        print_indent(file, options->indent, options->tab);
        fir_node_print(file, funcs[i], options);
        fprintf(file, "\n");
        if (!funcs[i]->ops[0])
            continue;

        struct scope scope = scope_create(funcs[i]);
        struct cfg cfg = cfg_create(&scope);
        struct schedule schedule = schedule_create(&cfg);

        VEC_REV_FOREACH(struct graph_node*, block_ptr, cfg.post_order) {
            if ((*block_ptr) == cfg.graph.sink)
                continue;

            const struct fir_node* block_func = cfg_block_func(*block_ptr);
            assert(block_func);

            print_indent(file, options->indent + 1, options->tab);
            fir_node_print(file, block_func, options);
            fprintf(file, "\n");

            struct const_node_span block_contents = schedule_block_contents(&schedule, *block_ptr);
            CONST_SPAN_FOREACH(const struct fir_node*, node_ptr, block_contents) {
                print_indent(file, options->indent + 2, options->tab);
                fir_node_print(file, *node_ptr, options);
                fprintf(file, "\n");
            }
            fprintf(file, "\n");
        }

        print_indent(file, options->indent + 1, options->tab);
        fir_node_print(file, funcs[i]->ops[0], options);
        fprintf(file, "\n");

        schedule_destroy(&schedule);
        scope_destroy(&scope);
        cfg_destroy(&cfg);
    }
}

void fir_mod_dump(const struct fir_mod* mod) {
    struct fir_print_options options = fir_print_options_default(stdout);
    fir_mod_print(stdout, mod, &options);
    fflush(stdout);
}
