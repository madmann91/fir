#include "macros.h"

#include "support/vec.h"
#include "support/term.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef ENABLE_REGEX
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

struct test {
    const char* name;
    void (*test_func) (struct test_context*);
    bool enabled;
    pid_t pid;
    int read_pipe;
};

DECL_VEC(test_vec, struct test)

static struct test_vec tests = {};

void fail_test(
    struct test_context* context,
    const char* msg,
    const char* file,
    unsigned line)
{
    write(context->write_pipe, context, sizeof(struct test_context));
    close(context->write_pipe);
    fprintf(stderr, "Assertion '%s' failed (%s:%u)\n", msg, file, line);
    abort();
}

void register_test(const char* name, void (*test_func) (struct test_context*)) {
    test_vec_push(&tests, &(struct test) {
        .name = name,
        .test_func = test_func
    });
}

static inline struct test* find_test_by_pid(pid_t pid) {
    FOREACH_VEC(struct test, test, tests) {
        if (test->pid == pid)
            return test;
    }
    return NULL;
}

static void start_test(struct test* test) {
    int pipes[2];
    pipe(pipes);

    pid_t pid = fork();
    if (pid == 0) {
        close(pipes[0]);
        struct test_context context = {
            .write_pipe = pipes[1]
        };
        test->test_func(&context);
        write(context.write_pipe, &context, sizeof(struct test_context));
        close(context.write_pipe);
        test_vec_destroy(&tests);
        exit(0);
    }

    close(pipes[1]);
    test->pid = pid;
    test->read_pipe = pipes[0];
}

static void print_tests() {
    FOREACH_VEC(struct test, test, tests) {
        printf("%s\n", test->name);
    }
}

static void usage() {
    printf(
        "usage: testdriver [options] filters ...\n"
        "options:\n"
        "   -h    --help       Shows this message\n"
#ifdef ENABLE_REGEX
        "         --regex      Allow the use of PERL-style regular expressions in filters\n"
#endif
        "         --no-color   Turns of the use of color in the output\n"
        "         --list       Lists all tests and exit\n");
}

struct options {
    bool enable_filtering;
    bool disable_colors;
#ifdef ENABLE_REGEX
    bool regex_match;
#endif
};

static bool parse_options(int argc, char** argv, struct options* options) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                usage();
                return false;
            } else if (!strcmp(argv[i], "--list")) {
                print_tests();
                return false;
            } else if (!strcmp(argv[i], "--no-color")) {
                options->disable_colors = true;
#ifdef ENABLE_REGEX
            } else if (!strcmp(argv[i], "--regex")) {
                options->regex_match = true;
#endif
            } else {
                fprintf(stderr, "invalid option '%s'\n", argv[i]);
                return false;
            }
        } else {
            options->enable_filtering = true;
        }
    }
    return true;
}

#ifdef ENABLE_REGEX
static pcre2_code* compile_regex(const char* pattern) {
    int err;
    PCRE2_SIZE err_off;
    pcre2_code* code = pcre2_compile((PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED, 0, &err, &err_off, NULL);
    if (!code) {
        PCRE2_UCHAR8 err_buf[256];
        pcre2_get_error_message(err, err_buf, sizeof(err_buf));
        fprintf(stderr, "could not compile pattern %s into regex: %s\n", pattern, err_buf);
        return NULL;
    }
    return code;
}
#endif

static void filter_tests(int argc, char** argv, const struct options* options) {
    if (!options->enable_filtering) {
        FOREACH_VEC(struct test, test, tests) { test->enabled = true; }
        return;
    }

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-')
            continue;
#ifdef ENABLE_REGEX
        if (options->regex_match) {
            pcre2_code* code = compile_regex(argv[i]);
            if (!code)
                continue;
            pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(code, NULL);
            FOREACH_VEC(struct test, test, tests) {
                test->enabled =
                    pcre2_match(code, (PCRE2_SPTR8)test->name, strlen(test->name), 0, 0, match_data, NULL) >= 0;
            }
            pcre2_match_data_free(match_data);
            pcre2_code_free(code);
        } else {
#else
        {
#endif
            FOREACH_VEC(struct test, test, tests) {
                test->enabled = !strcmp(test->name, argv[i]);
            }
        }
    }
}

static const char* color_code(bool success) {
    return success
        ? TERM2(TERM_FG_GREEN, TERM_BOLD)
        : TERM2(TERM_FG_RED, TERM_BOLD);
}

int main(int argc, char** argv) {
    size_t passed = 0;
    size_t failed = 0;
    size_t asserts_passed = 0;

    struct options options = { .disable_colors = !isatty(fileno(stdout)) };
    if (!parse_options(argc, argv, &options))
        return 1;

    filter_tests(argc, argv, &options);

    size_t enabled_tests = 0;
    FOREACH_VEC(struct test, test, tests) {
        enabled_tests += test->enabled ? 1 : 0;
    }

    printf("running %zu test(s):\n\n", enabled_tests);
    // Must flush to avoid flushing previously-buffered content in the forked children
    fflush(stdout);

    FOREACH_VEC(struct test, test, tests) {
        if (test->enabled)
            start_test(test);
    }

    for (size_t i = 0; i < enabled_tests; ++i) {
        int status;
        pid_t test_pid = wait(&status);

        struct test* test = find_test_by_pid(test_pid);
        if (!test)
            continue;

        bool success = false;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            success = true;

        passed += success ? 1 : 0;
        failed += success ? 0 : 1;

        const char* msg = success ? "[PASSED]" : "[FAILED]";
        const char* color = color_code(success);
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV)
            msg = "[SEGFAULT]";
        printf(" %10s .............................. %s%s%s\n", test->name,
            options.disable_colors ? "" : color, msg,
            options.disable_colors ? "" : TERM1(TERM_RESET));

        struct test_context context;
        read(test->read_pipe, &context, sizeof(struct test_context));
        close(test->read_pipe);

        asserts_passed += context.asserts_passed;
    }
    test_vec_destroy(&tests);
    printf("\n%s%zu/%zu tests passed, %zu asserts passed%s\n",
        options.disable_colors ? "" : color_code(failed == 0),
        passed, passed + failed, asserts_passed,
        options.disable_colors ? "" : TERM1(TERM_RESET));
    return failed == 0 ? 0 : 1;
}
