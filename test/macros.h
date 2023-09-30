#pragma once

#include <stddef.h>

#ifdef TEST_DISABLE_FORK
#include <setjmp.h>
#endif

struct test_context {
#ifndef TEST_DISABLE_FORK
    int write_pipe;
#else
    jmp_buf buf;
#endif
    size_t passed_asserts;
};

#define TEST(name) \
    void test_##name(struct test_context*); \
    __attribute__((constructor)) void register_##name() { register_test(#name, test_##name); } \
    void test_##name([[maybe_unused]] struct test_context* context)

#define REQUIRE(x) \
    do { \
        if (!(x)) \
            fail_test(context, #x, __FILE__, __LINE__); \
        else \
            context->passed_asserts++; \
    } while (false)

[[noreturn]]
void fail_test(
    struct test_context*,
    const char* msg,
    const char* file,
    unsigned line);

void register_test(const char* name, void (*) (struct test_context*));
