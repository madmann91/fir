#include "types.h"

#include "support/datatypes.h"
#include "support/hash.h"
#include "support/str_pool.h"
#include "support/mem_pool.h"
#include "support/mem_stream.h"

SMALL_VEC_IMPL(small_type_vec, const struct type*, PUBLIC)

static inline uint32_t hash_type(uint32_t h, const struct type* const* type_ptr) {
    const struct type* type = *type_ptr;
    h = hash_uint32(h, type->tag);
    switch (type->tag) {
        case TYPE_VARIANT: {
            h = hash_uint64(h, type->variant_type.option_count);
            for (size_t i = 0; i < type->variant_type.option_count; ++i)
                h = hash_uint64(h, type->variant_type.option_types[i]->id);
            break;
        }
        case TYPE_RECORD: {
            h = hash_uint64(h, type->record_type.field_count);
            for (size_t i = 0; i < type->record_type.field_count; ++i) {
                h = hash_string(h, type->record_type.field_names[i]);
                h = hash_uint64(h, type->record_type.field_types[i]->id);
            }
            break;
        }
        case TYPE_TUPLE: {
            h = hash_uint64(h, type->tuple_type.arg_count);
            for (size_t i = 0; i < type->tuple_type.arg_count; ++i)
                h = hash_uint64(h, type->tuple_type.arg_types[i]->id);
            break;
        }
        case TYPE_ARRAY:
            h = hash_uint64(h, type->array_type.elem_count);
            h = hash_uint64(h, type->array_type.elem_type->id);
            break;
        case TYPE_DYN_ARRAY:
            h = hash_uint64(h, type->dyn_array_type.elem_type->id);
            break;
        case TYPE_FUNC:
            h = hash_uint64(h, type->func_type.param_type->id);
            h = hash_uint64(h, type->func_type.ret_type->id);
            break;
        case TYPE_REF:
        case TYPE_PTR:
            h = hash_uint64(h, type->ptr_type.pointee_type->id);
            h = hash_uint8(h, type->ptr_type.is_const);
            break;
        default:
            break;
    }
    return h;
}

static inline bool is_type_equal(
    const struct type* const* type_ptr,
    const struct type* const* other_ptr)
{
    const struct type* type = *type_ptr;
    const struct type* other = *other_ptr;
    if (type->tag != other->tag)
        return false;
    switch (type->tag) {
        case TYPE_VARIANT: {
            return
                type->variant_type.option_count == other->variant_type.option_count &&
                !memcmp(
                    type->variant_type.option_types,
                    other->variant_type.option_types,
                    sizeof(struct type*) * type->variant_type.option_count);
        }
        case TYPE_RECORD: {
            return
                type->record_type.field_count == other->record_type.field_count &&
                !memcmp(
                    type->record_type.field_names,
                    other->record_type.field_names,
                    sizeof(char*) * type->record_type.field_count) &&
                !memcmp(
                    type->record_type.field_types,
                    other->record_type.field_types,
                    sizeof(struct type*) * type->record_type.field_count);
        }
        case TYPE_TUPLE: {
            return
                type->tuple_type.arg_count == other->tuple_type.arg_count &&
                !memcmp(
                    type->tuple_type.arg_types,
                    other->tuple_type.arg_types,
                    sizeof(char*) * type->tuple_type.arg_count);
        }
        case TYPE_ARRAY:
            return
                type->array_type.elem_count == other->array_type.elem_count &&
                type->array_type.elem_type  == other->array_type.elem_type;
        case TYPE_DYN_ARRAY:
            return type->dyn_array_type.elem_type  == other->dyn_array_type.elem_type;
        case TYPE_FUNC:
            return
                type->func_type.param_type == other->func_type.param_type &&
                type->func_type.ret_type   == other->func_type.ret_type;
        case TYPE_PTR:
        case TYPE_REF:
            return
                type->ptr_type.pointee_type == other->ptr_type.pointee_type &&
                type->ptr_type.is_const     == other->ptr_type.is_const;
        default:
            return true;
    }
}

SET_DEFINE(internal_type_set, const struct type*, hash_type, is_type_equal, PRIVATE)

struct type_set {
    uint64_t cur_id;
    struct str_pool* str_pool;
    struct mem_pool mem_pool;
    struct internal_type_set types;
};

void type_print(FILE* file, const struct type* type) {
    switch (type->tag) {
#define x(tag, str) case TYPE_##tag: fprintf(file, str); break;
        PRIM_TYPE_LIST(x)
#undef x
        case TYPE_VARIANT:
            for (size_t i = 0; i < type->variant_type.option_count; ++i) {
                type_print(file, type->variant_type.option_types[i]);
                if (i + 1 != type->variant_type.option_count)
                    fprintf(file, " | ");
            }
            break;
        case TYPE_RECORD:
            fprintf(file, "[");
            for (size_t i = 0; i < type->record_type.field_count; ++i) {
                fprintf(file, "%s: ", type->record_type.field_names[i]);
                type_print(file, type->record_type.field_types[i]);
                if (i + 1 != type->record_type.field_count)
                    fprintf(file, ", ");
            }
            fprintf(file, "]");
            break;
        case TYPE_TUPLE:
            fprintf(file, "(");
            for (size_t i = 0; i < type->tuple_type.arg_count; ++i) {
                type_print(file, type->tuple_type.arg_types[i]);
                if (i + 1 != type->tuple_type.arg_count)
                    fprintf(file, ", ");
            }
            fprintf(file, ")");
            break;
        case TYPE_ARRAY:
            fprintf(file, "[");
            type_print(file, type->array_type.elem_type);
            fprintf(file, " * %zu]", type->array_type.elem_count);
            break;
        case TYPE_DYN_ARRAY:
            fprintf(file, "[");
            type_print(file, type->dyn_array_type.elem_type);
            fprintf(file, "]");
            break;
        case TYPE_FUNC:
            fprintf(file, "func (");
            type_print(file, type->func_type.param_type);
            fprintf(file, ") -> ");
            type_print(file, type->func_type.ret_type);
            break;
        case TYPE_PTR:
            fprintf(file, "&%s", type->ptr_type.is_const ? "const " : "");
            type_print(file, type->ptr_type.pointee_type);
            break;
        case TYPE_REF:
            fprintf(file, "ref %s", type->ptr_type.is_const ? "const " : "");
            type_print(file, type->ref_type.pointee_type);
            break;
        case TYPE_BOTTOM:
            fprintf(file, "<bottom>");
            break;
        case TYPE_TOP:
            fprintf(file, "<top>");
            break;
        default:
            assert(false && "invalid type");
            break;
    }
}

void type_dump(const struct type* type) {
    type_print(stdout, type);
    printf("\n");
    fflush(stdout);
}

char* type_to_string(const struct type* type) {
    struct mem_stream mem_stream;
    mem_stream_init(&mem_stream);
    type_print(mem_stream.file, type);
    mem_stream_destroy(&mem_stream);
    return mem_stream.buf;
}

static int cmp_field_names(const void* field, const void* other) {
    return strcmp(*(char**)field, *(char**)other);
}

size_t type_find_field(const struct type* record_type, const char* field_name) {
    assert(record_type->tag == TYPE_RECORD);
    const char** name_ptr = bsearch(&field_name,
        record_type->record_type.field_names,
        record_type->record_type.field_count,
        sizeof(char*), cmp_field_names);
    return name_ptr ? (size_t)(name_ptr - record_type->record_type.field_names) : SIZE_MAX;
}

const struct type* type_remove_ref(const struct type* type) {
    return type->tag == TYPE_REF ? type->ref_type.pointee_type : type;
}

bool type_is_subtype(const struct type* left, const struct type* right) {
    if (left == right || right->tag == TYPE_TOP || left->tag == TYPE_BOTTOM)
        return true;
    if (left->tag == TYPE_REF)
        return type_is_subtype(left->ref_type.pointee_type, right);
    if ((type_is_signed_int(left) && type_is_signed_int(right)) ||
        (type_is_unsigned_int(left) && type_is_unsigned_int(right)) ||
        (type_is_float(left) && type_is_float(right)))
        return type_bitwidth(left) <= type_bitwidth(right);
    if (left->tag == TYPE_RECORD && right->tag == TYPE_RECORD) {
        if (left->record_type.field_count < right->record_type.field_count)
            return false;
        for (size_t i = 0; i < right->record_type.field_count; ++i) {
            size_t field_index = type_find_field(left, right->record_type.field_names[i]);
            if (field_index >= left->record_type.field_count)
                return false;
            if (!type_is_subtype(
                left->record_type.field_types[field_index],
                right->record_type.field_types[i]))
                return false;
        }
        return true;
    }
    return false;
}

bool type_is_unit(const struct type* type) {
    return type->tag == TYPE_TUPLE && type->tuple_type.arg_count == 0;
}

bool type_is_prim(const struct type* type) { return type_tag_is_prim(type->tag); }
bool type_is_float(const struct type* type) { return type_tag_is_float(type->tag); }
bool type_is_int(const struct type* type) { return type_tag_is_int(type->tag); }
bool type_is_int_or_bool(const struct type* type) { return type_tag_is_int_or_bool(type->tag); }
bool type_is_signed_int(const struct type* type) { return type_tag_is_signed_int(type->tag); }
bool type_is_unsigned_int(const struct type* type) { return type_tag_is_unsigned_int(type->tag); }
bool type_is_aggregate(const struct type* type) { return type_tag_is_aggregate(type->tag); }
size_t type_bitwidth(const struct type* type) { return type_tag_bitwidth(type->tag); }

size_t type_elem_count(const struct type* type) {
    assert(type_is_aggregate(type));
    return type->tag == TYPE_TUPLE ? type->tuple_type.arg_count : type->record_type.field_count;
}

const struct type* type_elem(const struct type* type, size_t index) {
    assert(type_is_aggregate(type));
    assert(index < type_elem_count(type));
    return type->tag == TYPE_TUPLE
        ? type->tuple_type.arg_types[index]
        : type->record_type.field_types[index];
}

bool type_tag_is_prim(enum type_tag tag) {
    switch (tag) {
#define x(tag, ...) case TYPE_##tag:
        PRIM_TYPE_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

bool type_tag_is_int(enum type_tag tag) {
    return type_tag_is_signed_int(tag) || type_tag_is_unsigned_int(tag);
}

bool type_tag_is_int_or_bool(enum type_tag tag) {
    return type_tag_is_int(tag) || tag == TYPE_BOOL;
}

bool type_tag_is_signed_int(enum type_tag tag) {
    switch (tag) {
        case TYPE_I8:
        case TYPE_I16:
        case TYPE_I32:
        case TYPE_I64:
            return true;
        default:
            return false;
    }
}

bool type_tag_is_unsigned_int(enum type_tag tag) {
    switch (tag) {
        case TYPE_U8:
        case TYPE_U16:
        case TYPE_U32:
        case TYPE_U64:
            return true;
        default:
            return false;
    }
}

bool type_tag_is_float(enum type_tag tag) {
    return tag == TYPE_F32 || tag == TYPE_F64;
}

bool type_tag_is_aggregate(enum type_tag tag) {
    return tag == TYPE_TUPLE || tag == TYPE_RECORD;
}

size_t type_tag_bitwidth(enum type_tag tag) {
    switch (tag) {
        case TYPE_I8:
        case TYPE_U8:
            return 8;
        case TYPE_I16:
        case TYPE_U16:
            return 16;
        case TYPE_I32:
        case TYPE_U32:
        case TYPE_F32:
            return 32;
        case TYPE_I64:
        case TYPE_U64:
        case TYPE_F64:
            return 64;
        case TYPE_BOOL:
            return 1;
        default:
            assert(false && "type has no bitwidth");
            return 0;
    }
}

struct type_set* type_set_create(void) {
    struct type_set* type_set = xmalloc(sizeof(struct type_set));
    type_set->cur_id = 0;
    type_set->str_pool = str_pool_create();
    type_set->mem_pool = mem_pool_create();
    type_set->types = internal_type_set_create();
    return type_set;
}

void type_set_destroy(struct type_set* type_set) {
    str_pool_destroy(type_set->str_pool);
    mem_pool_destroy(&type_set->mem_pool);
    internal_type_set_destroy(&type_set->types);
    free(type_set);
}

static const struct type** copy_types(
    struct type_set* type_set,
    const struct type* const* types,
    size_t type_count)
{
    size_t types_size = sizeof(struct type*) * type_count;
    const struct type** types_copy = mem_pool_alloc(&type_set->mem_pool, types_size, alignof(struct type*));
    memcpy(types_copy, types, types_size);
    return types_copy;
}

static const char** copy_strings(
    struct type_set* type_set,
    const char* const* strings,
    size_t string_count)
{
    if (!strings)
        return NULL;
    size_t strings_size = sizeof(char*) * string_count;
    const char** strings_copy = mem_pool_alloc(&type_set->mem_pool, strings_size, alignof(char*));
    for (size_t i = 0; i < string_count; ++i)
        strings_copy[i] = str_pool_insert(type_set->str_pool, strings[i]);
    return strings_copy;
}

static const struct type* insert_type(struct type_set* type_set, const struct type* type) {
    const struct type* const* found = internal_type_set_find(&type_set->types, &type);
    if (found)
        return *found;

    struct type* new_type = MEM_POOL_ALLOC(type_set->mem_pool, struct type);
    memcpy(new_type, type, sizeof(struct type));
    new_type->id = type_set->cur_id++;
    switch (type->tag) {
        case TYPE_VARIANT:
            new_type->variant_type.option_types = copy_types(type_set,
                type->variant_type.option_types,
                type->variant_type.option_count);
            break;
        case TYPE_RECORD:
            new_type->record_type.field_types = copy_types(type_set,
                type->record_type.field_types,
                type->record_type.field_count);
            new_type->record_type.field_names = copy_strings(type_set,
                type->record_type.field_names,
                type->record_type.field_count);
            break;
        case TYPE_TUPLE:
            new_type->tuple_type.arg_types = copy_types(type_set,
                type->tuple_type.arg_types,
                type->tuple_type.arg_count);
            break;
        default:
            break;
    }
    [[maybe_unused]] bool was_inserted = internal_type_set_insert(&type_set->types, (const struct type* const*)&new_type);
    assert(was_inserted);
    return new_type;
}

const struct type* type_top(struct type_set* type_set) {
    return insert_type(type_set, &(struct type) { .tag = TYPE_TOP });
}

const struct type* type_bottom(struct type_set* type_set) {
    return insert_type(type_set, &(struct type) { .tag = TYPE_BOTTOM });
}

const struct type* type_bool(struct type_set* type_set) {
    return type_prim(type_set, TYPE_BOOL);
}

const struct type* type_prim(struct type_set* type_set, enum type_tag tag) {
    assert(type_tag_is_prim(tag));
    return insert_type(type_set, &(struct type) { .tag = tag, });
}

const struct type* type_ptr(struct type_set* type_set, const struct type* pointee_type, bool is_const) {
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_PTR,
        .ptr_type = {
            .pointee_type = pointee_type,
            .is_const = is_const
        }
    });
}

const struct type* type_ref(struct type_set* type_set, const struct type* pointee_type, bool is_const) {
    assert(pointee_type->tag != TYPE_REF);
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_REF,
        .ptr_type = {
            .pointee_type = pointee_type,
            .is_const = is_const
        }
    });
}

const struct type* type_unit(struct type_set* type_set) {
    return type_tuple(type_set, NULL, 0);
}

const struct type* type_func(
    struct type_set* type_set,
    const struct type* param_type,
    const struct type* ret_type)
{
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_FUNC,
        .func_type = {
            .param_type = param_type,
            .ret_type = ret_type
        }
    });
}

const struct type* type_variant(
    struct type_set* type_set,
    const struct type* const* option_types,
    size_t option_count)
{
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_VARIANT,
        .variant_type = {
            .option_types = option_types,
            .option_count = option_count
        }
    });
}

const struct type* type_record(
    struct type_set* type_set,
    const struct type* const* field_types,
    const char* const* field_names,
    size_t field_count)
{
    struct small_string_vec sorted_field_names;
    small_string_vec_init(&sorted_field_names);
    small_string_vec_resize(&sorted_field_names, field_count);
    for (size_t i = 0; i < field_count; ++i)
        sorted_field_names.elems[i] = (char*)str_pool_insert(type_set->str_pool, field_names[i]);
    qsort(sorted_field_names.elems, field_count, sizeof(char*), cmp_field_names);

#ifndef NDEBUG
    for (size_t i = 1; i < field_count; ++i)
        assert(strcmp(sorted_field_names.elems[i - 1], sorted_field_names.elems[i]) < 0);
#endif

    struct small_type_vec sorted_field_types;
    small_type_vec_init(&sorted_field_types);
    small_type_vec_resize(&sorted_field_types, field_count);
    for (size_t i = 0; i < field_count; ++i) {
        char** name_ptr = bsearch(&field_names[i], sorted_field_names.elems, field_count, sizeof(char*), cmp_field_names);
        assert(name_ptr);
        sorted_field_types.elems[(size_t)(name_ptr - sorted_field_names.elems)] = field_types[i];
    }

    const struct type* record_type = insert_type(type_set, &(struct type) {
        .tag = TYPE_RECORD,
        .record_type = {
            .field_types = sorted_field_types.elems,
            .field_names = (const char* const*)sorted_field_names.elems,
            .field_count = field_count
        }
    });

    small_string_vec_destroy(&sorted_field_names);
    small_type_vec_destroy(&sorted_field_types);
    return record_type;
}

const struct type* type_tuple(
    struct type_set* type_set,
    const struct type* const* arg_types,
    size_t arg_count)
{
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_TUPLE,
        .tuple_type = {
            .arg_types = arg_types,
            .arg_count = arg_count
        }
    });
}

const struct type* type_array(
    struct type_set* type_set,
    const struct type* elem_type,
    size_t elem_count)
{
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_ARRAY,
        .array_type = {
            .elem_type = elem_type,
            .elem_count = elem_count
        }
    });
}

const struct type* type_dyn_array(
    struct type_set* type_set,
    const struct type* elem_type)
{
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_DYN_ARRAY,
        .dyn_array_type.elem_type = elem_type
    });
}
