#include "ast.h"
#include "types.h"

#include "support/datatypes.h"

#include <fir/node.h>
#include <fir/block.h>
#include <fir/module.h>

#include <assert.h>

struct emitter {
    enum fir_fp_flags fp_flags;
    struct fir_mod* mod;
    struct fir_block block;
};

static const struct fir_node* emit(struct emitter*, struct ast*);

static void emit_pattern(
    struct emitter* emitter,
    struct ast* pattern,
    const struct fir_node* val)
{
    switch (pattern->tag) {
        case AST_TUPLE_PATTERN: {
            size_t i = 0;
            for (struct ast* arg = pattern->tuple_pattern.args; arg; arg = arg->next)
                emit_pattern(emitter, arg, fir_ext_at(val, i++));
            break;
        }
        case AST_IDENT_PATTERN:
            pattern->node = pattern->ident_pattern.is_var
                ? fir_local(fir_func_frame(emitter->block.func), val) : val;
            break;
        default:
            assert(false && "invalid pattern");
            break;
    }
}

static const struct fir_node* convert_type(struct emitter* emitter, const struct type* type) {
    switch (type->tag) {
#define x(tag, ...) case TYPE_##tag:
        PRIM_TYPE_LIST(x)
#undef x
            if (type_is_int(type) || type->tag == TYPE_BOOL)
                return fir_int_ty(emitter->mod, type_bitwidth(type));
            if (type_is_float(type))
                return fir_float_ty(emitter->mod, type_bitwidth(type));
            assert(false && "invalid prim type");
            return NULL;
        case TYPE_FUNC:
            return fir_mem_func_ty(
                convert_type(emitter, type->func_type.param_type),
                convert_type(emitter, type->func_type.ret_type));
        case TYPE_RECORD:
        case TYPE_TUPLE: {
            struct small_node_vec args_ty;
            small_node_vec_init(&args_ty);
            const size_t arg_count = type->tag == TYPE_TUPLE
                ? type->tuple_type.arg_count
                : type->record_type.field_count;
            const struct type* const* arg_types = type->tag == TYPE_TUPLE
                ? type->tuple_type.arg_types
                : type->record_type.field_types;
            for (size_t i = 0; i < arg_count; ++i) {
                const struct fir_node* arg_ty = convert_type(emitter, arg_types[i]);
                small_node_vec_push(&args_ty, &arg_ty);
            }
            const struct fir_node* tup_ty = fir_tup_ty(emitter->mod, args_ty.elems, args_ty.elem_count);
            small_node_vec_destroy(&args_ty);
            return tup_ty;
        }
        case TYPE_REF:
        case TYPE_PTR:
            return fir_ptr_ty(emitter->mod);
        default:
            assert(false && "invalid type");
            return NULL;
    }
}

static const struct fir_node* emit_func_decl(struct emitter* emitter, struct ast* func_decl) {
    assert(func_decl->tag == AST_FUNC_DECL);
    assert(func_decl->node);
    struct fir_node* func = (struct fir_node*)func_decl->node;
    const struct fir_node* param = fir_block_start(&emitter->block, func);
    emit_pattern(emitter, func_decl->func_decl.param, param);
    const struct fir_node* ret_val = emit(emitter, func_decl->func_decl.body);
    fir_block_return(&emitter->block, ret_val);
    fir_node_make_external(func);
    return func;
}

static const struct fir_node* emit_tuple_expr(struct emitter* emitter, struct ast* tuple_expr) {
    struct small_node_vec args;
    small_node_vec_init(&args);
    for (struct ast* arg = tuple_expr->tuple_expr.args; arg; arg = arg->next)
        small_node_vec_push(&args, (const struct fir_node*[]) { emit(emitter, arg) });
    const struct fir_node* tup = fir_tup(emitter->mod, args.elems, args.elem_count);
    small_node_vec_destroy(&args);
    return tup;
}

static const struct fir_node* emit_record_expr(struct emitter* emitter, struct ast* record_expr) {
    const struct type* record_type = record_expr->type;
    assert(record_type->tag == TYPE_RECORD);

    struct small_node_vec args;
    small_node_vec_init(&args);
    small_node_vec_resize(&args, record_type->record_type.field_count);
    for (struct ast* field = record_expr->record_expr.fields; field; field = field->next) {
        size_t index = type_find_field(record_type, field->field_expr.name);
        assert(index < record_type->record_type.field_count);
        args.elems[index] = emit(emitter, field);
    }

    const struct fir_node* tup = fir_tup(emitter->mod, args.elems, args.elem_count);
    small_node_vec_destroy(&args);
    return tup;
}

static const struct fir_node* emit_literal(
    struct emitter* emitter,
    struct literal* literal,
    const struct type* type)
{
    if (literal->tag == LITERAL_BOOL)
        return fir_int_const(fir_bool_ty(emitter->mod), literal->bool_val ? 1 : 0);
    else if (literal->tag == LITERAL_INT)
        return fir_int_const(convert_type(emitter, type), literal->int_val);
    else if (literal->tag == LITERAL_FLOAT)
        return fir_float_const(convert_type(emitter, type), literal->float_val);
    assert(false && "invalid literal");
    return NULL;
}

static const struct fir_node* emit_block_expr(struct emitter* emitter, struct ast* block_expr) {
    const struct fir_node* last_val = NULL;
    for (struct ast* stmt = block_expr->block_expr.stmts; stmt; stmt = stmt->next) {
        last_val = emit(emitter, stmt);
        if (stmt->next || block_expr->block_expr.ends_with_semicolon)
            last_val = NULL;
    }
    return last_val ? last_val : fir_unit(emitter->mod);
}

static void emit_cond(
    struct emitter* emitter,
    struct ast* cond,
    struct fir_block* branch_true,
    struct fir_block* branch_false,
    const struct fir_block* merge_block)
{
    if (ast_is_logic_expr(cond)) {
        struct fir_block next_block = fir_block_merge(emitter->block.func);
        emit_cond(emitter, cond->binary_expr.left,
            cond->binary_expr.tag == BINARY_EXPR_LOGIC_OR  ? branch_true  : &next_block,
            cond->binary_expr.tag == BINARY_EXPR_LOGIC_AND ? branch_false : &next_block,
            &next_block);

        emitter->block = next_block;
        emit_cond(emitter, cond->binary_expr.right, branch_true, branch_false, merge_block);
    } else {
        const struct fir_node* cond_val = emit(emitter, cond);
        struct fir_block cond_true;
        struct fir_block cond_false;
        fir_block_branch(&emitter->block, cond_val, &cond_true, &cond_false, merge_block);

        emitter->block = cond_true;
        fir_block_jump(&emitter->block, branch_true);
        emitter->block = cond_false;
        fir_block_jump(&emitter->block, branch_false);
    }
}

static const struct fir_node* emit_if_expr(struct emitter* emitter, struct ast* if_expr) {
    const struct fir_node* local_ty = NULL;
    const struct fir_node* local = NULL;

    if (!type_is_unit(if_expr->type)) {
        local_ty = convert_type(emitter, if_expr->type);
        local = fir_local(fir_func_frame(emitter->block.func), fir_bot(local_ty));
    }

    struct fir_block then_block = fir_block_merge(emitter->block.func);
    struct fir_block else_block = fir_block_merge(emitter->block.func);
    struct fir_block merge_block = fir_block_merge(emitter->block.func);
    emit_cond(emitter, if_expr->if_expr.cond, &then_block, &else_block, &merge_block);

    emitter->block = then_block;
    const struct fir_node* then_val = emit(emitter, if_expr->if_expr.then_block);
    if (local)
        fir_block_store(&emitter->block, FIR_MEM_NON_NULL, local, then_val);
    fir_block_jump(&emitter->block, &merge_block);

    emitter->block = else_block;
    if (if_expr->if_expr.else_block) {
        const struct fir_node* else_val = emit(emitter, if_expr->if_expr.else_block);
        if (local)
            fir_block_store(&emitter->block, FIR_MEM_NON_NULL, local, else_val);
    }
    fir_block_jump(&emitter->block, &merge_block);

    emitter->block = merge_block;
    return local ? fir_block_load(&emitter->block, FIR_MEM_NON_NULL, local, local_ty) : fir_unit(emitter->mod);
}

static const struct fir_node* emit_call_expr(struct emitter* emitter, struct ast* call_expr) {
    const struct fir_node* callee = emit(emitter, call_expr->call_expr.callee);
    const struct fir_node* arg = emit(emitter, call_expr->call_expr.arg);
    return fir_block_call(&emitter->block, callee, arg);
}

static const struct fir_node* emit_const_or_var_decl(struct emitter* emitter, struct ast* decl) {
    const struct fir_node* val = decl->const_decl.init
        ? emit(emitter, decl->const_decl.init)
        : fir_bot(convert_type(emitter, decl->const_decl.pattern->type));
    emit_pattern(emitter, decl->const_decl.pattern, val);
    return fir_unit(emitter->mod);
}

static const struct fir_node* emit_implicit_cast(
    struct emitter* emitter,
    const struct fir_node* val,
    const struct type* source_type,
    const struct type* target_type)
{
    if (source_type == target_type)
        return val;
    if (source_type->tag == TYPE_REF && target_type->tag != TYPE_REF) {
        const struct fir_node* load_ty = convert_type(emitter, source_type->ref_type.pointee_type);
        const struct fir_node* loaded_val = fir_block_load(&emitter->block, 0, val, load_ty);
        return emit_implicit_cast(emitter, loaded_val, source_type->ref_type.pointee_type, target_type);
    }
    if (type_is_signed_int(source_type) && type_is_signed_int(target_type))
        return fir_cast_op(FIR_SEXT, convert_type(emitter, target_type), val);
    if (type_is_unsigned_int(source_type) && type_is_unsigned_int(target_type))
        return fir_cast_op(FIR_ZEXT, convert_type(emitter, target_type), val);
    if (type_is_float(source_type) && type_is_float(target_type))
        return fir_cast_op(FIR_FEXT, convert_type(emitter, target_type), val);
    if (source_type->tag == TYPE_RECORD && target_type->tag == TYPE_RECORD) {
        const struct fir_node* tup = fir_bot(convert_type(emitter, target_type));
        for (size_t i = 0; i < target_type->record_type.field_count; ++i) {
            size_t field_index = type_find_field(source_type, target_type->record_type.field_names[i]);
            assert(field_index < source_type->record_type.field_count);
            tup = fir_ins_at(tup, i, fir_ext_at(val, field_index));
        }
        return tup;
    }
    assert(false && "mismatch between the subtyping relation and emitting code");
    return val;
}

static const struct fir_node* emit_cast_expr(struct emitter* emitter, struct ast* cast_expr) {
    const struct fir_node* arg = emit(emitter, cast_expr->cast_expr.arg);

    const struct type* source_type = cast_expr->cast_expr.arg->type;
    const struct type* dest_type   = cast_expr->type;
    if (type_is_subtype(source_type, dest_type))
        return emit_implicit_cast(emitter, arg, source_type, dest_type);

    const struct fir_node* cast_ty = convert_type(emitter, dest_type);
    if (type_is_float(source_type) && type_is_int_or_bool(dest_type))
        return fir_cast_op(type_is_signed_int(dest_type) ? FIR_FTOS : FIR_FTOU, cast_ty, arg);
    if (type_is_int_or_bool(source_type) && type_is_float(dest_type))
        return fir_cast_op(type_is_signed_int(source_type) ? FIR_STOF : FIR_UTOF, cast_ty, arg);
    if ((type_is_int_or_bool(source_type) && type_is_int_or_bool(dest_type)) ||
        (type_is_float(source_type) && type_is_float(dest_type)))
    {
        assert(type_bitwidth(source_type) >= type_bitwidth(dest_type));
        return fir_cast_op(type_is_float(source_type) ? FIR_FTRUNC : FIR_ITRUNC, cast_ty, arg);
    }
    return NULL;
}

static const struct fir_node* emit_unary_expr(struct emitter* emitter, struct ast* unary_expr) {
    const struct fir_node* arg  = emit(emitter, unary_expr->unary_expr.arg);
    switch (unary_expr->unary_expr.tag) {
        case UNARY_EXPR_PLUS:
            return arg;
        case UNARY_EXPR_NOT:
            return fir_not(arg);
        case UNARY_EXPR_NEG:
            if (type_is_float(unary_expr->type))
                return fir_fneg(emitter->fp_flags, arg);
            return fir_ineg(arg);
        case UNARY_EXPR_PRE_INC:
        case UNARY_EXPR_PRE_DEC:
        case UNARY_EXPR_POST_INC:
        case UNARY_EXPR_POST_DEC:
            {
                const struct fir_node* val_ty = convert_type(emitter, unary_expr->type);
                const struct fir_node* old_val = fir_block_load(&emitter->block, 0, arg, val_ty);
                const struct fir_node* new_val = unary_expr_tag_is_inc(unary_expr->unary_expr.tag)
                    ? fir_iarith_op(FIR_IADD, old_val, fir_one(val_ty))
                    : fir_iarith_op(FIR_ISUB, old_val, fir_one(val_ty));
                fir_block_store(&emitter->block, 0, arg, new_val);
                return unary_expr_tag_is_prefix(unary_expr->unary_expr.tag) ? new_val : old_val;
            }
        default:
            assert(false && "invalid unary operation");
            return NULL;
    }
}

static inline const struct fir_node* emit_arith_op(
    const struct emitter* emitter,
    const struct type* type,
    enum fir_node_tag signed_tag,
    enum fir_node_tag unsigned_tag,
    enum fir_node_tag float_tag,
    const struct fir_node* left,
    const struct fir_node* right)
{
    if (type_is_signed_int(type))
        return fir_iarith_op(signed_tag, left, right);
    if (type_is_unsigned_int(type))
        return fir_iarith_op(unsigned_tag, left, right);
    if (type_is_float(type))
        return fir_farith_op(float_tag, emitter->fp_flags, left, right);
    assert(false && "invalid arithmetic operation");
    return NULL;
}

static inline const struct fir_node* emit_cmp_op(
    const struct emitter* emitter,
    const struct type* type,
    enum fir_node_tag signed_tag,
    enum fir_node_tag unsigned_tag,
    enum fir_node_tag ordered_tag,
    enum fir_node_tag unordered_tag,
    const struct fir_node* left,
    const struct fir_node* right)
{
    if (type_is_signed_int(type))
        return fir_icmp_op(signed_tag, left, right);
    if (type_is_unsigned_int(type))
        return fir_icmp_op(unsigned_tag, left, right);
    if (type_is_float(type)) {
        if (emitter->fp_flags & FIR_FP_FINITE_ONLY)
            return fir_fcmp_op(ordered_tag, left, right);
        return fir_fcmp_op(unordered_tag, left, right);
    }
    assert(false && "invalid comparison");
    return NULL;
}

static inline const struct fir_node* emit_shift_op(
    const struct type* type,
    enum fir_node_tag signed_tag,
    enum fir_node_tag unsigned_tag,
    const struct fir_node* left,
    const struct fir_node* right)
{
    if (type_is_signed_int(type))
        return fir_shift_op(signed_tag, left, right);
    if (type_is_unsigned_int(type))
        return fir_shift_op(unsigned_tag, left, right);
    assert(false && "invalid shift operation");
    return NULL;
}

static inline const struct fir_node* emit_binary_expr(struct emitter* emitter, struct ast* binary_expr) {
    const struct type* left_type = binary_expr->binary_expr.left->type;
    const struct fir_node* left  = emit(emitter, binary_expr->binary_expr.left);
    const struct fir_node* right = emit(emitter, binary_expr->binary_expr.right);
    const struct fir_node* ptr = NULL;
    bool is_assign = binary_expr_tag_is_assign(binary_expr->binary_expr.tag);

    if (is_assign) {
        assert(left_type->tag == TYPE_REF);
        assert(left->ty->tag == FIR_PTR_TY);
        const struct type* pointee_type = binary_expr->binary_expr.left->type->ref_type.pointee_type;
        ptr = left;
        left = fir_block_load(&emitter->block, 0, ptr, convert_type(emitter, pointee_type));
        left_type = type_remove_ref(left_type);
    } else if (ast_is_logic_expr(binary_expr)) {
        struct fir_block branch_true  = fir_block_merge(emitter->block.func);
        struct fir_block branch_false = fir_block_merge(emitter->block.func);
        struct fir_block merge_block  = fir_block_merge(emitter->block.func);
        const struct fir_node* local = fir_local(fir_func_frame(emitter->block.func), fir_bot(fir_bool_ty(emitter->mod)));

        emit_cond(emitter, binary_expr, &branch_true, &branch_false, &merge_block);

        emitter->block = branch_true;
        fir_block_store(&emitter->block, FIR_MEM_NON_NULL, local, fir_bool_const(emitter->mod, true));
        fir_block_jump(&emitter->block, &merge_block);
        emitter->block = branch_false;
        fir_block_store(&emitter->block, FIR_MEM_NON_NULL, local, fir_bool_const(emitter->mod, false));
        fir_block_jump(&emitter->block, &merge_block);

        emitter->block = merge_block;
        return fir_block_load(&emitter->block, FIR_MEM_NON_NULL, local, fir_bool_ty(emitter->mod));
    }

    const struct fir_node* result = NULL;
    switch (binary_expr_tag_remove_assign(binary_expr->binary_expr.tag)) {
        case BINARY_EXPR_ASSIGN: result = right; break;
        case BINARY_EXPR_MUL:    result = emit_arith_op(emitter, left_type, FIR_IMUL, FIR_IMUL, FIR_FMUL, left, right); break;
        case BINARY_EXPR_DIV:    result = emit_arith_op(emitter, left_type, FIR_SDIV, FIR_UDIV, FIR_FDIV, left, right); break;
        case BINARY_EXPR_REM:    result = emit_arith_op(emitter, left_type, FIR_SREM, FIR_UREM, FIR_FREM, left, right); break;
        case BINARY_EXPR_ADD:    result = emit_arith_op(emitter, left_type, FIR_IADD, FIR_IADD, FIR_FADD, left, right); break;
        case BINARY_EXPR_SUB:    result = emit_arith_op(emitter, left_type, FIR_ISUB, FIR_ISUB, FIR_FSUB, left, right); break;
        case BINARY_EXPR_LSHIFT: result = emit_shift_op(left_type, FIR_SHL, FIR_SHL, left, right); break; 
        case BINARY_EXPR_RSHIFT: result = emit_shift_op(left_type, FIR_ASHR, FIR_LSHR, left, right); break;
        case BINARY_EXPR_CMP_GT: result = emit_cmp_op(emitter, left_type, FIR_SCMPGT, FIR_UCMPGT, FIR_FCMPOGT, FIR_FCMPUGT, left, right); break;
        case BINARY_EXPR_CMP_LT: result = emit_cmp_op(emitter, left_type, FIR_SCMPLT, FIR_UCMPLT, FIR_FCMPOLT, FIR_FCMPULT, left, right); break;
        case BINARY_EXPR_CMP_GE: result = emit_cmp_op(emitter, left_type, FIR_SCMPGE, FIR_UCMPGE, FIR_FCMPOGE, FIR_FCMPUGE, left, right); break;
        case BINARY_EXPR_CMP_LE: result = emit_cmp_op(emitter, left_type, FIR_SCMPLE, FIR_UCMPLE, FIR_FCMPOLE, FIR_FCMPULE, left, right); break;
        case BINARY_EXPR_CMP_NE: result = emit_cmp_op(emitter, left_type, FIR_ICMPNE, FIR_ICMPNE, FIR_FCMPONE, FIR_FCMPUNE, left, right); break;
        case BINARY_EXPR_CMP_EQ: result = emit_cmp_op(emitter, left_type, FIR_ICMPEQ, FIR_ICMPEQ, FIR_FCMPOEQ, FIR_FCMPUEQ, left, right); break;
        case BINARY_EXPR_AND:    result = fir_bit_op(FIR_AND, left, right); break;
        case BINARY_EXPR_XOR:    result = fir_bit_op(FIR_XOR, left, right); break;
        case BINARY_EXPR_OR:     result = fir_bit_op(FIR_OR, left, right); break;
        default:
            assert(false && "invalid binary expression");
            return NULL;
    }

    if (is_assign) {
        assert(binary_expr->binary_expr.left->type->tag == TYPE_REF);
        fir_block_store(&emitter->block, 0, ptr, result);
        return fir_unit(emitter->mod);
    }
    return result;
}

static const struct fir_node* emit_while_loop(struct emitter* emitter, struct ast* while_loop) {
    struct fir_block continue_block;
    struct fir_block break_block = fir_block_merge(emitter->block.func);
    struct fir_block body_block  = fir_block_merge(emitter->block.func);
    fir_block_loop(&emitter->block, &continue_block, &break_block);

    emitter->block = continue_block;
    emit_cond(emitter, while_loop->while_loop.cond, &body_block, &break_block, &break_block);

    emitter->block = body_block;
    emit(emitter, while_loop->while_loop.body);
    fir_block_jump(&emitter->block, &continue_block);

    emitter->block = break_block;
    return fir_unit(emitter->mod);
}

static const struct fir_node* emit(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_LITERAL:
            return ast->node = emit_literal(emitter, &ast->literal, ast->type);
        case AST_FUNC_DECL:
            return ast->node = emit_func_decl(emitter, ast);
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            return ast->node = emit_const_or_var_decl(emitter, ast);
        case AST_IDENT_EXPR:
            return ast->node = ast->ident_expr.bound_to->node;
        case AST_TUPLE_EXPR:
            return ast->node = emit_tuple_expr(emitter, ast);
        case AST_RECORD_EXPR:
            return ast->node = emit_record_expr(emitter, ast);
        case AST_BLOCK_EXPR:
            return ast->node = emit_block_expr(emitter, ast);
        case AST_IF_EXPR:
            return ast->node = emit_if_expr(emitter, ast);
        case AST_CALL_EXPR:
            return ast->node = emit_call_expr(emitter, ast);
        case AST_FIELD_EXPR:
            return ast->node = emit(emitter, ast->field_expr.arg);
        case AST_CAST_EXPR:
            return ast->node = emit_cast_expr(emitter, ast);
        case AST_UNARY_EXPR:
            return ast->node = emit_unary_expr(emitter, ast);
        case AST_BINARY_EXPR:
            return ast->node = emit_binary_expr(emitter, ast);
        case AST_WHILE_LOOP:
            return ast->node = emit_while_loop(emitter, ast);
        default:
            assert(false && "invalid AST node");
            return NULL;
    }
}

static void emit_head(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUNC_DECL:
            ast->node = fir_func(convert_type(emitter, ast->type));
            break;
        default:
            break;
    }
}

void ast_emit(struct ast* ast, struct fir_mod* mod) {
    assert(ast->tag == AST_PROGRAM);
    struct emitter emitter = {
        .mod = mod
    };
    for (struct ast* decl = ast->program.decls; decl; decl = decl->next)
        emit_head(&emitter, decl);
    for (struct ast* decl = ast->program.decls; decl; decl = decl->next)
        emit(&emitter, decl);
}
