#include <fir/module.h>
#include <fir/version.h>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static void usage(void) {
    printf(
        "usage: fir [options] file.fir ...\n"
        "options:\n"
        "    -h    --help        Shows this message.\n"
        "          --version     Shows version information.\n"
        "          --no-cleanup  Do not clean up the module after loading it.\n");
}

static void version(void) {
    printf("fir %"PRIu32".%"PRIu32".%"PRIu32" %"PRIu32"\n",
        fir_version_major(),
        fir_version_minor(),
        fir_version_patch(),
        fir_version_timestamp());
    printf("See LICENSE.txt for licensing and copyright information.\n");
}

struct options {
    bool no_cleanup;
};

static bool parse_options(int argc, char** argv, struct options* options) {
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                usage();
                return false;
            } else if (!strcmp(argv[i], "--version")) {
                version();
                return false;
            } else if (!strcmp(argv[i], "--no-cleanup")) {
                options->no_cleanup = true;
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

static inline char* read_file(const char* file_name) {
    FILE* file = fopen(file_name, "rb");
    if (!file)
        return NULL;

    size_t capacity = 1024;
    char* data = malloc(capacity);
    size_t size = 0;

    while (true) {
        size_t to_read = capacity - size;
        if (to_read == 0) {
            capacity += capacity >> 1;
            data = realloc(data, capacity);
            to_read = capacity - size;
        }
        size_t read = fread(data + size, 1, to_read, file);
        size += read;
        if (read < to_read)
            break;
    }
    fclose(file);

    data = realloc(data, size + 1);
    data[size] = 0;
    return data;
}

static inline bool compile_file(const char* file_name, const struct options* options) {
    char* file_data = read_file(file_name);
    if (!file_data) {
        fprintf(stderr, "cannot open file '%s'\n", file_name);
        return false;
    }
    struct fir_mod* mod = fir_mod_create(file_name);
    bool status = fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = file_name,
        .file_data = file_data,
        .file_size = strlen(file_data),
        .error_log = stderr
    });
    free(file_data);
    if (!options->no_cleanup)
        fir_mod_cleanup(mod);
    fir_mod_dump(mod);
    fir_mod_destroy(mod);
    return status;
}

int main(int argc, char** argv) {
    struct options options = {};
    if (!parse_options(argc, argv, &options))
        return 1;

    bool status = true;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-')
            status &= compile_file(argv[i], &options);
    }

    return status ? 0 : 1;
}
