#pragma once

struct test_context {
    int write_pipe;
    unsigned asserts_passed;
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
            context->asserts_passed++; \
    } while (false)

[[noreturn]]
void fail_test(
    struct test_context*,
    const char* msg,
    const char* file,
    unsigned line);

void register_test(const char* name, void (*) (struct test_context*));
