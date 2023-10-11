#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "primes.h"
#include "alloc.h"

struct htable {
    size_t capacity;
    uint32_t* hashes;
    char* keys;
    char* vals;
};

#define HTABLE_OCCUPIED_FLAG UINT32_C(0x80000000)
#define HTABLE_MAX_LOAD_FACTOR 70 //%

[[nodiscard]] static inline struct htable htable_create(size_t key_size, size_t val_size, size_t init_capacity) {
    init_capacity = next_prime(init_capacity);
    char* vals = val_size > 0 ? xmalloc(val_size * init_capacity) : NULL;
    return (struct htable) {
        .capacity = init_capacity,
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

static inline size_t htable_next_bucket(const struct htable* htable, size_t bucket_idx) {
    return bucket_idx + 1 < htable->capacity ? bucket_idx + 1 : 0;
}

static inline bool htable_is_bucket_occupied(const struct htable* htable, size_t bucket_idx) {
    return (htable->hashes[bucket_idx] & HTABLE_OCCUPIED_FLAG) != 0;
}

static inline bool htable_needs_rehash(const struct htable* htable, size_t elem_count) {
    return elem_count * 100 >= htable->capacity * HTABLE_MAX_LOAD_FACTOR;
}

static inline void htable_clear(struct htable* htable) {
    memset(htable->hashes, 0, sizeof(uint32_t) * htable->capacity);
}

static inline void htable_rehash(
    struct htable* htable,
    size_t key_size,
    size_t val_size,
    size_t capacity)
{
    struct htable copy = htable_create(key_size, val_size, capacity);
    for (size_t i = 0; i < htable->capacity; ++i) {
        if (!htable_is_bucket_occupied(htable, i))
            continue;
        uint32_t hash = htable->hashes[i];
        size_t idx = mod_prime(hash, copy.capacity);
        while (htable_is_bucket_occupied(&copy, idx))
            idx = htable_next_bucket(&copy, idx);
        copy.hashes[idx] = hash;
        memcpy(copy.keys + idx * key_size, htable->keys + i * key_size, key_size);
        memcpy(copy.vals + idx * val_size, htable->vals + i * val_size, val_size);
    }
    htable_destroy(htable);
    *htable = copy;
}

static inline void htable_grow(
    struct htable* htable,
    size_t key_size,
    size_t val_size)
{
    size_t next_capacity = htable->capacity < MAX_PRIME
        ? next_prime(htable->capacity + 1)
        : htable->capacity + (htable->capacity >> 1);
    htable_rehash(htable, key_size, val_size, next_capacity);
}

static inline bool htable_insert(
    struct htable* htable,
    const void* key,
    const void* val,
    size_t key_size,
    size_t val_size,
    uint32_t hash,
    bool (*cmp) (const void*, const void*))
{
    hash |= HTABLE_OCCUPIED_FLAG;
    size_t idx = mod_prime(hash, htable->capacity);
    for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) {
        if (htable->hashes[idx] == hash && cmp(htable->keys + idx * key_size, key))
            return false;
    }
    htable->hashes[idx] = hash;
    memcpy(htable->keys + idx * key_size, key, key_size);
    memcpy(htable->vals + idx * val_size, val, val_size);
    return true;
}

static inline bool htable_find(
    const struct htable* htable,
    size_t* found_idx,
    const void* key,
    size_t key_size,
    uint32_t hash,
    bool (*cmp) (const void*, const void*))
{
    hash |= HTABLE_OCCUPIED_FLAG;
    size_t idx = mod_prime(hash, htable->capacity);
    for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) {
        if (htable->hashes[idx] == hash && cmp(htable->keys + idx * key_size, key)) {
            *found_idx = idx;
            return true;
        }
    }
    return false;
}

static inline bool htable_remove(
    struct htable* htable,
    const void* key,
    size_t key_size,
    size_t val_size,
    uint32_t hash,
    bool (*cmp) (const void*, const void*))
{
    hash |= HTABLE_OCCUPIED_FLAG;
    size_t idx;
    if (!htable_find(htable, &idx, key, key_size, hash, cmp))
        return false;

    size_t next_idx = htable_next_bucket(htable, idx);
    while (htable_is_bucket_occupied(htable, next_idx)) {
        uint32_t next_hash = htable->hashes[next_idx];
        size_t ideal_next_idx = mod_prime(next_hash, htable->capacity);
        if (ideal_next_idx <= idx || ideal_next_idx > next_idx) {
            memcpy(htable->keys + idx * key_size, htable->keys + next_idx * key_size, key_size);
            memcpy(htable->vals + idx * val_size, htable->vals + next_idx * val_size, val_size);
            htable->hashes[idx] = htable->hashes[next_idx];
            idx = next_idx;
        }
        next_idx = htable_next_bucket(htable, next_idx);
    }
    htable->hashes[idx] = 0;
    return true;
}
