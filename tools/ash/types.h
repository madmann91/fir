#pragma once

#include "token.h"
#include "ast.h"

#include "support/vec.h"

#include <stdio.h>

enum type_tag {
#define x(tag, ...) TYPE_##tag = PRIM_TYPE_##tag,
    PRIM_TYPE_LIST(x)
#undef x
    TYPE_TOP,
    TYPE_BOTTOM,
    TYPE_PTR,
    TYPE_REF,
    TYPE_VARIANT,
    TYPE_FUNC,
    TYPE_RECORD,
    TYPE_TUPLE,
    TYPE_ARRAY,
    TYPE_DYN_ARRAY
};

struct type;

struct type {
    uint64_t id;
    enum type_tag tag;
    union {
        struct {
            const struct type* pointee_type;
            bool is_const;
        } ptr_type, ref_type;
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
        struct {
            const struct type* const* arg_types;
            size_t arg_count;
        } tuple_type;
        struct {
            const struct type* elem_type;
            size_t elem_count;
        } array_type;
        struct {
            const struct type* elem_type;
        } dyn_array_type;
    };
};

struct type_set;

SMALL_VEC_DECL(small_type_vec, const struct type*, PUBLIC)

void type_print(FILE*, const struct type*);
void type_dump(const struct type*);
[[nodiscard]] char* type_to_string(const struct type*);

[[nodiscard]] size_t type_find_field(const struct type* record_type, const char* field_name);
[[nodiscard]] const struct type* type_remove_ref(const struct type*);

[[nodiscard]] bool type_is_subtype(const struct type*, const struct type*);
[[nodiscard]] bool type_is_unit(const struct type*);
[[nodiscard]] bool type_is_prim(const struct type*);
[[nodiscard]] bool type_is_int(const struct type*);
[[nodiscard]] bool type_is_signed_int(const struct type*);
[[nodiscard]] bool type_is_unsigned_int(const struct type*);
[[nodiscard]] bool type_is_float(const struct type*);
[[nodiscard]] size_t type_bitwidth(const struct type*);

[[nodiscard]] bool type_tag_is_prim(enum type_tag);
[[nodiscard]] bool type_tag_is_int(enum type_tag);
[[nodiscard]] bool type_tag_is_signed_int(enum type_tag);
[[nodiscard]] bool type_tag_is_unsigned_int(enum type_tag);
[[nodiscard]] bool type_tag_is_float(enum type_tag);
[[nodiscard]] size_t type_tag_bitwidth(enum type_tag);

[[nodiscard]] struct type_set* type_set_create(void);
void type_set_destroy(struct type_set*);

const struct type* type_top(struct type_set*);
const struct type* type_bottom(struct type_set*);

const struct type* type_bool(struct type_set*);
const struct type* type_prim(struct type_set*, enum type_tag);

const struct type* type_ptr(struct type_set*, const struct type* pointee_type, bool is_const);
const struct type* type_ref(struct type_set*, const struct type* pointee_type, bool is_const);

const struct type* type_unit(struct type_set*);

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

const struct type* type_tuple(
    struct type_set*,
    const struct type* const* arg_types,
    size_t arg_count);

const struct type* type_array(
    struct type_set*,
    const struct type* elem_type,
    size_t elem_count);

const struct type* type_dyn_array(
    struct type_set*,
    const struct type* elem_type);
