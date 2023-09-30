#pragma once

#include "support/vec.h"

#include <stddef.h>
#include <stdbool.h>

struct options;
struct test_context;

enum test_status {
    TEST_PASSED,
    TEST_FAILED,
    TEST_SEGFAULT
};

struct test {
    const char* name;
    void (*test_func) (struct test_context*);
    bool enabled;
    enum test_status status;
    size_t passed_asserts;

#ifndef TEST_DISABLE_FORK
    pid_t pid;
    int read_pipe;
#endif
};

VEC_DECL(test_vec, struct test, PUBLIC)

extern struct test_vec tests;

void run_tests(void);
