#include "check.h"
#include "ast.h"
#include "types.h"

#include "support/set.h"
#include "support/hash.h"
#include "support/log.h"

#include <stdint.h>
#include <assert.h>

static inline uint32_t hash_ast(struct ast* const* ast_ptr) {
    return hash_uint64(hash_init(), (uintptr_t)*ast_ptr);
}

static inline bool cmp_ast(struct ast* const* ast_ptr, struct ast* const* other_ptr) {
    return *ast_ptr == *other_ptr;
}

SET_DEFINE(ast_set, struct ast*, hash_ast, cmp_ast, PRIVATE)

struct type_checker {
    struct ast_set visited_decls;
    struct type_set* type_set;
    struct log* log;
};

static const struct type* cannot_infer(
    struct type_checker* type_checker,
    const struct fir_source_range* source_range,
    const char* name)
{
    log_error(type_checker->log, source_range, "cannot infer type for recursive symbol '%s'", name);
    return type_top(type_checker->type_set);
}

static const struct type* expect_type(
    struct type_checker* type_checker,
    const struct fir_source_range* source_range,
    const struct type* type,
    const struct type* expected_type)
{
    if (!type_is_subtype(type, expected_type)) {
        char* type_str = type_to_string(type);
        char* expected_type_str = type_to_string(expected_type);
        log_error(type_checker->log, source_range, "expected type '%s', but got '%s'",
            type_str, expected_type_str);
        free(type_str);
        free(expected_type_str);
        return type_top(type_checker->type_set);
    }
    return type;
}

static const struct type* coerce(
    struct type_checker* type_checker,
    struct ast** expr,
    const struct type* type)
{
    // TODO
    return type;
}

static const struct type* check(
    struct type_checker* type_checker,
    struct ast* expr,
    const struct type* type)
{
    // TODO
    return type;
}

static const struct type* infer(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUNC_DECL:
            if (!ast_set_insert(&type_checker->visited_decls, &ast))
                return cannot_infer(type_checker, &ast->source_range, ast->func_decl.name);
            break;
        default:
            assert(false && "invalid AST node");
            return type_top(type_checker->type_set);
    }
}

void check_program(struct ast* program, struct type_set* type_set, struct log* log) {
    assert(program->tag == AST_PROGRAM);
    struct type_checker type_checker = {
        .visited_decls = ast_set_create(),
        .type_set = type_set,
        .log = log
    };

    for (struct ast* decl = program->program.decls; decl; decl = decl->next)
        infer(&type_checker, decl);

    ast_set_destroy(&type_checker.visited_decls);
}
