#pragma once

#include "str.h"

struct str_pool;

[[nodiscard]] struct str_pool* str_pool_create(void);
void str_pool_destroy(struct str_pool*);
const char* str_pool_insert(struct str_pool*, const char*);
const char* str_pool_insert_view(struct str_pool*, struct str_view);
