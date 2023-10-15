#pragma once

#include "htable.h"
#include "visibility.h"

#include <string.h>

#define MAP_DEFAULT_CAPACITY 4
#define MAP_PREFIX map_very_long_prefix_

#define MAP_FOREACH_KEY(key_ty, key, map) \
    for (size_t MAP_PREFIX##i = 0; MAP_PREFIX##i < (map).htable.capacity; ++MAP_PREFIX##i) \
        if (htable_is_bucket_occupied(&(map).htable, MAP_PREFIX##i)) \
            for (bool MAP_PREFIX##once = true; MAP_PREFIX##once; MAP_PREFIX##once = false) \
                for (key_ty const* key = &((key_ty const*)(map).htable.keys)[MAP_PREFIX##i]; MAP_PREFIX##once; MAP_PREFIX##once = false) \

#define MAP_FOREACH(key_ty, key, val_ty, val, map) \
    MAP_FOREACH_KEY(key_ty, key, map) \
        for (val_ty const* val = &((val_ty const*)(map).htable.vals)[MAP_PREFIX##i]; MAP_PREFIX##once; MAP_PREFIX##once = false) \

#define MAP_DEFINE(name, key_ty, val_ty, hash, cmp, vis) \
    MAP_DECL(name, key_ty, val_ty, vis) \
    MAP_IMPL(name, key_ty, val_ty, hash, cmp, vis)

#define MAP_DECL(name, key_ty, val_ty, vis) \
    struct name { \
        struct htable htable; \
        size_t elem_count; \
    }; \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create_with_capacity(size_t); \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create(void); \
    VISIBILITY(vis) void name##_destroy(struct name*); \
    VISIBILITY(vis) void name##_clear(struct name*); \
    VISIBILITY(vis) bool name##_insert(struct name*, key_ty const*, val_ty const*); \
    VISIBILITY(vis) val_ty const* name##_find(const struct name*, key_ty const*); \
    VISIBILITY(vis) bool name##_remove(struct name*, key_ty const*);

#define MAP_IMPL(name, key_ty, val_ty, hash, cmp, vis) \
    static inline bool name##_cmp_wrapper(const void* left, const void* right) { \
        return cmp((key_ty const*)left, (key_ty const*)right); \
    } \
    VISIBILITY(vis) struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .htable = htable_create(sizeof(key_ty), sizeof(val_ty), capacity) \
        }; \
    } \
    VISIBILITY(vis) struct name name##_create(void) { \
        return name##_create_with_capacity(MAP_DEFAULT_CAPACITY); \
    } \
    VISIBILITY(vis) void name##_destroy(struct name* map) { \
        htable_destroy(&map->htable); \
    } \
    VISIBILITY(vis) void name##_clear(struct name* map) { \
        htable_clear(&map->htable); \
        map->elem_count = 0; \
    } \
    VISIBILITY(vis) bool name##_insert(struct name* map, key_ty const* key, val_ty const* val) { \
        if (htable_insert(&map->htable, key, val, sizeof(key_ty), sizeof(val_ty), hash(hash_init(), key), name##_cmp_wrapper)) { \
            if (htable_needs_rehash(&map->htable, map->elem_count)) \
                htable_grow(&map->htable, sizeof(key_ty), sizeof(val_ty)); \
            map->elem_count++; \
            return true; \
        } \
        return false; \
    } \
    VISIBILITY(vis) val_ty const* name##_find(const struct name* map, key_ty const* key) { \
        size_t idx; \
        if (!htable_find(&map->htable, &idx, key, sizeof(key_ty), hash(hash_init(), key), name##_cmp_wrapper)) \
           return NULL; \
        return ((val_ty const*)map->htable.vals) + idx; \
    } \
    VISIBILITY(vis) bool name##_remove(struct name* map, key_ty const* key) { \
        if (htable_remove(&map->htable, key, sizeof(key_ty), sizeof(val_ty), hash(hash_init(), key), name##_cmp_wrapper)) { \
            map->elem_count--; \
            return true; \
        } \
        return false; \
    }
