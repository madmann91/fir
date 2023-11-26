#pragma once

#include "hash_table.h"
#include "visibility.h"
#include "hash.h"

#include <string.h>

#define SET_DEFAULT_CAPACITY 4
#define SET_PREFIX set_very_long_prefix_

#define SET_FOREACH(elem_ty, elem, set) \
    for (size_t SET_PREFIX##i = 0; SET_PREFIX##i < (set).hash_table.capacity; ++SET_PREFIX##i) \
        if (hash_table_is_bucket_occupied(&(set).hash_table, SET_PREFIX##i)) \
            for (bool SET_PREFIX##once = true; SET_PREFIX##once; SET_PREFIX##once = false) \
                for (elem_ty const* elem = &((elem_ty const*)(set).hash_table.keys)[SET_PREFIX##i]; SET_PREFIX##once; SET_PREFIX##once = false) \

#define SET_DEFINE(name, elem_ty, hash, cmp, vis) \
    SET_DECL(name, elem_ty, vis) \
    SET_IMPL(name, elem_ty, hash, cmp, vis)

#define SET_DECL(name, elem_ty, vis) \
    struct name { \
        struct hash_table hash_table; \
        size_t elem_count; \
    }; \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create_with_capacity(size_t); \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create(void); \
    VISIBILITY(vis) void name##_destroy(struct name*); \
    VISIBILITY(vis) void name##_clear(struct name*); \
    VISIBILITY(vis) bool name##_insert(struct name*, elem_ty const*); \
    VISIBILITY(vis) elem_ty const* name##_find(const struct name*, elem_ty const*); \
    VISIBILITY(vis) bool name##_remove(struct name*, elem_ty const*);

#define SET_IMPL(name, elem_ty, hash, cmp, vis) \
    static inline bool name##_cmp_wrapper(const void* left, const void* right) { \
        return cmp((elem_ty const*)left, (elem_ty const*)right); \
    } \
    VISIBILITY(vis) struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .hash_table = hash_table_create(sizeof(elem_ty), 0, capacity) \
        }; \
    } \
    VISIBILITY(vis) struct name name##_create(void) { \
        return name##_create_with_capacity(SET_DEFAULT_CAPACITY); \
    } \
    VISIBILITY(vis) void name##_destroy(struct name* set) { \
        hash_table_destroy(&set->hash_table); \
    } \
    VISIBILITY(vis) void name##_clear(struct name* set) { \
        hash_table_clear(&set->hash_table); \
        set->elem_count = 0; \
    } \
    VISIBILITY(vis) bool name##_insert(struct name* set, elem_ty const* elem) { \
        if (hash_table_insert(&set->hash_table, elem, NULL, sizeof(elem_ty), 0, hash(hash_init(), elem), name##_cmp_wrapper)) { \
            if (hash_table_needs_rehash(&set->hash_table, set->elem_count)) \
                hash_table_grow(&set->hash_table, sizeof(elem_ty), 0); \
            set->elem_count++; \
            return true; \
        } \
        return false; \
    } \
    VISIBILITY(vis) elem_ty const* name##_find(const struct name* set, elem_ty const* elem) { \
        size_t idx; \
        if (!hash_table_find(&set->hash_table, &idx, elem, sizeof(elem_ty), hash(hash_init(), elem), name##_cmp_wrapper)) \
           return NULL; \
        return ((elem_ty const*)set->hash_table.keys) + idx; \
    } \
    VISIBILITY(vis) bool name##_remove(struct name* set, elem_ty const* elem) { \
        if (hash_table_remove(&set->hash_table, elem, sizeof(elem_ty), 0, hash(hash_init(), elem), name##_cmp_wrapper)) { \
            set->elem_count--; \
            return true; \
        } \
        return false; \
    }
