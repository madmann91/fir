#include "parser.h"
#include "ast.h"
#include "types.h"

#include "support/io.h"
#include "support/str.h"
#include "support/log.h"
#include "support/mem_pool.h"

#include <fir/module.h>

struct options {
    bool disable_colors;
    bool disable_cleanup;
    bool print_ast;
    bool print_ir;
};

static void usage(void) {
    printf(
        "usage: ash [options] files...\n"
        "options:\n"
        "  -h  --help        Shows this message.\n"
        "      --no-color    Disables colors in the output.\n"
        "      --no-cleanup  Do not clean up the module after emitting it.\n"
        "      --print-ast   Prints the AST on the standard output.\n"
        "      --print-ir    Prints the IR on the standard output.\n");
}

static bool parse_options(int argc, char** argv, struct options* options) {
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                usage();
                return false;
            } else if (!strcmp(argv[i], "--no-color")) {
                options->disable_colors = true;
            } else if (!strcmp(argv[i], "--no-cleanup")) {
                options->disable_cleanup = true;
            } else if (!strcmp(argv[i], "--print-ast")) {
                options->print_ast = true;
            } else if (!strcmp(argv[i], "--print-ir")) {
                options->print_ir = true;
            } else {
                fprintf(stderr, "invalid option '%s'\n", argv[i]);
                return false;
            }
        } else {
            file_count++;
        }
    }
    if (file_count == 0) {
        fprintf(stderr, "no input file\n");
        return false;
    }
    return true;
}

static bool compile_file(const char* file_name, const struct options* options) {
    size_t file_size = 0;
    char* file_data = read_file(file_name, &file_size);
    if (!file_data) {
        fprintf(stderr, "cannot open '%s'\n", file_name);
        return false;
    }

    struct log log = {
        .file = stderr,
        .max_errors = SIZE_MAX,
        .disable_colors = options->disable_colors || !is_terminal(stderr),
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

    if (options->print_ast)
        ast_dump(program);

    mod = fir_mod_create(file_name);
    ast_emit(program, mod);
    if (!options->disable_cleanup)
        fir_mod_cleanup(mod);
    if (options->print_ir)
        fir_mod_dump(mod);

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
    if (!parse_options(argc, argv, &options))
        return 1;

    bool status = true;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-')
            continue;
        status &= compile_file(argv[i], &options);
    }

    return status ? 0 : 1;
}
