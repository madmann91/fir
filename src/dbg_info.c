#include <fir/dbg_info.h>

#include "support/set.h"
#include "support/vec.h"
#include "support/hash.h"
#include "support/alloc.h"

#include <string.h>

static uint32_t hash_str(char* const* str_ptr) {
    const char* str = *str_ptr;
    uint32_t h = hash_init();
    for (; *str; ++str)
        h = hash_uint8(h, *str);
    return h;
}

static bool cmp_str(char* const* str_ptr, char* const* other_ptr) {
    return !strcmp(*str_ptr, *other_ptr);
}

DECL_SET(string_set, char*, hash_str, cmp_str)
DECL_VEC(dbg_info_vec, struct fir_dbg_info*)

struct fir_dbg_info_pool {
    struct string_set strings;
    struct dbg_info_vec dbg_info;
};

struct fir_dbg_info_pool* fir_dbg_info_pool_create(void) {
    struct fir_dbg_info_pool* pool = xmalloc(sizeof(struct fir_dbg_info_pool));
    pool->strings = string_set_create();
    pool->dbg_info = dbg_info_vec_create();
    return pool;
}

void fir_dbg_info_pool_destroy(struct fir_dbg_info_pool* pool) {
    FOREACH_SET(char*, str_ptr, pool->strings) {
        free((char*)*str_ptr);
    }
    string_set_destroy(&pool->strings);
    FOREACH_VEC(struct fir_dbg_info*, elem_ptr, pool->dbg_info) {
        free(*elem_ptr);
    }
    dbg_info_vec_destroy(&pool->dbg_info);
    free(pool);
}

static inline const char* unique_string(struct fir_dbg_info_pool* pool, const char* str) {
    if (!str)
        return NULL;
    if (str[0] == '\0')
        return "";
    char* const* found = string_set_find(&pool->strings, (char* const*)&str);
    if (found)
        return *found;
    char* new_str = strdup(str);
    string_set_insert(&pool->strings, &new_str);
    return new_str;
}

const struct fir_dbg_info* fir_dbg_info(
    struct fir_dbg_info_pool* pool,
    const char* name,
    const char* file_name,
    struct fir_source_range source_range)
{
    struct fir_dbg_info* dbg_info = xmalloc(sizeof(struct fir_dbg_info));
    dbg_info->name = unique_string(pool, name);
    dbg_info->file_name = unique_string(pool, file_name);
    dbg_info->source_range = source_range;
    dbg_info_vec_push(&pool->dbg_info, &dbg_info);
    return dbg_info;
}
