#include <fir/dbg_info.h>

#include "support/str_pool.h"
#include "support/alloc.h"
#include "support/vec.h"

VEC_DEFINE(dbg_info_vec, struct fir_dbg_info*, PRIVATE)

struct fir_dbg_info_pool {
    struct str_pool* str_pool;
    struct dbg_info_vec dbg_info;
};

struct fir_dbg_info_pool* fir_dbg_info_pool_create(void) {
    struct fir_dbg_info_pool* pool = xmalloc(sizeof(struct fir_dbg_info_pool));
    pool->str_pool = str_pool_create();
    pool->dbg_info = dbg_info_vec_create();
    return pool;
}

void fir_dbg_info_pool_destroy(struct fir_dbg_info_pool* pool) {
    VEC_FOREACH(struct fir_dbg_info*, elem_ptr, pool->dbg_info) {
        free(*elem_ptr);
    }
    str_pool_destroy(pool->str_pool);
    dbg_info_vec_destroy(&pool->dbg_info);
    free(pool);
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
    dbg_info->name = str_pool_insert_view(pool->str_pool,
        (struct str_view) { .data = name, .length = name_len });
    dbg_info->file_name = str_pool_insert_view(pool->str_pool,
        (struct str_view) { .data = file_name, .length = file_name_len });
    dbg_info->source_range = source_range;
    dbg_info_vec_push(&pool->dbg_info, &dbg_info);
    return dbg_info;
}
