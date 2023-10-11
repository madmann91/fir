#include "types.h"

#include "support/set.h"
#include "support/hash.h"
#include "support/str_pool.h"
#include "support/mem_pool.h"

static inline uint32_t hash_type(const struct type* const* type_ptr) {
    const struct type* type = *type_ptr;
    uint32_t h = hash_uint32(hash_init(), type->tag);
    switch (type->tag) {
        case TYPE_VARIANT: {
            h = hash_uint64(h, type->variant_type.option_count);
            for (size_t i = 0; i < type->variant_type.option_count; ++i)
                h = hash_uint64(h, type->variant_type.option_types[i]->id);
            break;
        }
        case TYPE_RECORD: {
            h = hash_uint64(h, type->record_type.field_count);
            if (type->record_type.field_names) {
                for (size_t i = 0; i < type->record_type.field_count; ++i)
                    h = hash_string(h, type->record_type.field_names[i]);
            }
            for (size_t i = 0; i < type->record_type.field_count; ++i)
                h = hash_uint64(h, type->record_type.field_types[i]->id);
            break;
        }
        case TYPE_FUNC:
            h = hash_uint64(h, type->func_type.param_type->id);
            h = hash_uint64(h, type->func_type.ret_type->id);
            break;
        default:
            break;
    }
    return h;
}

static inline bool cmp_type(
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
        case TYPE_FUNC:
            return
                type->func_type.param_type == other->func_type.param_type &&
                type->func_type.ret_type   == other->func_type.ret_type;
        default:
            return true;
    }
}

SET_DEFINE(internal_type_set, const struct type*, hash_type, cmp_type, PRIVATE)

struct type_set {
    uint64_t cur_id;
    struct str_pool* str_pool;
    struct mem_pool mem_pool;
    struct internal_type_set types;
};

void print_type(FILE* file, const struct type* type) {
    switch (type->tag) {
#define x(tag, str) case TYPE_##tag: fprintf(file, str); break;
        PRIM_TYPE_LIST(x)
#undef x
        case TYPE_VARIANT:
            for (size_t i = 0; i < type->variant_type.option_count; ++i) {
                print_type(file, type->variant_type.option_types[i]);
                if (i + 1 != type->variant_type.option_count)
                    fprintf(file, " | ");
            }
            break;
        case TYPE_RECORD:
            fprintf(file, "[");
            for (size_t i = 0; i < type->record_type.field_count; ++i) {
                if (type->record_type.field_names)
                    fprintf(file, "%s: ", type->record_type.field_names[i]);
                print_type(file, type->record_type.field_types[i]);
                if (i + 1 != type->record_type.field_count)
                    fprintf(file, ", ");
            }
            fprintf(file, "]");
            break;
        case TYPE_FUNC:
            fprintf(file, "func (");
            print_type(file, type->func_type.param_type);
            fprintf(file, ") -> ");
            print_type(file, type->func_type.ret_type);
            break;
        default:
            assert(false && "invalid type");
            break;
    }
}

void dump_type(const struct type* type) {
    print_type(stdout, type);
    printf("\n");
    fflush(stdout);
}

bool type_tag_is_prim_type(enum type_tag tag) {
    switch (tag) {
#define x(tag, ...) case TYPE_##tag:
        PRIM_TYPE_LIST(x)
#undef x
            return true;
        default:
            return false;
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

    struct type* new_type = xmalloc(sizeof(struct type));
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
        default:
            break;
    }
    [[maybe_unused]] bool was_inserted = internal_type_set_insert(&type_set->types, (const struct type* const*)&new_type);
    assert(was_inserted);
    return new_type;
}

const struct type* prim_type(struct type_set* type_set, enum type_tag tag) {
    assert(type_tag_is_prim_type(tag));
    return insert_type(type_set, &(struct type) { .tag = tag, });
}

const struct type* func_type(
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

const struct type* variant_type(
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

const struct type* record_type(
    struct type_set* type_set,
    const struct type* const* field_types,
    const char* const* field_names,
    size_t field_count)
{
    return insert_type(type_set, &(struct type) {
        .tag = TYPE_RECORD,
        .record_type = {
            .field_types = field_types,
            .field_names = field_names,
            .field_count = field_count
        }
    });
}
