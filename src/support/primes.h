#pragma once

#include <stddef.h>

#define MIN_PRIME 7
#define MAX_PRIME 1048583
#define PRIMES(f) \
    f(MIN_PRIME) \
    f(17) \
    f(31) \
    f(67) \
    f(257) \
    f(1031) \
    f(4093) \
    f(8191) \
    f(16381) \
    f(32381) \
    f(65539) \
    f(131071) \
    f(262147) \
    f(524287) \
    f(MAX_PRIME)

static inline size_t next_prime(size_t i) {
#define f(x) if (i <= x) return x;
    PRIMES(f)
    return i;
#undef f
}

static inline size_t mod_prime(size_t i, size_t p) {
    switch (p) {
#define f(x) case x: return i % x;
    PRIMES(f)
#undef f
        default: return i % p;
    }
}
