#pragma once

#include "hash_table.h"
#include "visibility.h"
#include "hash.h"

#include <string.h>

#define MAP_DEFAULT_CAPACITY 4
#define MAP_PREFIX map_very_long_prefix_

#define MAP_FOREACH_COMMON(map, ...) \
    for (size_t MAP_PREFIX##i = 0; MAP_PREFIX##i < (map).hash_table.capacity; ++MAP_PREFIX##i) \
        if (hash_table_is_bucket_occupied(&(map).hash_table, MAP_PREFIX##i)) \
            for (bool MAP_PREFIX##once = true; MAP_PREFIX##once; MAP_PREFIX##once = false) \
                __VA_ARGS__

#define MAP_FOREACH_ACCESS(ty, val, elems) \
    for (ty const* val = &((ty const*)(elems))[MAP_PREFIX##i]; MAP_PREFIX##once; MAP_PREFIX##once = false) \

#define MAP_FOREACH(key_ty, key, val_ty, val, map) \
    MAP_FOREACH_COMMON(map, \
        MAP_FOREACH_ACCESS(key_ty, key, (map).hash_table.keys) \
        MAP_FOREACH_ACCESS(val_ty, val, (map).hash_table.elems))

#define MAP_FOREACH_KEY(key_ty, key, map) \
    MAP_FOREACH_COMMON(map, \
        MAP_FOREACH_ACCESS(key_ty, key, (map).hash_table.keys)) \

#define MAP_FOREACH_VAL(val_ty, val, map) \
    MAP_FOREACH_COMMON(map, \
        MAP_FOREACH_ACCESS(val_ty, val, (map).hash_table.vals))

#define MAP_DEFINE(name, key_ty, val_ty, hash, is_equal, vis) \
    MAP_DECL(name, key_ty, val_ty, vis) \
    MAP_IMPL(name, key_ty, val_ty, hash, is_equal, vis)

#define MAP_DECL(name, key_ty, val_ty, vis) \
    struct name { \
        struct hash_table hash_table; \
        size_t elem_count; \
    }; \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create_with_capacity(size_t); \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create(void); \
    VISIBILITY(vis) void name##_destroy(struct name*); \
    VISIBILITY(vis) void name##_clear(struct name*); \
    VISIBILITY(vis) bool name##_insert(struct name*, key_ty const*, val_ty const*); \
    VISIBILITY(vis) val_ty const* name##_find(const struct name*, key_ty const*); \
    VISIBILITY(vis) bool name##_remove(struct name*, key_ty const*);

#define MAP_IMPL(name, key_ty, val_ty, hash, is_equal, vis) \
    static inline bool name##_is_equal_wrapper(const void* left, const void* right) { \
        return is_equal((key_ty const*)left, (key_ty const*)right); \
    } \
    VISIBILITY(vis) struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .hash_table = hash_table_create(sizeof(key_ty), sizeof(val_ty), capacity) \
        }; \
    } \
    VISIBILITY(vis) struct name name##_create(void) { \
        return name##_create_with_capacity(MAP_DEFAULT_CAPACITY); \
    } \
    VISIBILITY(vis) void name##_destroy(struct name* map) { \
        hash_table_destroy(&map->hash_table); \
    } \
    VISIBILITY(vis) void name##_clear(struct name* map) { \
        hash_table_clear(&map->hash_table); \
        map->elem_count = 0; \
    } \
    VISIBILITY(vis) bool name##_insert(struct name* map, key_ty const* key, val_ty const* val) { \
        if (hash_table_insert(&map->hash_table, key, val, sizeof(key_ty), sizeof(val_ty), hash(hash_init(), key), name##_is_equal_wrapper)) { \
            if (hash_table_needs_rehash(&map->hash_table, map->elem_count++)) \
                hash_table_grow(&map->hash_table, sizeof(key_ty), sizeof(val_ty)); \
            return true; \
        } \
        return false; \
    } \
    VISIBILITY(vis) val_ty const* name##_find(const struct name* map, key_ty const* key) { \
        size_t idx; \
        if (!hash_table_find(&map->hash_table, &idx, key, sizeof(key_ty), hash(hash_init(), key), name##_is_equal_wrapper)) \
           return NULL; \
        return ((val_ty const*)map->hash_table.vals) + idx; \
    } \
    VISIBILITY(vis) bool name##_remove(struct name* map, key_ty const* key) { \
        if (hash_table_remove(&map->hash_table, key, sizeof(key_ty), sizeof(val_ty), hash(hash_init(), key), name##_is_equal_wrapper)) { \
            map->elem_count--; \
            return true; \
        } \
        return false; \
    }
