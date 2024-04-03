#include "fir/node.h"
#include "fir/module.h"

#include "support/term.h"
#include "support/io.h"

#include "analysis/scope.h"
#include "analysis/cfg.h"
#include "analysis/schedule.h"

#include <inttypes.h>

struct print_styles {
    const char* error_style;
    const char* value_style;
    const char* type_style;
    const char* keyword_style;
    const char* reset_style;
    const char* data_style;
};

static inline struct print_styles print_styles_from_options(const struct fir_print_options* options) {
    return (struct print_styles) {
        .error_style   = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD),
        .value_style   = options->disable_colors ? "" : TERM2(TERM_FG_GREEN, TERM_BOLD),
        .type_style    = options->disable_colors ? "" : TERM1(TERM_FG_BLUE),
        .keyword_style = options->disable_colors ? "" : TERM2(TERM_BOLD, TERM_FG_YELLOW),
        .reset_style   = options->disable_colors ? "" : TERM1(TERM_RESET),
        .data_style    = options->disable_colors ? "" : TERM1(TERM_FG_CYAN),
    };
}

static void print_node(FILE*, const struct fir_node*, const struct fir_print_options*);

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

static void print_node_name(FILE* file, const struct fir_node* node) {
    fprintf(file, "%s_%"PRIu64, fir_node_name(node), node->id);
}

static void print_op(FILE* file, const struct fir_node* op, const struct fir_print_options* options) {
    struct print_styles print_styles = print_styles_from_options(options);
    if (!op)
        fprintf(file, "%s<unset>%s", print_styles.error_style, print_styles.reset_style);
    else if (!fir_node_is_nominal(op) && (op->props & FIR_PROP_INVARIANT) != 0) {
        if (!fir_node_is_ty(op)) {
            print_node(file, op->ty, options);
            fprintf(file, " ");
        }
        print_node(file, op, options);
    } else
        print_node_name(file, op);
}

static void print_node(FILE* file, const struct fir_node* node, const struct fir_print_options* options) {
    struct print_styles print_styles = print_styles_from_options(options);
    if (fir_node_is_external(node))
        fprintf(file, "%sextern%s ", print_styles.keyword_style, print_styles.reset_style);
    fprintf(file, "%s%s%s",
        fir_node_is_ty(node) ? print_styles.type_style : print_styles.value_style,
        fir_node_tag_to_string(node->tag),
        print_styles.reset_style);
    if (fir_node_has_bitwidth(node))
        fprintf(file, "[%s%zu%s]", print_styles.data_style, node->data.bitwidth, print_styles.reset_style);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        fprintf(file, "[%s%"PRIu64"%s]", print_styles.data_style, node->data.int_val, print_styles.reset_style);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        fprintf(file, "[%s%a%s]", print_styles.data_style, node->data.float_val, print_styles.reset_style);
    else if (node->tag == FIR_ARRAY_TY)
        fprintf(file, "[%s%zu%s]", print_styles.data_style, node->data.array_dim, print_styles.reset_style);
    else if (fir_node_has_mem_flags(node)) {
        fprintf(file, "[%s", print_styles.data_style);
        print_mem_flags(file, node->data.mem_flags);
        fprintf(file, "%s]", print_styles.reset_style);
    } else if (fir_node_has_fp_flags(node)) {
        fprintf(file, "[%s", print_styles.data_style);
        print_fp_flags(file, node->data.fp_flags);
        fprintf(file, "%s]", print_styles.reset_style);
    }
    if (node->op_count == 0)
        return;
    fprintf(file, "(");
    for (size_t i = 0; i < node->op_count; ++i) {
        print_op(file, node->ops[i], options);
        if (i != node->op_count - 1)
            fprintf(file, ", ");
    }
    fprintf(file, ")");
    if (node->ctrl && options->verbosity != FIR_VERBOSITY_COMPACT) {
        fprintf(file, "@");
        print_op(file, node->ctrl, options);
    }
}

void fir_node_print(FILE* file, const struct fir_node* node, const struct fir_print_options* options) {
    print_indent(file, options->indent, options->tab);
    if (!fir_node_is_ty(node)) {
        if (options->verbosity != FIR_VERBOSITY_COMPACT) {
            print_node(file, node->ty, options);
            fprintf(file, " ");
        }
        print_node_name(file, node);
        fprintf(file, " = ");
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
    const char* keyword_style = options->disable_colors ? "" : TERM1(TERM_FG_BLUE);
    const char* comment_style = options->disable_colors ? "" : TERM2(TERM_FG_CYAN, TERM_ITALIC);
    const char* reset_style   = options->disable_colors ? "" : TERM1(TERM_RESET);

    fprintf(file, "%smod%s \"%.*s\"\n\n",
        keyword_style, reset_style,
        (int)strlen(fir_mod_name(mod)), fir_mod_name(mod));

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

            print_indent(file, options->indent + 1, options->tab);
            fir_node_print(file, cfg_block_func(*block_ptr), options);
            fprintf(file, "\n");
        }
        fprintf(file, "\n");

        struct node_vec* block_contents = xmalloc(sizeof(struct node_vec) * cfg.graph.node_count);
        for (size_t i = 0; i < cfg.graph.node_count; ++i)
            block_contents[i] = node_vec_create();
        schedule_list_block_contents(&schedule, block_contents);

        VEC_REV_FOREACH(struct graph_node*, block_ptr, cfg.post_order) {
            if ((*block_ptr) == cfg.graph.sink)
                continue;

            const struct fir_node* block_func = cfg_block_func(*block_ptr);
            print_indent(file, options->indent + 1, options->tab);
            fprintf(file, "%s#", comment_style);
            print_node_name(file, block_func);
            fprintf(file, ": %s\n", reset_style);

            VEC_FOREACH(const struct fir_node*, node_ptr, block_contents[(*block_ptr)->index]) {
                print_indent(file, options->indent + 2, options->tab);
                fir_node_print(file, *node_ptr, options);
                fprintf(file, "\n");
            }
            fprintf(file, "\n");
        }

        for (size_t i = 0; i < cfg.graph.node_count; ++i)
            node_vec_destroy(&block_contents[i]);
        free(block_contents);
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
