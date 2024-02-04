#include "ast.h"

#include "support/log.h"
#include "support/map.h"

#include <assert.h>

MAP_DEFINE(symbol_table, struct str_view, struct ast*, str_view_hash, str_view_is_equal, PRIVATE)

struct env {
    struct symbol_table symbol_table;
    struct env* next;
    struct env* prev;
};

struct name_binder {
    struct env* env;
    struct log* log;
};

static inline struct env* alloc_env(struct env* prev) {
    struct env* env = xmalloc(sizeof(struct env));
    env->symbol_table = symbol_table_create();
    env->prev = prev;
    env->next = NULL;
    return env;
}

static inline void free_env(struct env* env) {
    if (!env)
        return;
    while (env->prev)
        env = env->prev;
    do {
        struct env* next = env->next;
        symbol_table_destroy(&env->symbol_table);
        free(env);
        env = next;
    } while (env);
}

static inline void push_env(struct name_binder* name_binder) {
    if (!name_binder->env->next)
        name_binder->env->next = alloc_env(name_binder->env);
    name_binder->env = name_binder->env->next;
    symbol_table_clear(&name_binder->env->symbol_table);
}

static inline void pop_env(struct name_binder* name_binder) {
    assert(name_binder->env->prev);
    name_binder->env = name_binder->env->prev;
}

static struct ast* find_symbol(
    struct name_binder* name_binder,
    struct fir_source_range* source_range,
    const char* name)
{
    struct env* env = name_binder->env;
    while (env) {
        struct ast* const* symbol = symbol_table_find(&env->symbol_table, &STR_VIEW(name));
        if (symbol)
            return *symbol;
        env = env->prev;
    }
    log_error(name_binder->log, source_range, "unknown identifier '%s'", name);
    return NULL;
}

static bool insert_symbol(
    struct name_binder* name_binder,
    const char* name,
    struct ast* symbol)
{
    if (!symbol_table_insert(&name_binder->env->symbol_table, &STR_VIEW(name), &symbol)) {
        log_error(name_binder->log, &symbol->source_range, "identifier '%s' already exists", name);
        return false;
    }
    return true;
}

static void bind_head(struct name_binder* name_binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUNC_DECL:
            insert_symbol(name_binder, ast->func_decl.name, ast);
            break;
        default:
            break;
    }
}

static void bind(struct name_binder* name_binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_ERROR:
        case AST_PRIM_TYPE:
        case AST_LITERAL:
            break;
        case AST_FUNC_DECL:
            push_env(name_binder);
            bind(name_binder, ast->func_decl.param);
            bind(name_binder, ast->func_decl.body);
            pop_env(name_binder);
            break;
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            if (ast->const_decl.init)
                bind(name_binder, ast->const_decl.init);
            bind(name_binder, ast->const_decl.pattern);
            break;
        case AST_IDENT_EXPR:
            ast->ident_expr.bound_to = find_symbol(name_binder, &ast->source_range, ast->ident_expr.name);
            break;
        case AST_IDENT_PATTERN:
            insert_symbol(name_binder, ast->ident_pattern.name, ast);
            if (ast->ident_pattern.type)
                bind(name_binder, ast->ident_pattern.type);
            break;
        case AST_FIELD_TYPE:
        case AST_FIELD_EXPR:
        case AST_FIELD_PATTERN:
            bind(name_binder, ast->field_type.arg);
            break;
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            for (struct ast* field = ast->record_type.fields; field; field = field->next)
                bind(name_binder, field);
            break;
        case AST_TUPLE_TYPE:
        case AST_TUPLE_EXPR:
        case AST_TUPLE_PATTERN:
            for (struct ast* arg = ast->tuple_type.args; arg; arg = arg->next)
                bind(name_binder, arg);
            break;
        case AST_BLOCK_EXPR:
            for (struct ast* stmt = ast->block_expr.stmts; stmt; stmt = stmt->next)
                bind_head(name_binder, stmt);
            for (struct ast* stmt = ast->block_expr.stmts; stmt; stmt = stmt->next)
                bind(name_binder, stmt);
            break;
        case AST_UNARY_EXPR:
            bind(name_binder, ast->unary_expr.arg);
            break;
        case AST_BINARY_EXPR:
            bind(name_binder, ast->unary_expr.arg);
            break;
        case AST_IF_EXPR:
            bind(name_binder, ast->if_expr.cond);
            bind(name_binder, ast->if_expr.then_block);
            if (ast->if_expr.else_block)
                bind(name_binder, ast->if_expr.else_block);
            break;
        case AST_CALL_EXPR:
            bind(name_binder, ast->call_expr.callee);
            bind(name_binder, ast->call_expr.arg);
            break;
        case AST_WHILE_LOOP:
            bind(name_binder, ast->while_loop.cond);
            bind(name_binder, ast->while_loop.body);
            break;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

void ast_bind(struct ast* ast, struct log* log) {
    assert(ast->tag == AST_PROGRAM);
    struct name_binder name_binder = {
        .env = alloc_env(NULL),
        .log = log
    };
    for (struct ast* decl = ast->program.decls; decl; decl = decl->next)
        bind_head(&name_binder, decl);
    for (struct ast* decl = ast->program.decls; decl; decl = decl->next)
        bind(&name_binder, decl);
    free_env(name_binder.env);
}
