#include <fir/dbg_info.h>

#include "support/set.h"
#include "support/vec.h"
#include "support/hash.h"
#include "support/alloc.h"
#include "support/str.h"

#include <string.h>

static inline uint32_t hash_str_view(const struct str_view* str_view) {
    uint32_t h = hash_init();
    for (size_t i = 0; i < str_view->length; ++i)
        h = hash_uint8(h, str_view->data[i]);
    return h;
}

static inline bool cmp_str_view(const struct str_view* str_view, const struct str_view* other) {
    return str_view_is_equal(*str_view, *other);
}

DEF_SET(str_view_set, struct str_view, hash_str_view, cmp_str_view, PRIVATE)
DEF_VEC(dbg_info_vec, struct fir_dbg_info*, PRIVATE)

struct fir_dbg_info_pool {
    struct str_view_set strings;
    struct dbg_info_vec dbg_info;
};

struct fir_dbg_info_pool* fir_dbg_info_pool_create(void) {
    struct fir_dbg_info_pool* pool = xmalloc(sizeof(struct fir_dbg_info_pool));
    pool->strings = str_view_set_create();
    pool->dbg_info = dbg_info_vec_create();
    return pool;
}

void fir_dbg_info_pool_destroy(struct fir_dbg_info_pool* pool) {
    FOREACH_SET(struct str_view, str_view, pool->strings) {
        free((char*)str_view->data);
    }
    str_view_set_destroy(&pool->strings);
    FOREACH_VEC(struct fir_dbg_info*, elem_ptr, pool->dbg_info) {
        free(*elem_ptr);
    }
    dbg_info_vec_destroy(&pool->dbg_info);
    free(pool);
}

static inline const char* unique_string(
    struct fir_dbg_info_pool* pool,
    const struct str_view* str_view)
{
    if (str_view->length == 0)
        return NULL;
    const struct str_view* found = str_view_set_find(&pool->strings, str_view);
    if (found)
        return found->data;
    char* str = xmalloc(str_view->length + 1);
    memcpy(str, str_view->data, str_view->length);
    str[str_view->length] = 0;
    str_view_set_insert(&pool->strings, &(struct str_view) { .data = str, .length = str_view->length });
    return str;
}

const struct fir_dbg_info* fir_dbg_info(
    struct fir_dbg_info_pool* pool,
    const char* name,
    const char* file_name,
    struct fir_source_range source_range)
{
    return fir_dbg_info_with_length(pool,
        name, strlen(name),
        file_name, strlen(file_name),
        source_range);
}

const struct fir_dbg_info* fir_dbg_info_with_length(
    struct fir_dbg_info_pool* pool,
    const char* name,
    size_t name_len,
    const char* file_name,
    size_t file_name_len,
    struct fir_source_range source_range)
{
    struct fir_dbg_info* dbg_info = xmalloc(sizeof(struct fir_dbg_info));
    dbg_info->name = unique_string(pool,
        &(struct str_view) { .data = name, .length = name_len });
    dbg_info->file_name = unique_string(pool,
        &(struct str_view) { .data = file_name, .length = file_name_len });
    dbg_info->source_range = source_range;
    dbg_info_vec_push(&pool->dbg_info, &dbg_info);
    return dbg_info;
}
