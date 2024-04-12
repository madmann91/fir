#include "parser.h"
#include "ast.h"
#include "types.h"

#include <overture/file.h>
#include <overture/path.h>
#include <overture/cli.h>
#include <overture/str.h>
#include <overture/log.h>
#include <overture/term.h>
#include <overture/mem_pool.h>

#include <fir/module.h>

struct options {
    bool disable_colors;
    bool disable_cleanup;
    bool is_verbose;
    bool print_ast;
    bool print_ir;
};

static enum cli_state usage(void*, char*) {
    printf(
        "usage: ash [options] files...\n"
        "options:\n"
        "  -h  --help               Shows this message.\n"
        "  -v  --verbose            Makes the output verbose.\n"
        "      --no-color           Disables colors in the output.\n"
        "      --no-cleanup         Do not clean up the module after emitting it.\n"
        "      --print-ast          Prints the AST on the standard output.\n"
        "      --print-ir           Prints the IR on the standard output.\n");
    return CLI_STATE_ERROR;
}

static inline struct str make_mod_name(const char* file_name) {
    struct str mod_name = str_create();
    str_append(&mod_name, trim_ext(skip_dir(STR_VIEW(file_name))));
    return mod_name;
}

static bool compile_file(const char* file_name, const struct options* options) {
    size_t file_size = 0;
    char* file_data = file_read(file_name, &file_size);
    if (!file_data) {
        fprintf(stderr, "cannot open '%s'\n", file_name);
        return false;
    }

    struct log log = {
        .file = stderr,
        .max_errors = SIZE_MAX,
        .disable_colors = options->disable_colors || !is_term(stderr),
        .source_name = file_name,
        .source_data = (struct str_view) { .data = file_data, .length = file_size }
    };
    struct mem_pool mem_pool = mem_pool_create();

    bool status = true;
    struct type_set* type_set = NULL;
    struct fir_mod* mod = NULL;

    struct ast* program = parse_file(file_data, file_size, &mem_pool, &log);
    if (log.error_count != 0)
        goto error;

    ast_bind(program, &log);
    if (log.error_count != 0)
        goto error;

    type_set = type_set_create();
    ast_check(program, &mem_pool, type_set, &log);
    if (log.error_count != 0)
        goto error;

    if (options->print_ast) {
        ast_print(stdout, program, &(struct ast_print_options) {
            .tab = "    ",
            .disable_colors = options->disable_colors || !is_term(stdout),
            .print_casts = options->is_verbose
        });
    }

    struct str mod_name = make_mod_name(file_name);
    mod = fir_mod_create(str_terminate(&mod_name));
    str_destroy(&mod_name);
    ast_emit(program, mod);
    if (!options->disable_cleanup)
        fir_mod_cleanup(mod);
    if (options->print_ir) {
        fir_mod_print(stdout, mod, &(struct fir_mod_print_options) {
            .tab = "    ",
            .verbosity = options->is_verbose ? FIR_VERBOSITY_HIGH : FIR_VERBOSITY_MEDIUM,
            .disable_colors = options->disable_colors || !is_term(stdout)
        });
    }

    goto done;

error:
    status = false;
done:
    if (type_set)
        type_set_destroy(type_set);
    if (mod)
        fir_mod_destroy(mod);
    mem_pool_destroy(&mem_pool);
    free(file_data);
    return status;
}

int main(int argc, char** argv) {
    struct options options = {};
    struct cli_option cli_options [] = {
        { .short_name = "-h", .long_name = "--help", .parse = usage },
        cli_flag(NULL, "--no-color",   &options.disable_colors),
        cli_flag(NULL, "--no-cleanup", &options.disable_cleanup),
        cli_flag(NULL, "--print-ir",   &options.print_ir),
        cli_flag(NULL, "--print-ast",  &options.print_ast),
        cli_flag("-v", "--verbose",    &options.is_verbose)
    };
    if (!cli_parse_options(argc, argv, cli_options, sizeof(cli_options) / sizeof(cli_options[0])))
        return 1;

    bool status = true;
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        status &= compile_file(argv[i], &options);
        file_count++;
    }

    if (file_count == 0) {
        fprintf(stderr, "no input file\n");
        return 1;
    }

    return status ? 0 : 1;
}
