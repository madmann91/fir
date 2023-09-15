#pragma once

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline uint64_t make_bitmask(size_t bits) {
    assert(bits <= 64);
    return (bits == 64 ? 0 : (UINT64_C(1) << bits)) - 1;
}

static inline uint64_t double_bits(double x) {
    static_assert(sizeof(uint64_t) == sizeof(double));
    uint64_t y;
    memcpy(&y, &x, sizeof(x));
    return y;
}
