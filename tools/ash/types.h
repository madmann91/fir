#pragma once

#include "token.h"

#include <stdio.h>

enum type_tag {
#define x(tag, ...) TYPE_##tag,
    PRIM_TYPE_LIST(x)
#undef x
    TYPE_VARIANT,
    TYPE_FUNC,
    TYPE_RECORD
};

struct type;

struct type {
    uint64_t id;
    enum type_tag tag;
    union {
        struct {
            const struct type* const* option_types;
            size_t option_count;
        } variant_type;
        struct {
            const struct type* param_type;
            const struct type* ret_type;
        } func_type;
        struct {
            const struct type* const* field_types;
            const char* const* field_names;
            size_t field_count;
        } record_type;
    };
};

struct type_set;

void print_type(FILE*, const struct type*);
void dump_type(const struct type*);

bool type_tag_is_prim_type(enum type_tag);

struct type_set* type_set_create(void);
void type_set_destroy(struct type_set*);

const struct type* prim_type(struct type_set*, enum type_tag);

const struct type* func_type(
    struct type_set*,
    const struct type* param_type,
    const struct type* ret_type);

const struct type* variant_type(
    struct type_set*,
    const struct type* const* option_types,
    size_t option_count);

const struct type* record_type(
    struct type_set*,
    const struct type* const* field_types,
    const char* const* field_names,
    size_t field_count);
