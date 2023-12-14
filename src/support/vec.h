#pragma once

#include "alloc.h"
#include "visibility.h"

#include <string.h>
#include <stdlib.h>

#define SMALL_VEC_CAPACITY 4

#define VEC_FOREACH(elem_ty, elem, vec) \
    for (elem_ty* elem = (vec).elems; elem != (vec).elems + (vec).elem_count; ++elem)

#define VEC_REV_FOREACH(elem_ty, elem, vec) \
    for (elem_ty* elem = (vec).elems + (vec).elem_count; elem-- != (vec).elems; )

#define VEC_DEFINE(name, elem_ty, vis) \
    VEC_DECL(name, elem_ty, vis) \
    VEC_IMPL(name, elem_ty, vis)

#define VEC_DECL(name, elem_ty, vis) \
    struct name { \
        elem_ty* elems; \
        size_t capacity; \
        size_t elem_count; \
    }; \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create_with_capacity(size_t); \
    [[nodiscard]] VISIBILITY(vis) struct name name##_create(void); \
    VISIBILITY(vis) void name##_destroy(struct name*); \
    VISIBILITY(vis) void name##_resize(struct name*, size_t); \
    VISIBILITY(vis) void name##_push(struct name*, elem_ty const*); \
    VISIBILITY(vis) elem_ty* name##_pop(struct name*); \
    VISIBILITY(vis) elem_ty* name##_last(struct name*); \
    VISIBILITY(vis) void name##_clear(struct name*);

#define VEC_IMPL(name, elem_ty, vis) \
    VISIBILITY(vis) struct name name##_create_with_capacity(size_t init_capacity) { \
        return (struct name) { \
            .elems = xmalloc(sizeof(elem_ty) * init_capacity), \
            .elem_count = 0, \
            .capacity = init_capacity \
        }; \
    } \
    VISIBILITY(vis) struct name name##_create(void) { \
        return (struct name) {}; \
    } \
    VISIBILITY(vis) void name##_destroy(struct name* vec) { \
        free(vec->elems); \
        memset(vec, 0, sizeof(struct name)); \
    } \
    VISIBILITY(vis) void name##_resize(struct name* vec, size_t elem_count) { \
        if (elem_count > vec->capacity) { \
            vec->capacity += vec->capacity >> 1; \
            if (elem_count > vec->capacity) \
                vec->capacity = elem_count; \
            vec->elems = xrealloc(vec->elems, vec->capacity * sizeof(elem_ty)); \
        } \
        vec->elem_count = elem_count; \
    } \
    VISIBILITY(vis) void name##_push(struct name* vec, elem_ty const* elem) { \
        name##_resize(vec, vec->elem_count + 1); \
        vec->elems[vec->elem_count - 1] = *elem; \
    } \
    VISIBILITY(vis) elem_ty* name##_pop(struct name* vec) { \
        return &vec->elems[--vec->elem_count]; \
    } \
    VISIBILITY(vis) elem_ty* name##_last(struct name* vec) { \
        return &vec->elems[vec->elem_count - 1]; \
    } \
    VISIBILITY(vis) void name##_clear(struct name* vec) { \
        vec->elem_count = 0; \
    }

#define SMALL_VEC_DEFINE(name, elem_ty, vis) \
    SMALL_VEC_DECL(name, elem_ty, vis) \
    SMALL_VEC_IMPL(name, elem_ty, vis)

#define SMALL_VEC_DECL(name, elem_ty, vis) \
    struct name { \
        elem_ty small_elems[SMALL_VEC_CAPACITY]; \
        elem_ty* elems; \
        size_t capacity; \
        size_t elem_count; \
    }; \
    VISIBILITY(vis) void name##_init(struct name*); \
    VISIBILITY(vis) void name##_destroy(struct name*); \
    VISIBILITY(vis) void name##_resize(struct name*, size_t); \
    VISIBILITY(vis) void name##_push(struct name*, elem_ty const*); \
    VISIBILITY(vis) elem_ty* name##_pop(struct name*); \
    VISIBILITY(vis) elem_ty* name##_last(struct name*); \
    VISIBILITY(vis) void name##_clear(struct name*);

#define SMALL_VEC_IMPL(name, elem_ty, vis) \
    VISIBILITY(vis) void name##_init(struct name* vec) { \
        vec->elem_count = 0; \
        vec->elems = vec->small_elems; \
        vec->capacity = SMALL_VEC_CAPACITY; \
    } \
    VISIBILITY(vis) void name##_destroy(struct name* vec) { \
        if (vec->elems != vec->small_elems) \
            free(vec->elems); \
        memset(vec, 0, sizeof(struct name)); \
    } \
    VISIBILITY(vis) void name##_resize(struct name* vec, size_t elem_count) { \
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
    VISIBILITY(vis) void name##_push(struct name* vec, elem_ty const* elem) { \
        name##_resize(vec, vec->elem_count + 1); \
        vec->elems[vec->elem_count - 1] = *elem; \
    } \
    VISIBILITY(vis) elem_ty* name##_pop(struct name* vec) { \
        return &vec->elems[--vec->elem_count]; \
    } \
    VISIBILITY(vis) elem_ty* name##_last(struct name* vec) { \
        return &vec->elems[vec->elem_count - 1]; \
    } \
    VISIBILITY(vis) void name##_clear(struct name* vec) { \
        vec->elem_count = 0; \
    }
