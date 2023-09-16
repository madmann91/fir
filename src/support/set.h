#pragma once

#include "htable.h"

#include <string.h>

#define SET_DEFAULT_CAPACITY 4

#define FOREACH_SET(elem_ty, elem, set) \
    for (size_t \
        very_long_prefix_i = 0, \
        very_long_prefix_once = 0; \
        very_long_prefix_i < (set).htable.capacity; \
        ++very_long_prefix_i, very_long_prefix_once = 0) \
        for (const elem_ty* elem = &((const elem_ty*)(set).htable.keys)[very_long_prefix_i]; \
            very_long_prefix_once == 0 && htable_is_bucket_occupied(&(set).htable, very_long_prefix_i); \
            very_long_prefix_once = 1) \

#define DECL_SET(name, elem_ty, hash, cmp) \
    struct name { \
        struct htable htable; \
    }; \
    static inline struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .htable = htable_create(sizeof(elem_ty), 0, capacity) \
        }; \
    } \
    static inline struct name name##_create(void) { \
        return name##_create_with_capacity(SET_DEFAULT_CAPACITY); \
    } \
    static inline void name##_destroy(struct name* set) { \
        htable_destroy(&set->htable); \
    } \
    static inline void name##_rehash(struct name*); \
    static inline bool name##_insert(struct name* set, elem_ty const* elem) { \
        struct htable* htable = &set->htable; \
        uint32_t h = hash(elem) | HTABLE_OCCUPIED_FLAG; \
        size_t idx = htable_first_bucket(htable, h); \
        for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) { \
            if (htable->hashes[idx] == h && cmp(((elem_ty*)htable->keys) + idx, elem)) \
                return false; \
        } \
        htable->elem_count++; \
        htable->hashes[idx] = h; \
        memcpy(((elem_ty*)htable->keys) + idx, elem, sizeof(elem_ty)); \
        if (htable_needs_rehash(htable)) \
            name##_rehash(set); \
        return true; \
    } \
    static inline elem_ty const* name##_find(const struct name* set, elem_ty const* elem) { \
        const struct htable* htable = &set->htable; \
        uint32_t h = hash(elem) | HTABLE_OCCUPIED_FLAG; \
        size_t idx = htable_first_bucket(htable, h); \
        for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) { \
            if (htable->hashes[idx] == h && cmp(((elem_ty*)htable->keys) + idx, elem)) \
                return ((elem_ty*)htable->keys) + idx; \
        } \
        return NULL; \
    } \
    static inline void name##_rehash(struct name* set) { \
        struct name copy = name##_create_with_capacity(htable_grow_capacity(set->htable.capacity)); \
        for (size_t i = 0; i < set->htable.capacity; ++i) { \
            if (!htable_is_bucket_occupied(&set->htable, i)) \
                continue; \
            uint32_t h = set->htable.hashes[i]; \
            size_t idx = htable_first_bucket(&copy.htable, h); \
            while (htable_is_bucket_occupied(&copy.htable, idx)) \
                idx = htable_next_bucket(&copy.htable, idx); \
            copy.htable.hashes[idx] = h; \
            copy.htable.elem_count++; \
            memcpy(((elem_ty*)copy.htable.keys) + idx, ((elem_ty*)set->htable.keys) + i, sizeof(elem_ty)); \
        } \
        name##_destroy(set); \
        *set = copy; \
    }
