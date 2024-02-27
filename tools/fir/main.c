#include <fir/module.h>
#include <fir/version.h>
#include <fir/codegen.h>

#include "support/io.h"
#include "support/term.h"
#include "support/cli.h"
#include "support/str.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static bool usage(void*, char*) {
    printf(
        "usage: fir [options] file.fir ...\n"
        "options:\n"
        "  -h  --help               Shows this message.\n"
        "      --version            Shows version information.\n"
        "  -v  --verbose            Makes the output verbose.\n"
        "      --no-color           Disables colors in the output.\n"
        "      --no-cleanup         Do not clean up the module after loading it.\n"
        "      --codegen <name>     Selects the given code generator.\n");
    return false;
}

static bool version(void*, char*) {
#define HIGH TERM2(TERM_FG_WHITE, TERM_BOLD)
#define MED TERM2(TERM_FG_YELLOW, TERM_BOLD)
#define LOW TERM2(TERM_FG_RED, TERM_BOLD)
#define RESET TERM1(TERM_RESET)
	printf(
		LOW "               .             \n" RESET
		LOW "       .       ...           \n" RESET
		LOW "       ...     ....          \n" RESET
		LOW "    .   ..... .....   .        ///////////////// " MED "   ////\n" RESET
		LOW "    ..   ........... ...         ////         // " MED "   ////\n" RESET
		LOW "    .... ................        ////\n" RESET
		LOW "  . ........ ... ........        ////       //   " MED " //////    " HIGH " ////// //////\n" RESET
		LOW "  ..... ...  ..   .......  .     /////////////   " MED "    ///    " HIGH "    /////   //\n" RESET
		LOW " . ....       .   . ........     ////       //   " MED "    ///    " HIGH "    ///\n" RESET
		LOW " .. . ..               .....     ////            " MED "    ///    " HIGH "    ///\n" RESET
		LOW " ....                  .....     ////            " MED "    ///    " HIGH "    ///\n" RESET
		LOW "  ....                .....      ////            " MED "    ///    " HIGH "    ///        ////\n" RESET
		LOW "   ....              ....       //////           " MED "   /////   " HIGH "   /////       ////\n\n" RESET);
#undef RED
#undef RES

    printf("fir %"PRIu32".%"PRIu32".%"PRIu32" %"PRIu32"\n",
        fir_version_major(),
        fir_version_minor(),
        fir_version_patch(),
        fir_version_timestamp());
    printf("See LICENSE.txt for licensing and copyright information.\n");
    return false;
}

struct options {
    char* codegen;
    bool disable_cleanup;
    bool disable_colors;
    bool is_verbose;
};

static enum fir_codegen_tag codegen_tag_from_string(const char* name) {
    if (!strcmp(name, "llvm"))
        return FIR_CODEGEN_LLVM;
    if (!strcmp(name, "dummy"))
        return FIR_CODEGEN_DUMMY;
    return FIR_CODEGEN_DUMMY;
}

static inline bool generate_code(struct fir_mod* mod, const struct options* options) {
    struct fir_codegen* codegen = fir_codegen_create(codegen_tag_from_string(options->codegen), NULL, 0);
    if (!codegen) {
        fprintf(stderr, "code generator '%s' is not supported\n", options->codegen);
        return false;
    }

    struct str output_file = str_create();
    str_printf(&output_file, "%s.o", fir_mod_name(mod));
    bool status = fir_codegen_run(codegen, mod, str_terminate(&output_file));
    str_destroy(&output_file);
    fir_codegen_destroy(codegen);
    return status;
}

static inline bool compile_file(const char* file_name, const struct options* options) {
    size_t file_size = 0;
    char* file_data = read_file(file_name, &file_size);
    if (!file_data) {
        fprintf(stderr, "cannot open file '%s'\n", file_name);
        return false;
    }
    struct fir_mod* mod = fir_mod_create(file_name);
    bool status = fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = file_name,
        .file_data = file_data,
        .file_size = file_size,
        .error_log = stderr
    });
    free(file_data);
    if (!options->disable_cleanup)
        fir_mod_cleanup(mod);

    struct fir_print_options print_options = fir_print_options_default(stdout);
    print_options.disable_colors |= options->disable_colors;
    print_options.verbosity = options->is_verbose ? FIR_VERBOSITY_HIGH : FIR_VERBOSITY_MEDIUM;
    fir_mod_print(stdout, mod, &print_options);

    status &= generate_code(mod, options);

    fir_mod_destroy(mod);
    return status;
}

int main(int argc, char** argv) {
    struct options options = { .codegen = "dummy" };

    struct cli_option cli_options[] = {
        { .short_name = "-h", .long_name = "--help", .parse = usage },
        { .long_name = "--version", .parse = version },
        cli_option_string(NULL, "--codegen", &options.codegen),
        cli_flag(NULL, "--no-color",   &options.disable_colors),
        cli_flag(NULL, "--no-cleanup", &options.disable_cleanup),
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
