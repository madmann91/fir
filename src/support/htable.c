#include "htable.h"

#include "primes.h"
#include "alloc.h"

#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

struct htable htable_create(size_t key_size, size_t val_size, size_t init_capacity) {
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

void htable_destroy(struct htable* htable) {
    free(htable->hashes);
    free(htable->vals);
    free(htable->keys);
    memset(htable, 0, sizeof(struct htable));
}

size_t htable_first_bucket(const struct htable* htable, uint32_t hash_val) {
    return mod_prime(hash_val, htable->capacity);
}

size_t htable_next_bucket(const struct htable* htable, size_t bucket_idx) {
    return bucket_idx + 1 < htable->capacity ? bucket_idx + 1 : 0;
}

bool htable_is_bucket_occupied(const struct htable* htable, size_t bucket_idx) {
    return (htable->hashes[bucket_idx] & HTABLE_OCCUPIED_FLAG) != 0;
}

bool htable_needs_rehash(const struct htable* htable) {
    return htable->elem_count * 100 >= htable->capacity * HTABLE_MAX_LOAD_FACTOR;
}

size_t htable_grow_capacity(size_t capacity) {
    return capacity < MAX_PRIME ? next_prime(capacity + 1) : capacity * 2 + 1;
}

void htable_empty_bucket(const struct htable* htable, size_t bucket_idx) {
    htable->hashes[bucket_idx] = 0;
}
