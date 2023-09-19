#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "primes.h"
#include "alloc.h"

struct htable {
    size_t capacity;
    size_t elem_count;
    uint32_t* hashes;
    void* keys;
    void* vals;
};

#define HTABLE_OCCUPIED_FLAG UINT32_C(0x80000000)
#define HTABLE_MAX_LOAD_FACTOR 70 //%

static inline struct htable htable_create(size_t key_size, size_t val_size, size_t init_capacity) {
    init_capacity = next_prime(init_capacity);
    void* vals = val_size > 0 ? xmalloc(val_size * init_capacity) : NULL;
    return (struct htable) {
        .capacity = init_capacity,
        .elem_count = 0,
        .hashes = xcalloc(init_capacity, sizeof(uint32_t)),
        .keys = xmalloc(key_size * init_capacity),
        .vals = vals
    };
}

static inline void htable_destroy(struct htable* htable) {
    free(htable->hashes);
    free(htable->vals);
    free(htable->keys);
    memset(htable, 0, sizeof(struct htable));
}

static inline size_t htable_first_bucket(const struct htable* htable, uint32_t hash_val) {
    return mod_prime(hash_val, htable->capacity);
}

static inline size_t htable_rehashed_capacity(const struct htable* htable) {
    return htable->capacity < MAX_PRIME
        ? next_prime(htable->capacity + 1)
        : htable->capacity + (htable->capacity >> 1);
}

static inline bool htable_needs_rehash(const struct htable* htable) {
    return htable->elem_count * 100 >= htable->capacity * HTABLE_MAX_LOAD_FACTOR;
}

static inline size_t htable_next_bucket(const struct htable* htable, size_t bucket_idx) {
    return bucket_idx + 1 < htable->capacity ? bucket_idx + 1 : 0;
}

static inline bool htable_is_bucket_occupied(const struct htable* htable, size_t bucket_idx) {
    return (htable->hashes[bucket_idx] & HTABLE_OCCUPIED_FLAG) != 0;
}
