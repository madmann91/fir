#pragma once

#include "htable.h"
#include "linkage.h"

#include <string.h>

#define SET_DEFAULT_CAPACITY 4
#define SET_PREFIX set_very_long_prefix_

#define SET_FOREACH(elem_ty, elem, set) \
    for (size_t SET_PREFIX##i = 0; SET_PREFIX##i < (set).htable.capacity; ++SET_PREFIX##i) \
        if (htable_is_bucket_occupied(&(set).htable, SET_PREFIX##i)) \
            for (bool SET_PREFIX##once = true; SET_PREFIX##once; SET_PREFIX##once = false) \
                for (elem_ty const* elem = &((elem_ty const*)(set).htable.keys)[SET_PREFIX##i]; SET_PREFIX##once; SET_PREFIX##once = false) \

#define SET_DEFINE(name, elem_ty, hash, cmp, linkage) \
    SET_DECL(name, elem_ty, linkage) \
    SET_IMPL(name, elem_ty, hash, cmp, linkage)

#define SET_DECL(name, elem_ty, linkage) \
    struct name { \
        struct htable htable; \
    }; \
    LINKAGE(linkage) struct name name##_create_with_capacity(size_t); \
    LINKAGE(linkage) struct name name##_create(void); \
    LINKAGE(linkage) void name##_destroy(struct name*); \
    LINKAGE(linkage) void name##_rehash(struct name*); \
    LINKAGE(linkage) bool name##_insert(struct name*, elem_ty const*); \
    LINKAGE(linkage) elem_ty const* name##_find(const struct name*, elem_ty const*);

#define SET_IMPL(name, elem_ty, hash, cmp, linkage) \
    LINKAGE(linkage) struct name name##_create_with_capacity(size_t capacity) { \
        return (struct name) { \
            .htable = htable_create(sizeof(elem_ty), 0, capacity) \
        }; \
    } \
    LINKAGE(linkage) struct name name##_create(void) { \
        return name##_create_with_capacity(SET_DEFAULT_CAPACITY); \
    } \
    LINKAGE(linkage) void name##_destroy(struct name* set) { \
        htable_destroy(&set->htable); \
    } \
    LINKAGE(linkage) void name##_rehash(struct name*); \
    LINKAGE(linkage) bool name##_insert(struct name* set, elem_ty const* elem) { \
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
    LINKAGE(linkage) elem_ty const* name##_find(const struct name* set, elem_ty const* elem) { \
        const struct htable* htable = &set->htable; \
        uint32_t h = hash(elem) | HTABLE_OCCUPIED_FLAG; \
        size_t idx = htable_first_bucket(htable, h); \
        for (; htable_is_bucket_occupied(htable, idx); idx = htable_next_bucket(htable, idx)) { \
            if (htable->hashes[idx] == h && cmp(((elem_ty*)htable->keys) + idx, elem)) \
                return ((elem_ty*)htable->keys) + idx; \
        } \
        return NULL; \
    } \
    LINKAGE(linkage) void name##_rehash(struct name* set) { \
        struct name copy = name##_create_with_capacity(htable_rehashed_capacity(&set->htable)); \
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
