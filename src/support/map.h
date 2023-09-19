#pragma once

#include "htable.h"
#include "linkage.h"

#include <string.h>

#define MAP_DEFAULT_CAPACITY 4

#define FOREACH_MAP_KEY(key_ty, key, map) \
    for (size_t \
        very_long_prefix_i = 0, \
        very_long_prefix_once = 0; \
        very_long_prefix_i < (map).htable.capacity; \
        ++very_long_prefix_i, very_long_prefix_once = 0) \
        for (key_ty const* key = &((key_ty const*)(map).htable.keys)[very_long_prefix_i]; \
            very_long_prefix_once == 0 && htable_is_bucket_occupied(&(map).htable, very_long_prefix_i); \
            very_long_prefix_once = 1) \

#define FOREACH_MAP(key_ty, key, val_ty, val, map) \
    FOREACH_MAP_KEY(key_ty, key, map) \
        for (val_ty const* val = &((val_ty const*)(map).htable.vals)[very_long_prefix_i]; \
            very_long_prefix_once == 0; \
            very_long_prefix_once = 1)

#define DEF_MAP(name, key_ty, val_ty, hash, cmp, linkage) \
    DECL_MAP(name, key_ty, val_ty, linkage) \
    IMPL_MAP(name, key_ty, val_ty, hash, cmp, linkage)

#define DECL_MAP(name, key_ty, val_ty, linkage) \
    struct name { \
        struct htable htable; \
    }; \
    LINKAGE(linkage) struct name name##_create_with_capacity(size_t); \
    LINKAGE(linkage) struct name name##_create(void); \
    LINKAGE(linkage) void name##_destroy(struct name*); \
    LINKAGE(linkage) void name##_rehash(struct name*); \
    LINKAGE(linkage) bool name##_insert(struct name*, key_ty const*, val_ty const*); \
    LINKAGE(linkage) val_ty const* name##_find(const struct name*, key_ty const*);

#define IMPL_MAP(name, key_ty, val_ty, hash, cmp, linkage) \
    LINKAGE(linkage) struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .htable = htable_create(sizeof(key_ty), sizeof(val_ty), capacity) \
        }; \
    } \
    LINKAGE(linkage) struct name name##_create(void) { \
        return name##_create_with_capacity(MAP_DEFAULT_CAPACITY); \
    } \
    LINKAGE(linkage) void name##_destroy(struct name* map) { \
        htable_destroy(&map->htable); \
    } \
    LINKAGE(linkage) void name##_rehash(struct name*); \
    LINKAGE(linkage) bool name##_insert(struct name* map, key_ty const* key, val_ty const* val) { \
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
    LINKAGE(linkage) val_ty const* name##_find(const struct name* map, key_ty const* key) { \
        const struct htable* htable = &map->htable; \
        uint32_t h = hash(key) | HTABLE_OCCUPIED_FLAG; \
        size_t idx = htable_first_bucket(htable, h); \
        for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) { \
            if (htable->hashes[idx] == h && cmp(((key_ty*)htable->keys) + idx, key)) \
                return ((val_ty*)htable->vals) + idx; \
        } \
        return NULL; \
    } \
    LINKAGE(linkage) void name##_rehash(struct name* map) { \
        struct name copy = name##_create_with_capacity(htable_rehashed_capacity(&map->htable)); \
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
