#pragma once

#include "hash_table.h"
#include "visibility.h"
#include "alloc.h"
#include "hash.h"

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#define IMMUTABLE_SET_POOL_DEFAULT_CAPACITY 4
#define IMMUTABLE_SET_SMALL_CAPACITY 4

#define IMMUTABLE_SET_FOREACH(elem_ty, elem, set) \
    for (elem_ty const* elem = (set).elems; elem != (set).elems + (set).elem_count; ++elem)

#define IMMUTABLE_SET_DEFINE(name, elem_ty, hash, cmp, vis) \
    IMMUTABLE_SET_DECL(name, elem_ty, vis) \
    IMMUTABLE_SET_IMPL(name, elem_ty, hash, cmp, vis)

#define IMMUTABLE_SET_DECL(name, elem_ty, vis) \
    struct name##_pool { \
        struct hash_table hash_table; \
    }; \
    struct name { \
        size_t elem_count; \
        elem_ty elems[]; \
    }; \
    [[nodiscard]] VISIBILITY(vis) struct name##_pool name##_pool_create(void); \
    VISIBILITY(vis) void name##_pool_destroy(struct name##_pool*); \
    VISIBILITY(vis) const struct name* name##_pool_insert(struct name##_pool*, elem_ty*, size_t); \
    VISIBILITY(vis) const struct name* name##_pool_insert_unsafe(struct name##_pool*, elem_ty const*, size_t); \
    VISIBILITY(vis) const struct name* name##_pool_merge(struct name##_pool*, const struct name*, const struct name*); \
    VISIBILITY(vis) void name##_pool_reset(struct name##_pool*); \
    VISIBILITY(vis) elem_ty const* name##_find(const struct name*, elem_ty const*); \

#define IMMUTABLE_SET_IMPL(name, elem_ty, hash, cmp, vis) \
    static inline int name##_cmp_wrapper(const void* left, const void* right) { \
        return cmp((elem_ty const*)left, (elem_ty const*)right); \
    } \
    static inline bool name##_is_equal_wrapper(const void* left, const void* right) { \
        struct name* left_set = *(struct name**)left; \
        struct name* right_set = *(struct name**)right; \
        if (left_set->elem_count != right_set->elem_count) \
            return false; \
        for (size_t i = 0; i < left_set->elem_count; ++i) { \
            if (cmp(&left_set->elems[i], &right_set->elems[i]) != 0) \
                return false; \
        } \
        return true; \
    } \
    static inline uint32_t name##_hash_wrapper(uint32_t h, const struct name* set) { \
        h = hash_uint64(h, set->elem_count); \
        for (size_t i = 0; i < set->elem_count; ++i) \
            h = hash(h, &set->elems[i]); \
        return h; \
    } \
    static inline struct name* name##_alloc(elem_ty const* elems, size_t elem_count) { \
        struct name* set = xmalloc(sizeof(struct name) + sizeof(elem_ty) * elem_count); \
        set->elem_count = elem_count; \
        memcpy(set->elems, elems, sizeof(elem_ty) * elem_count); \
        return set; \
    } \
    VISIBILITY(vis) struct name##_pool name##_pool_create(void) { \
        return (struct name##_pool) { \
            .hash_table = hash_table_create(sizeof(struct name*), 0, IMMUTABLE_SET_POOL_DEFAULT_CAPACITY) \
        }; \
    } \
    VISIBILITY(vis) void name##_pool_destroy(struct name##_pool* pool) { \
        name##_pool_reset(pool); \
        hash_table_destroy(&pool->hash_table); \
    } \
    VISIBILITY(vis) const struct name* name##_pool_insert(struct name##_pool* pool, elem_ty* elems, size_t elem_count) { \
        qsort(elems, elem_count, sizeof(elem_ty), name##_cmp_wrapper); \
        size_t unique_elem_count = 0; \
        for (size_t i = 0; i < elem_count; ++i) { \
            while (i + 1 < elem_count && elems[i] == elems[i + 1]) i++; \
            elems[unique_elem_count++] = elems[i]; \
        } \
        return name##_pool_insert_unsafe(pool, elems, unique_elem_count); \
    } \
    VISIBILITY(vis) const struct name* name##_pool_insert_unsafe( \
        struct name##_pool* pool, \
        elem_ty const* elems, \
        size_t elem_count) \
    { \
        struct { size_t elem_count; elem_ty elems[IMMUTABLE_SET_SMALL_CAPACITY]; } small_set; \
        bool use_small_set = elem_count <= IMMUTABLE_SET_SMALL_CAPACITY; \
        struct name* set = (struct name*)&small_set; \
        struct name* set_heap = NULL; /* avoid -Wreturn-local-addr */ \
        if (use_small_set) { \
            memcpy(set->elems, elems, sizeof(elem_ty) * elem_count); \
            set->elem_count = elem_count; \
        } else { \
            set = set_heap = name##_alloc(elems, elem_count); \
        } \
        \
        uint32_t h = name##_hash_wrapper(hash_init(), set); \
        size_t index = SIZE_MAX; \
        if (hash_table_find(&pool->hash_table, &index, &set, sizeof(struct name*), h, name##_is_equal_wrapper)) { \
            free(set_heap); \
            return ((struct name**)pool->hash_table.keys)[index]; \
        } \
        \
        if (use_small_set) \
            set_heap = name##_alloc(elems, elem_count); \
        set = set_heap; \
        [[maybe_unused]] bool was_inserted = hash_table_insert( \
            &pool->hash_table, &set, NULL, sizeof(struct name*), 0, h, name##_is_equal_wrapper); \
        assert(was_inserted); \
        return set; \
    } \
    VISIBILITY(vis) const struct name* name##_pool_merge( \
        struct name##_pool* pool, \
        const struct name* first_set, \
        const struct name* second_set) \
    { \
        elem_ty small_elem_buffer[IMMUTABLE_SET_SMALL_CAPACITY * 2]; \
        elem_ty* merged_elems = small_elem_buffer; \
        if (first_set->elem_count + second_set->elem_count > IMMUTABLE_SET_SMALL_CAPACITY * 2) \
            merged_elems = xmalloc(sizeof(elem_ty) * (first_set->elem_count + second_set->elem_count)); \
        size_t i = 0, j = 0, k = 0; \
        for (; i < first_set->elem_count && j < second_set->elem_count;) { \
            int order = cmp(&first_set->elems[i], &second_set->elems[j]); \
            if (order <= 0) { \
                merged_elems[k++] = first_set->elems[i++]; \
                if (order == 0) \
                    j++; \
            } else { \
                merged_elems[k++] = second_set->elems[j++]; \
            } \
        } \
        for (; i < first_set->elem_count; i++) \
            merged_elems[k++] = first_set->elems[i]; \
        for (; j < second_set->elem_count; j++) \
            merged_elems[k++] = second_set->elems[j]; \
        const struct name* merged_set = name##_pool_insert_unsafe(pool, merged_elems, k); \
        if (merged_elems != small_elem_buffer) \
            free(merged_elems); \
        return merged_set; \
    } \
    VISIBILITY(vis) void name##_pool_reset(struct name##_pool* pool) { \
        for (size_t i = 0; i < pool->hash_table.capacity; ++i) { \
            if (hash_table_is_bucket_occupied(&pool->hash_table, i)) \
                free(((struct name**)pool->hash_table.keys)[i]); \
        } \
        hash_table_clear(&pool->hash_table); \
    } \
    VISIBILITY(vis) elem_ty const* name##_find(const struct name* set, elem_ty const* elem) { \
        return bsearch(elem, set->elems, set->elem_count, sizeof(elem_ty), name##_cmp_wrapper); \
    }
