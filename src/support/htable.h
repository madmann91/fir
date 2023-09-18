#pragma once

#include <stddef.h>
#include <stdint.h>

struct htable {
    size_t capacity;
    size_t elem_count;
    uint32_t* hashes;
    void* keys;
    void* vals;
};

#define HTABLE_OCCUPIED_FLAG UINT32_C(0x80000000)
#define HTABLE_MAX_LOAD_FACTOR 70 //%

struct htable htable_create(size_t key_size, size_t val_size, size_t init_capacity);
void htable_destroy(struct htable*);

size_t htable_first_bucket(const struct htable*, uint32_t hash_val);
size_t htable_next_bucket(const struct htable*, size_t bucket_idx);
bool htable_is_bucket_occupied(const struct htable*, size_t bucket_idx);
bool htable_needs_rehash(const struct htable*);
size_t htable_grow_capacity(size_t);
void htable_empty_bucket(const struct htable*, size_t bucket_idx);
