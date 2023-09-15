#pragma once

#include "htable.h"

#include <string.h>

#define MAP_DEFAULT_CAPACITY 4

#define FOREACH_MAP_KEY(key_ty, key, map) \
    for (size_t \
        very_long_prefix_i = 0, \
        very_long_prefix_once = 0; \
        very_long_prefix_i < (map).htable.capacity; \
        ++very_long_prefix_i, very_long_prefix_once = 0) \
        for (const key_ty* key = &((const key_ty*)(map).htable.keys)[very_long_prefix_i]; \
            very_long_prefix_once == 0 && htable_is_bucket_occupied(&(map).htable, very_long_prefix_i); \
            very_long_prefix_once = 1) \

#define FOREACH_MAP(key_ty, key, val_ty, val, map) \
    FOREACH_MAP_KEY(key_ty, key, map) \
        for (const val_ty* val = &((const val_ty*)(map).htable.vals)[very_long_prefix_i]; \
            very_long_prefix_once == 0; \
            very_long_prefix_once = 1)

#define DECL_MAP(name, key_ty, val_ty, hash, cmp) \
    struct name { \
        struct htable htable; \
    }; \
    static inline struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .htable = htable_create(sizeof(key_ty), sizeof(val_ty), capacity) \
        }; \
    } \
    static inline struct name name##_create(void) { \
        return name##_create_with_capacity(MAP_DEFAULT_CAPACITY); \
    } \
    static inline void name##_destroy(struct name* map) { \
        htable_destroy(&map->htable); \
    } \
    static inline void name##_rehash(struct name*); \
    static inline bool name##_insert(struct name* map, key_ty const* key, val_ty const* val) { \
        struct htable* htable = &map->htable; \
        uint32_t h = hash(key) | HTABLE_OCCUPIED_FLAG; \
        size_t idx = htable_first_bucket(htable, h); \
        for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) { \
            if (htable->hashes[idx] == h && cmp(((key_ty*)htable->keys) + idx, key)) \
                return false; \
        } \
        htable->elem_count++; \
        htable->hashes[idx] = h; \
        memcpy(((key_ty*)htable->keys) + idx, key, sizeof(key_ty)); \
        memcpy(((val_ty*)htable->vals) + idx, val, sizeof(val_ty)); \
        if (htable_needs_rehash(htable)) \
            name##_rehash(map); \
        return true; \
    } \
    static inline val_ty const* name##_find(const struct name* map, key_ty const* key) { \
        const struct htable* htable = &map->htable; \
        uint32_t h = hash(key) | HTABLE_OCCUPIED_FLAG; \
        size_t idx = htable_first_bucket(htable, h); \
        for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) { \
            if (htable->hashes[idx] == h && cmp(((key_ty*)htable->keys) + idx, key)) \
                return ((val_ty*)htable->vals) + idx; \
        } \
        return NULL; \
    } \
    static inline void name##_rehash(struct name* map) { \
        struct name copy = name##_create_with_capacity(htable_grow_capacity(map->htable.capacity)); \
        for (size_t i = 0; i < map->htable.capacity; ++i) { \
            if (!htable_is_bucket_occupied(&map->htable, i)) \
                continue; \
            uint32_t h = map->htable.hashes[i]; \
            size_t idx = htable_first_bucket(&copy.htable, h); \
            while (htable_is_bucket_occupied(&copy.htable, idx)) \
                idx = htable_next_bucket(&copy.htable, idx); \
            copy.htable.hashes[idx] = h; \
            copy.htable.elem_count++; \
            memcpy(((key_ty*)copy.htable.keys) + idx, ((key_ty*)map->htable.keys) + i, sizeof(key_ty)); \
            memcpy(((val_ty*)copy.htable.vals) + idx, ((val_ty*)map->htable.vals) + i, sizeof(val_ty)); \
        } \
        name##_destroy(map); \
        *map = copy; \
    }
