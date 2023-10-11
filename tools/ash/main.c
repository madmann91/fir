#include "parser.h"
#include "bind.h"
#include "ast.h"
#include "check.h"
#include "types.h"

#include "support/io.h"
#include "support/str.h"
#include "support/log.h"
#include "support/mem_pool.h"

struct options {
    bool disable_colors;
};

static void usage(void) {
    printf(
        "usage: ash [options] files...\n"
        "options:\n"
        "  -h  --help      Shows this message.\n"
        "      --no-color  Disables colors in the output.\n");
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
    struct ast* program = parse_file(file_data, file_size, &mem_pool, &log);
    if (log.error_count != 0)
        goto error;

    bind_program(program, &log);
    if (log.error_count != 0)
        goto error;

    type_set = type_set_create();
    check_program(program, type_set, &log);
    if (log.error_count != 0)
        goto error;

    ast_dump(program);
    goto done;

error:
    status = false;
done:
    if (type_set)
        type_set_destroy(type_set);
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
