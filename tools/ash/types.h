#pragma once

#include "token.h"

#include <stdio.h>

enum type_tag {
    TYPE_TOP,
    TYPE_BOTTOM,
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
struct format_out;

void type_print(struct format_out*, const struct type*);
void type_dump(const struct type*);
char* type_to_string(char* buf, size_t size, const struct type*);

bool type_is_subtype(const struct type*, const struct type*);
bool type_is_prim(const struct type*);
bool type_tag_is_prim(enum type_tag);

struct type_set* type_set_create(void);
void type_set_destroy(struct type_set*);

const struct type* type_top(struct type_set*);
const struct type* type_bottom(struct type_set*);

const struct type* type_prim(struct type_set*, enum type_tag);

const struct type* type_func(
    struct type_set*,
    const struct type* param_type,
    const struct type* ret_type);

const struct type* type_variant(
    struct type_set*,
    const struct type* const* option_types,
    size_t option_count);

const struct type* type_record(
    struct type_set*,
    const struct type* const* field_types,
    const char* const* field_names,
    size_t field_count);
