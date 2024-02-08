#include "ast.h"
#include "types.h"

#include "support/term.h"

#include <assert.h>
#include <inttypes.h>

static inline uint32_t hash_ast(uint32_t h, struct ast* const* ast_ptr) {
    return hash_uint64(h, (uintptr_t)*ast_ptr);
}

static inline bool is_ast_equal(struct ast* const* ast_ptr, struct ast* const* other_ptr) {
    return *ast_ptr == *other_ptr;
}

SET_IMPL(ast_set, struct ast*, hash_ast, is_ast_equal, PUBLIC)

static inline void print_indent(FILE* file, size_t indent, const char* tab) {
    for (size_t i = 0; i < indent; ++i)
        fputs(tab, file);
}

static inline void print_many(
    FILE* file,
    const char* begin,
    const char* sep,
    const char* end,
    const struct ast* ast,
    const struct fir_print_options* options)
{
    fprintf(file, "%s", begin);
    for (; ast; ast = ast->next) {
        ast_print(file, ast, options);
        if (ast->next)
            fprintf(file, "%s", sep);
    }
    fprintf(file, "%s", end);
}

static void print_prim_type(FILE* file, enum prim_type_tag tag) {
    switch (tag) {
#define x(tag, str) case PRIM_TYPE_##tag: fprintf(file, str); break;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid primitive type");
            break;
    }
}

static void print_literal(FILE* file, const struct literal* literal) {
    if (literal->tag == LITERAL_BOOL)
        fprintf(file, literal->bool_val ? "true" : "false");
    else if (literal->tag == LITERAL_INT)
        fprintf(file, "%"PRIuMAX, literal->int_val);
    else if (literal->tag == LITERAL_FLOAT)
        fprintf(file, "%g", literal->float_val);
}

static void print_with_parens(
    FILE* file,
    const struct ast* ast,
    const struct fir_print_options* options)
{
    if (ast->tag == AST_TUPLE_TYPE ||
        ast->tag == AST_TUPLE_EXPR ||
        ast->tag == AST_TUPLE_PATTERN)
        ast = ast->tuple_type.args;
    print_many(file, "(", ", ", ")", ast, options);
}

static void print_unary_expr_operand(
    FILE* file,
    const struct ast* operand,
    const struct fir_print_options* options)
{
    if (operand->tag == AST_UNARY_EXPR)
        print_with_parens(file, operand, options);
    else
        ast_print(file, operand, options);
}

static void print_binary_expr_operand(
    FILE* file,
    const struct ast* operand,
    const struct fir_print_options* options,
    int precedence)
{
    bool needs_parens =
        operand->tag == AST_BINARY_EXPR &&
        binary_expr_tag_to_precedence(operand->binary_expr.tag) > precedence;
    if (needs_parens)
        print_with_parens(file, operand, options);
    else
        ast_print(file, operand, options);
}

void ast_print(FILE* file, const struct ast* ast, const struct fir_print_options* options) {
    const char* keyword_style = options->disable_colors ? "" : TERM2(TERM_FG_GREEN, TERM_BOLD);
    const char* literal_style = options->disable_colors ? "" : TERM1(TERM_FG_CYAN);
    const char* error_style = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD);
    const char* reset_style = options->disable_colors ? "" : TERM1(TERM_RESET);
    switch (ast->tag) {
        case AST_ERROR:
            fprintf(file, "%s<ERROR>%s", error_style, reset_style);
            break;
        case AST_PROGRAM:
            print_many(file, "", "\n", "", ast->program.decls, options);
            fprintf(file, "\n");
            break;
        case AST_LITERAL:
            fprintf(file, "%s", literal_style);
            print_literal(file, &ast->literal);
            fprintf(file, "%s", reset_style);
            break;
        case AST_PRIM_TYPE:
            fprintf(file, "%s", keyword_style);
            print_prim_type(file, ast->prim_type.tag);
            fprintf(file, "%s", reset_style);
            break;
        case AST_IDENT_EXPR:
            fprintf(file, "%s", ast->ident_expr.name);
            break;
        case AST_IDENT_PATTERN:
            fprintf(file, "%s", ast->ident_pattern.name);
            if (ast->ident_pattern.type) {
                fprintf(file, ": ");
                ast_print(file, ast->ident_pattern.type, options);
            }
            break;
        case AST_CAST_EXPR:
            if (ast_is_implicit_cast(ast) && options->verbosity < FIR_VERBOSITY_HIGH) {
                ast_print(file, ast->cast_expr.arg, options);
                return;
            }

            fprintf(file, "(");
            ast_print(file, ast->cast_expr.arg, options);
            fprintf(file, " %sas%s ", keyword_style, reset_style);
            type_print(file, ast->type);
            fprintf(file, ")");
            break;
        case AST_FIELD_TYPE:
        case AST_FIELD_EXPR:
        case AST_FIELD_PATTERN:
            if (ast->field_type.name)
                fprintf(file, ast->tag == AST_FIELD_TYPE ? "%s: " : "%s = ", ast->field_type.name);
            ast_print(file, ast->field_type.arg, options);
            break;
        case AST_RECORD_TYPE:
        case AST_RECORD_EXPR:
        case AST_RECORD_PATTERN:
            print_many(file, "[", ", ", "]", ast->record_type.fields, options);
            break;
        case AST_TUPLE_TYPE:
        case AST_TUPLE_EXPR:
        case AST_TUPLE_PATTERN:
            print_many(file, "(", ", ", ")", ast->tuple_type.args, options);
            break;
        case AST_BLOCK_EXPR:
            if (!ast->block_expr.stmts) {
                fprintf(file, "{}");
            } else {
                struct fir_print_options block_options = *options;
                block_options.indent++;
                fprintf(file, "{\n");
                for (struct ast* stmt = ast->block_expr.stmts; stmt; stmt = stmt->next) {
                    print_indent(file, block_options.indent, block_options.tab);
                    ast_print(file, stmt, &block_options);
                    if ((stmt->next && ast_needs_semicolon(stmt)) ||
                        (!stmt->next && ast->block_expr.ends_with_semicolon))
                        fprintf(file, ";");
                    fprintf(file, "\n");
                }
                print_indent(file, options->indent, options->tab);
                fprintf(file, "}");
            }
            break;
        case AST_UNARY_EXPR:
            if (unary_expr_tag_is_prefix(ast->unary_expr.tag))
                fprintf(file, "%s", unary_expr_tag_to_string(ast->unary_expr.tag));
            print_unary_expr_operand(file, ast->unary_expr.arg, options);
            if (!unary_expr_tag_is_prefix(ast->unary_expr.tag))
                fprintf(file, "%s", unary_expr_tag_to_string(ast->unary_expr.tag));
            break;
        case AST_BINARY_EXPR:
        {
            int prec = binary_expr_tag_to_precedence(ast->binary_expr.tag);
            print_binary_expr_operand(file, ast->binary_expr.left, options, prec);
            fprintf(file, " %s ", binary_expr_tag_to_string(ast->binary_expr.tag));
            print_binary_expr_operand(file, ast->binary_expr.right, options, prec);
            break;
        }
        case AST_IF_EXPR:
            fprintf(file, "%sif%s ", keyword_style, reset_style);
            ast_print(file, ast->if_expr.cond, options);
            fprintf(file, " ");
            ast_print(file, ast->if_expr.then_block, options);
            if (ast->if_expr.else_block) {
                fprintf(file, " %selse%s ", keyword_style, reset_style);
                ast_print(file, ast->if_expr.else_block, options);
            }
            break;
        case AST_CALL_EXPR:
            ast_print(file, ast->call_expr.callee, options);
            print_with_parens(file, ast->call_expr.arg, options);
            break;
        case AST_WHILE_LOOP:
            fprintf(file, "%swhile%s ", keyword_style, reset_style);
            ast_print(file, ast->while_loop.cond, options);
            fprintf(file, " ");
            ast_print(file, ast->while_loop.body, options);
            break;
        case AST_FUNC_DECL:
            fprintf(file, "%sfunc%s %s", keyword_style, reset_style, ast->func_decl.name);
            print_with_parens(file, ast->func_decl.param, options);
            if (ast->func_decl.ret_type) {
                fprintf(file, " -> ");
                ast_print(file, ast->func_decl.ret_type, options);
            }
            if (ast->func_decl.body) {
                fprintf(file, " = ");
                ast_print(file, ast->func_decl.body, options);
            }
            fprintf(file, ";");
            break;
        case AST_VAR_DECL:
            fprintf(file, "%svar%s ", keyword_style, reset_style);
            ast_print(file, ast->var_decl.pattern, options);
            if (ast->var_decl.init) {
                fprintf(file, " = ");
                ast_print(file, ast->var_decl.init, options);
            }
            fprintf(file, ";");
            break;
        case AST_CONST_DECL:
            fprintf(file, "%sconst%s ", keyword_style, reset_style);
            ast_print(file, ast->const_decl.pattern, options);
            fprintf(file, " = ");
            ast_print(file, ast->const_decl.init, options);
            fprintf(file, ";");
            break;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

void ast_dump(const struct ast* ast) {
    struct fir_print_options options = fir_print_options_default(stdout);
    ast_print(stdout, ast, &options);
    printf("\n");
    fflush(stdout);
}

const char* binary_expr_tag_to_string(enum binary_expr_tag tag) {
    switch (tag) {
#define x(tag, str, ...) case BINARY_EXPR_##tag: return str;
        BINARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid binary expression");
            return "";
    }
}

const char* unary_expr_tag_to_string(enum unary_expr_tag tag) {
    switch (tag) {
#define x(tag, str) case UNARY_EXPR_##tag: return str;
        UNARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid unary expression");
            return "";
    }
}

bool unary_expr_tag_is_prefix(enum unary_expr_tag tag) {
    switch (tag) {
        case UNARY_EXPR_POST_INC:
        case UNARY_EXPR_POST_DEC:
            return false;
        default:
            return true;
    }
}

bool unary_expr_tag_is_inc(enum unary_expr_tag tag) {
    return tag == UNARY_EXPR_PRE_INC || tag == UNARY_EXPR_POST_INC;
}

bool unary_expr_tag_is_dec(enum unary_expr_tag tag) {
    return tag == UNARY_EXPR_PRE_DEC || tag == UNARY_EXPR_POST_DEC;
}

bool unary_expr_tag_is_inc_or_dec(enum unary_expr_tag tag) {
    return unary_expr_tag_is_inc(tag) || unary_expr_tag_is_dec(tag);
}

int binary_expr_tag_to_precedence(enum binary_expr_tag tag) {
    switch (tag) {
#define x(tag, str, prec) case BINARY_EXPR_##tag: return prec;
        BINARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid binary expression");
            return 0;
    }
}

bool binary_expr_tag_is_assign(enum binary_expr_tag tag) {
    switch (tag) {
        case BINARY_EXPR_ASSIGN:
        case BINARY_EXPR_ADD_ASSIGN:
        case BINARY_EXPR_SUB_ASSIGN:
        case BINARY_EXPR_MUL_ASSIGN:
        case BINARY_EXPR_DIV_ASSIGN:
        case BINARY_EXPR_REM_ASSIGN:
        case BINARY_EXPR_RSHIFT_ASSIGN:
        case BINARY_EXPR_LSHIFT_ASSIGN:
        case BINARY_EXPR_AND_ASSIGN:
        case BINARY_EXPR_XOR_ASSIGN:
        case BINARY_EXPR_OR_ASSIGN:
            return true;
        default:
            return false;
    }
}

bool binary_expr_tag_is_cmp(enum binary_expr_tag tag) {
    switch (tag) {
        case BINARY_EXPR_CMP_EQ:
        case BINARY_EXPR_CMP_NE:
        case BINARY_EXPR_CMP_LT:
        case BINARY_EXPR_CMP_GT:
        case BINARY_EXPR_CMP_LE:
        case BINARY_EXPR_CMP_GE:
            return true;
        default:
            return false;
    }
}

enum binary_expr_tag binary_expr_tag_remove_assign(enum binary_expr_tag tag) {
    switch (tag) {
        case BINARY_EXPR_ADD_ASSIGN:    return BINARY_EXPR_ADD;
        case BINARY_EXPR_SUB_ASSIGN:    return BINARY_EXPR_SUB;
        case BINARY_EXPR_MUL_ASSIGN:    return BINARY_EXPR_MUL;
        case BINARY_EXPR_DIV_ASSIGN:    return BINARY_EXPR_DIV;
        case BINARY_EXPR_REM_ASSIGN:    return BINARY_EXPR_REM;
        case BINARY_EXPR_RSHIFT_ASSIGN: return BINARY_EXPR_RSHIFT;
        case BINARY_EXPR_LSHIFT_ASSIGN: return BINARY_EXPR_LSHIFT;
        case BINARY_EXPR_AND_ASSIGN:    return BINARY_EXPR_AND;
        case BINARY_EXPR_XOR_ASSIGN:    return BINARY_EXPR_XOR;
        case BINARY_EXPR_OR_ASSIGN:     return BINARY_EXPR_OR;
        default:
            return tag;
    }
}

bool ast_is_implicit_cast(const struct ast* ast) {
    return ast->tag == AST_CAST_EXPR && !ast->cast_expr.type;
}

bool ast_needs_semicolon(const struct ast* ast) {
    switch (ast->tag) {
        case AST_BLOCK_EXPR:
        case AST_IF_EXPR:
        case AST_WHILE_LOOP:
        case AST_CONST_DECL:
        case AST_VAR_DECL:
        case AST_FUNC_DECL:
            return false;
        default:
            return true;
    }
}

bool ast_is_irrefutable_pattern(const struct ast* ast) {
    switch (ast->tag) {
        case AST_IDENT_PATTERN:
            return true;
        case AST_TUPLE_PATTERN:
            for (struct ast* arg = ast->tuple_pattern.args; arg; arg = arg->next) {
                if (!ast_is_irrefutable_pattern(arg))
                    return false;
            }
            return true;
        default:
            return false;
    }
}
