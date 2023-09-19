#pragma once

#include "alloc.h"

#include <string.h>
#include <stdlib.h>

#define VEC_SMALL_CAPACITY 4

#define FOREACH_VEC(elem_ty, elem, vec) \
    for (elem_ty* elem = (vec).elems; elem != (vec).elems + (vec).elem_count; ++elem)

#define DECL_VEC(name, elem_ty) \
    struct name { \
        elem_ty* elems; \
        size_t capacity; \
        size_t elem_count; \
    }; \
    static inline struct name name##_create_with_capacity(size_t init_capacity) { \
        return (struct name) { \
            .elems = xmalloc(sizeof(elem_ty) * init_capacity), \
            .elem_count = 0, \
            .capacity = init_capacity \
        }; \
    } \
    static inline struct name name##_create(void) { \
        return (struct name) {}; \
    } \
    static inline void name##_destroy(struct name* vec) { \
        free(vec->elems); \
        memset(vec, 0, sizeof(struct name)); \
    } \
    static inline void name##_resize(struct name* vec, size_t elem_count) { \
        if (elem_count > vec->capacity) { \
            vec->capacity += vec->capacity >> 1; \
            if (elem_count > vec->capacity) \
                vec->capacity = elem_count; \
            vec->elems = xrealloc(vec->elems, vec->capacity * sizeof(elem_ty)); \
        } \
        vec->elem_count = elem_count; \
    } \
    static inline void name##_push(struct name* vec, elem_ty const* elem) { \
        name##_resize(vec, vec->elem_count + 1); \
        vec->elems[vec->elem_count - 1] = *elem; \
    } \
    static inline void name##_pop(struct name* vec) { \
        vec->elem_count--; \
    } \
    static inline void name##_clear(struct name* vec) { \
        vec->elem_count = 0; \
    }

#define DECL_SMALL_VEC(name, elem_ty) \
    struct name { \
        elem_ty small_elems[VEC_SMALL_CAPACITY]; \
        elem_ty* elems; \
        size_t capacity; \
        size_t elem_count; \
    }; \
    static inline void name##_init(struct name* vec) { \
        vec->elem_count = 0; \
        vec->elems = vec->small_elems; \
        vec->capacity = VEC_SMALL_CAPACITY; \
    } \
    static inline void name##_destroy(struct name* vec) { \
        if (vec->elems != vec->small_elems) \
            free(vec->elems); \
        memset(vec, 0, sizeof(struct name)); \
    } \
    static inline void name##_resize(struct name* vec, size_t elem_count) { \
        if (elem_count > vec->capacity) { \
            vec->capacity += vec->capacity >> 1; \
            if (elem_count > vec->capacity) \
                vec->capacity = elem_count; \
            if (vec->elems == vec->small_elems) { \
                vec->elems = xmalloc(vec->capacity * sizeof(elem_ty)); \
                memcpy(vec->elems, vec->small_elems, vec->elem_count * sizeof(elem_ty)); \
            } else { \
                vec->elems = xrealloc(vec->elems, vec->capacity * sizeof(elem_ty)); \
            } \
        } \
        vec->elem_count = elem_count; \
    } \
    static inline void name##_push(struct name* vec, elem_ty const* elem) { \
        name##_resize(vec, vec->elem_count + 1); \
        vec->elems[vec->elem_count - 1] = *elem; \
    } \
    static inline void name##_pop(struct name* vec) { \
        vec->elem_count--; \
    } \
    static inline void name##_clear(struct name* vec) { \
        vec->elem_count = 0; \
    }
