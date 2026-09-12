#include "lower/lower.h"

#include <stdarg.h>

#include "../../runtime/rivel_rt.h"

/*
 * Conventions shared with the runtime (see runtime/rivel_rt.h):
 *   - int and pointers are L, bool is W (0/1), float is D
 *   - every struct field, list element, and box payload is one 8-byte cell:
 *     bools widen to L when stored and narrow to W when loaded
 *   - optionals are pointers: null is 0, a T? of a value type points to an RvBox
 */

enum { HEADER_SIZE = 16, CELL_SIZE = 8 };

typedef struct Loop {
    IrBlock *continue_target;
    IrBlock *break_target;
} Loop;

typedef Vec(Loop) LoopVec;

typedef struct Lowerer {
    Program *prog;
    TypeTable *tt;
    IrModule *m;
    const Source *src;
    Arena *arena;
    IrFunc *f;
    IrBlock *b;
    Symbol *fn;
    LoopVec loops;
} Lowerer;

/* ---- helpers --------------------------------------------------------------- */

static IrType cls(const Type *t) {
    switch (t->kind) {
    case TY_BOOL:
        return IR_W;
    case TY_FLOAT:
        return IR_D;
    default:
        return IR_L;
    }
}

static IrType cell_cls(const Type *t) {
    return t->kind == TY_FLOAT ? IR_D : IR_L;
}

static int field_kind(const Type *t) {
    switch (t->kind) {
    case TY_INT:
        return RV_FK_INT;
    case TY_FLOAT:
        return RV_FK_FLOAT;
    case TY_BOOL:
        return RV_FK_BOOL;
    default:
        return RV_FK_PTR;
    }
}

static const char *loc_of(Lowerer *L, Span span) {
    int line;
    int col;
    source_position(L->src, span.lo, &line, &col);
    return ir_loc(L->m, line, col);
}

static IrBlock *new_block(Lowerer *L) {
    return ir_block_new(L->f);
}

static void use_block(Lowerer *L, IrBlock *b) {
    L->b = b;
}

static bool terminated(Lowerer *L) {
    return ir_block_terminated(L->b);
}

/* Calls a runtime or user function. `ret` is -1 for void. */
static IrValue call(Lowerer *L, const char *name, int ret, int nargs, ...) {
    IrValue args[8];
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) {
        args[i] = va_arg(ap, IrValue);
    }
    va_end(ap);
    return ir_emit_call(L->f, L->b, name, ret >= 0, ret >= 0 ? (IrType)ret : IR_W, args, (size_t)nargs);
}

static IrValue binop(Lowerer *L, IrOp op, IrType t, IrValue a, IrValue b) {
    return ir_emit_binary(L->f, L->b, op, t, a, b);
}

static IrValue cmp(Lowerer *L, IrOp op, IrType t, IrValue a, IrValue b) {
    return ir_emit_cmp(L->f, L->b, op, t, a, b);
}

static IrValue unop(Lowerer *L, IrOp op, IrType t, IrValue a) {
    return ir_emit_unary(L->f, L->b, op, t, a);
}

static IrValue load(Lowerer *L, IrType t, IrValue addr, int32_t offset) {
    return ir_emit_load(L->f, L->b, t, addr, offset);
}

static void store(Lowerer *L, IrType t, IrValue value, IrValue addr, int32_t offset) {
    ir_emit_store(L->b, t, value, addr, offset);
}

static IrValue load_slot(Lowerer *L, IrType t, uint32_t slot) {
    return ir_emit_load_slot(L->f, L->b, t, slot);
}

static void store_slot(Lowerer *L, IrType t, IrValue value, uint32_t slot) {
    ir_emit_store_slot(L->b, t, value, slot);
}

static IrValue null_ptr(void) {
    return ir_int(IR_L, 0);
}

static IrValue w_const(int64_t v) {
    return ir_int(IR_W, v);
}

/* Widens a register value to its 8-byte cell representation. */
static IrValue to_cell(Lowerer *L, IrValue v, const Type *t) {
    if (t->kind == TY_BOOL) {
        return unop(L, IR_EXTUW, IR_L, v);
    }
    return v;
}

/* Narrows a cell value back to its register class. */
static IrValue from_cell(Lowerer *L, IrValue v, const Type *t) {
    if (t->kind == TY_BOOL) {
        return unop(L, IR_COPY, IR_W, v);
    }
    return v;
}

static IrValue load_cell(Lowerer *L, IrValue addr, int32_t offset, const Type *t) {
    return from_cell(L, load(L, cell_cls(t), addr, offset), t);
}

static void store_cell(Lowerer *L, IrValue value, IrValue addr, int32_t offset, const Type *t) {
    store(L, cell_cls(t), to_cell(L, value, t), addr, offset);
}

/* Reads the payload of a box (an optional of a value type) or passes a reference through. */
static IrValue unbox(Lowerer *L, IrValue ptr, const Type *elem) {
    if (elem->kind == TY_INT || elem->kind == TY_FLOAT || elem->kind == TY_BOOL) {
        return load_cell(L, ptr, HEADER_SIZE, elem);
    }
    return ptr;
}

static IrValue box(Lowerer *L, IrValue v, const Type *elem) {
    switch (elem->kind) {
    case TY_INT:
        return call(L, "rv_box_int", IR_L, 1, v);
    case TY_FLOAT:
        return call(L, "rv_box_float", IR_L, 1, v);
    case TY_BOOL:
        return call(L, "rv_box_bool", IR_L, 1, v);
    default:
        return v;
    }
}

/* Applies the implicit conversions sema allowed from `from` to `to`. */
static IrValue convert(Lowerer *L, IrValue v, const Type *from, const Type *to) {
    if (from == to || to->kind == TY_ERROR || from->kind == TY_ERROR) {
        return v;
    }
    if (from->kind == TY_NULL) {
        return null_ptr();
    }
    if (from->kind == TY_INT && to->kind == TY_FLOAT) {
        return unop(L, IR_SLTOF, IR_D, v);
    }
    if (to->kind == TY_OPTIONAL) {
        if (from->kind == TY_OPTIONAL) {
            return v;
        }
        if (from->kind == TY_INT && to->elem->kind == TY_FLOAT) {
            v = unop(L, IR_SLTOF, IR_D, v);
        }
        return box(L, v, to->elem);
    }
    return v;
}

static IrValue to_str(Lowerer *L, IrValue v, const Type *t) {
    switch (t->kind) {
    case TY_INT:
        return call(L, "rv_str_from_int", IR_L, 1, v);
    case TY_FLOAT:
        return call(L, "rv_str_from_float", IR_L, 1, v);
    case TY_BOOL:
        return call(L, "rv_str_from_bool", IR_L, 1, v);
    default:
        return v;
    }
}

/* Materializes a value through a fresh slot so it can flow out of several blocks. */
static uint32_t temp_slot(Lowerer *L) {
    return ir_slot(L->f);
}

/* ---- expressions ---------------------------------------------------------------- */

static IrValue lower_expr(Lowerer *L, Expr *e);

static IrValue lower_expr_to(Lowerer *L, Expr *e, const Type *target) {
    IrValue v = lower_expr(L, e);
    return convert(L, v, e->type, target);
}

static IrValue lower_name(Lowerer *L, Expr *e) {
    Symbol *sym = e->as.name.sym;
    IrValue v;

    if (sym->kind == SYM_GLOBAL) {
        return load_cell(L, ir_sym(sym->mangled), 0, sym->type);
    }
    v = load_slot(L, cls(sym->type), (uint32_t)sym->slot);
    if (e->type != sym->type && sym->type->kind == TY_OPTIONAL) {
        return unbox(L, v, sym->type->elem);
    }
    return v;
}

static IrValue lower_fstring(Lowerer *L, Expr *e) {
    ExprVec *parts = &e->as.fstring.parts;
    IrValue acc;

    if (parts->len == 0) {
        return call(L, "rv_str_empty", IR_L, 0);
    }
    acc = to_str(L, lower_expr(L, parts->data[0]), parts->data[0]->type);
    for (size_t i = 1; i < parts->len; i++) {
        IrValue part = to_str(L, lower_expr(L, parts->data[i]), parts->data[i]->type);
        acc = call(L, "rv_str_concat", IR_L, 2, acc, part);
    }
    return acc;
}

static const char *list_fn(Lowerer *L, const Type *elem, const char *base) {
    return arena_printf(L->arena, "%s_%s", base, elem->kind == TY_FLOAT ? "d" : "l");
}

static IrValue lower_list_literal(Lowerer *L, Expr *e) {
    const Type *elem = e->type->elem;
    IrValue list =
        call(L, "rv_list_new", IR_L, 2, ir_int(IR_L, field_kind(elem)), ir_int(IR_L, (int64_t)e->as.list.items.len));

    for (size_t i = 0; i < e->as.list.items.len; i++) {
        Expr *item = e->as.list.items.data[i];
        IrValue v = to_cell(L, lower_expr_to(L, item, elem), elem);
        call(L, elem->kind == TY_FLOAT ? "rv_list_append_d" : "rv_list_append_l", -1, 2, list, v);
    }
    return list;
}

static IrValue lower_unary(Lowerer *L, Expr *e) {
    Expr *operand = e->as.unary.operand;
    IrValue v = lower_expr(L, operand);

    switch (e->as.unary.op) {
    case TOK_MINUS:
        if (operand->type->kind == TY_FLOAT) {
            return unop(L, IR_NEG, IR_D, v);
        }
        return call(L, "rv_neg", IR_L, 2, v, ir_sym(loc_of(L, e->span)));
    case TOK_BANG:
        return binop(L, IR_XOR, IR_W, v, w_const(1));
    case TOK_TILDE:
        return binop(L, IR_XOR, IR_L, v, ir_int(IR_L, -1));
    default:
        return v;
    }
}

static IrOp cmp_op(TokKind op) {
    switch (op) {
    case TOK_EQ:
        return IR_CEQ;
    case TOK_NE:
        return IR_CNE;
    case TOK_LT:
        return IR_CLT;
    case TOK_LE:
        return IR_CLE;
    case TOK_GT:
        return IR_CGT;
    default:
        return IR_CGE;
    }
}

/* Lowers `a op b` for already-evaluated operands. */
static IrValue lower_binop(Lowerer *L, TokKind op, IrValue a, const Type *at, IrValue b, const Type *bt,
                           const Type *result, Span span) {
    IrValue loc = ir_sym(loc_of(L, span));

    if (op == TOK_EQ || op == TOK_NE || op == TOK_LT || op == TOK_LE || op == TOK_GT || op == TOK_GE) {
        bool is_eq = op == TOK_EQ || op == TOK_NE;
        if (ty_is_numeric(at) && ty_is_numeric(bt)) {
            if (at->kind == TY_INT && bt->kind == TY_INT) {
                return cmp(L, cmp_op(op), IR_L, a, b);
            }
            a = convert(L, a, at, L->tt->t_float);
            b = convert(L, b, bt, L->tt->t_float);
            return cmp(L, cmp_op(op), IR_D, a, b);
        }
        if (at->kind == TY_BOOL && bt->kind == TY_BOOL) {
            return cmp(L, cmp_op(op), IR_W, a, b);
        }
        if (at->kind == TY_STR && bt->kind == TY_STR) {
            IrValue r;
            if (is_eq) {
                r = call(L, "rv_str_eq", IR_W, 2, a, b);
                return op == TOK_EQ ? r : binop(L, IR_XOR, IR_W, r, w_const(1));
            }
            r = call(L, "rv_str_cmp", IR_L, 2, a, b);
            return cmp(L, cmp_op(op), IR_L, r, ir_int(IR_L, 0));
        }
        /* references and optionals: null checks compare pointers, otherwise deep equality */
        if (at->kind == TY_NULL || bt->kind == TY_NULL) {
            IrValue p = at->kind == TY_NULL ? b : a;
            return cmp(L, cmp_op(op), IR_L, p, null_ptr());
        }
        {
            const Type *common = at->kind == TY_OPTIONAL ? at : bt->kind == TY_OPTIONAL ? bt : at;
            IrValue r;
            a = convert(L, a, at, common);
            b = convert(L, b, bt, common);
            r = call(L, "rv_eq", IR_W, 2, a, b);
            return op == TOK_EQ ? r : binop(L, IR_XOR, IR_W, r, w_const(1));
        }
    }
    if (at->kind == TY_STR) {
        return call(L, "rv_str_concat", IR_L, 2, a, b);
    }
    if (result->kind == TY_FLOAT) {
        a = convert(L, a, at, L->tt->t_float);
        b = convert(L, b, bt, L->tt->t_float);
        switch (op) {
        case TOK_PLUS:
            return binop(L, IR_ADD, IR_D, a, b);
        case TOK_MINUS:
            return binop(L, IR_SUB, IR_D, a, b);
        case TOK_STAR:
            return binop(L, IR_MUL, IR_D, a, b);
        case TOK_SLASH:
            return binop(L, IR_DIV, IR_D, a, b);
        case TOK_PERCENT:
            return call(L, "fmod", IR_D, 2, a, b);
        default:
            return a;
        }
    }
    switch (op) {
    case TOK_PLUS:
        return call(L, "rv_add", IR_L, 3, a, b, loc);
    case TOK_MINUS:
        return call(L, "rv_sub", IR_L, 3, a, b, loc);
    case TOK_STAR:
        return call(L, "rv_mul", IR_L, 3, a, b, loc);
    case TOK_SLASH:
        return call(L, "rv_div", IR_L, 3, a, b, loc);
    case TOK_PERCENT:
        return call(L, "rv_mod", IR_L, 3, a, b, loc);
    case TOK_SHL:
        return call(L, "rv_shl", IR_L, 3, a, b, loc);
    case TOK_SHR:
        return call(L, "rv_shr", IR_L, 3, a, b, loc);
    case TOK_AMP:
        return binop(L, IR_AND, IR_L, a, b);
    case TOK_PIPE:
        return binop(L, IR_OR, IR_L, a, b);
    case TOK_CARET:
        return binop(L, IR_XOR, IR_L, a, b);
    default:
        return a;
    }
}

static IrValue lower_short_circuit(Lowerer *L, Expr *e) {
    bool is_and = e->as.binary.op == TOK_ANDAND;
    uint32_t slot = temp_slot(L);
    IrBlock *rhs_block = new_block(L);
    IrBlock *join = new_block(L);
    IrValue l = lower_expr(L, e->as.binary.lhs);
    IrValue r;

    store_slot(L, IR_W, l, slot);
    if (is_and) {
        ir_emit_br(L->b, l, rhs_block, join);
    } else {
        ir_emit_br(L->b, l, join, rhs_block);
    }
    use_block(L, rhs_block);
    r = lower_expr(L, e->as.binary.rhs);
    store_slot(L, IR_W, r, slot);
    ir_emit_jmp(L->b, join);
    use_block(L, join);
    return load_slot(L, IR_W, slot);
}

static IrValue lower_coalesce(Lowerer *L, Expr *e) {
    Expr *lhs = e->as.binary.lhs;
    Expr *rhs = e->as.binary.rhs;
    const Type *elem = e->type;
    IrType t = cls(elem);
    uint32_t slot = temp_slot(L);
    IrBlock *some = new_block(L);
    IrBlock *none = new_block(L);
    IrBlock *join = new_block(L);
    IrValue p = lower_expr(L, lhs);
    IrValue is_null = cmp(L, IR_CEQ, IR_L, p, null_ptr());

    ir_emit_br(L->b, is_null, none, some);
    use_block(L, some);
    store_slot(L, t, unbox(L, p, elem), slot);
    ir_emit_jmp(L->b, join);
    use_block(L, none);
    store_slot(L, t, lower_expr_to(L, rhs, elem), slot);
    ir_emit_jmp(L->b, join);
    use_block(L, join);
    return load_slot(L, t, slot);
}

static IrValue lower_binary(Lowerer *L, Expr *e) {
    TokKind op = e->as.binary.op;
    Expr *lhs = e->as.binary.lhs;
    Expr *rhs = e->as.binary.rhs;
    IrValue a;
    IrValue b;

    if (op == TOK_ANDAND || op == TOK_OROR) {
        return lower_short_circuit(L, e);
    }
    if (op == TOK_QQ) {
        return lower_coalesce(L, e);
    }
    a = lower_expr(L, lhs);
    b = lower_expr(L, rhs);
    return lower_binop(L, op, a, lhs->type, b, rhs->type, e->type, e->span);
}

static IrValue lower_field(Lowerer *L, Expr *e) {
    IrValue base = lower_expr(L, e->as.field.base);
    return load_cell(L, base, HEADER_SIZE + CELL_SIZE * e->as.field.field_index, e->type);
}

static IrValue lower_index(Lowerer *L, Expr *e) {
    IrValue base = lower_expr(L, e->as.index.base);
    IrValue idx = lower_expr_to(L, e->as.index.index, L->tt->t_int);
    IrValue loc = ir_sym(loc_of(L, e->span));

    if (e->as.index.base->type->kind == TY_STR) {
        return call(L, "rv_str_index", IR_L, 3, base, idx, loc);
    }
    return from_cell(L, call(L, list_fn(L, e->type, "rv_list_get"), cell_cls(e->type), 3, base, idx, loc), e->type);
}

static IrValue lower_slice(Lowerer *L, Expr *e) {
    IrValue base = lower_expr(L, e->as.slice.base);
    IrValue lo = lower_expr_to(L, e->as.slice.lo, L->tt->t_int);
    IrValue hi = lower_expr_to(L, e->as.slice.hi, L->tt->t_int);
    IrValue loc = ir_sym(loc_of(L, e->span));

    if (e->as.slice.base->type->kind == TY_STR) {
        return call(L, "rv_str_slice", IR_L, 4, base, lo, hi, loc);
    }
    return call(L, "rv_list_slice", IR_L, 4, base, lo, hi, loc);
}

/* min/max as a branch: the result flows through a slot. */
static IrValue lower_min_max(Lowerer *L, Expr *call_expr, bool is_min) {
    Expr *x = call_expr->as.call.args.data[0].value;
    Expr *y = call_expr->as.call.args.data[1].value;
    const Type *t = call_expr->type;
    IrType c = cls(t);
    uint32_t slot = temp_slot(L);
    IrBlock *pick_a = new_block(L);
    IrBlock *pick_b = new_block(L);
    IrBlock *join = new_block(L);
    IrValue a = lower_expr_to(L, x, t);
    IrValue b = lower_expr_to(L, y, t);
    IrValue a_wins = cmp(L, is_min ? IR_CLE : IR_CGE, c, a, b);

    ir_emit_br(L->b, a_wins, pick_a, pick_b);
    use_block(L, pick_a);
    store_slot(L, c, a, slot);
    ir_emit_jmp(L->b, join);
    use_block(L, pick_b);
    store_slot(L, c, b, slot);
    ir_emit_jmp(L->b, join);
    use_block(L, join);
    return load_slot(L, c, slot);
}

static IrValue lower_builtin(Lowerer *L, Expr *e) {
    ArgVec *args = &e->as.call.args;
    IrValue loc = ir_sym(loc_of(L, e->span));
    Expr *a0 = args->len > 0 ? args->data[0].value : NULL;
    Expr *a1 = args->len > 1 ? args->data[1].value : NULL;

    switch ((Builtin)e->as.call.builtin) {
    case BI_PRINT:
        call(L, "rv_print", -1, 1, to_str(L, lower_expr(L, a0), a0->type));
        return ir_none();
    case BI_PRINTLN:
        call(L, "rv_println", -1, 1, to_str(L, lower_expr(L, a0), a0->type));
        return ir_none();
    case BI_EPRINTLN:
        call(L, "rv_eprintln", -1, 1, to_str(L, lower_expr(L, a0), a0->type));
        return ir_none();
    case BI_LEN:
        return load(L, IR_L, lower_expr(L, a0), HEADER_SIZE);
    case BI_STR:
        return to_str(L, lower_expr(L, a0), a0->type);
    case BI_INT:
        if (a0->type->kind == TY_STR) {
            return call(L, "rv_str_parse_int", IR_L, 1, lower_expr(L, a0));
        }
        if (a0->type->kind == TY_FLOAT) {
            return call(L, "rv_float_to_int", IR_L, 2, lower_expr(L, a0), loc);
        }
        return lower_expr(L, a0);
    case BI_FLOAT:
        if (a0->type->kind == TY_STR) {
            return call(L, "rv_str_parse_float", IR_L, 1, lower_expr(L, a0));
        }
        return lower_expr_to(L, a0, L->tt->t_float);
    case BI_ORD:
        return call(L, "rv_ord", IR_L, 2, lower_expr(L, a0), loc);
    case BI_CHR:
        return call(L, "rv_chr", IR_L, 2, lower_expr_to(L, a0, L->tt->t_int), loc);
    case BI_PANIC:
        call(L, "rv_panic", -1, 2, lower_expr(L, a0), loc);
        ir_emit_hlt(L->b);
        use_block(L, new_block(L));
        return ir_none();
    case BI_ASSERT:
        call(L, "rv_assert", -1, 2, lower_expr(L, a0), loc);
        return ir_none();
    case BI_EXIT:
        call(L, "rv_exit", -1, 1, lower_expr_to(L, a0, L->tt->t_int));
        ir_emit_hlt(L->b);
        use_block(L, new_block(L));
        return ir_none();
    case BI_ARGS:
        return call(L, "rv_args", IR_L, 0);
    case BI_READ_FILE:
        return call(L, "rv_read_file", IR_L, 1, lower_expr(L, a0));
    case BI_WRITE_FILE:
        return call(L, "rv_write_file", IR_W, 2, lower_expr(L, a0), lower_expr(L, a1));
    case BI_READ_LINE:
        return call(L, "rv_read_line", IR_L, 0);
    case BI_SQRT:
        return call(L, "rv_sqrt", IR_D, 1, lower_expr_to(L, a0, L->tt->t_float));
    case BI_ABS:
        if (a0->type->kind == TY_FLOAT) {
            return call(L, "fabs", IR_D, 1, lower_expr(L, a0));
        }
        return call(L, "rv_abs", IR_L, 2, lower_expr(L, a0), loc);
    case BI_MIN:
        return lower_min_max(L, e, true);
    case BI_MAX:
        return lower_min_max(L, e, false);
    case BI_WRAPPING_ADD:
        return binop(L, IR_ADD, IR_L, lower_expr(L, a0), lower_expr(L, a1));
    case BI_WRAPPING_SUB:
        return binop(L, IR_SUB, IR_L, lower_expr(L, a0), lower_expr(L, a1));
    case BI_WRAPPING_MUL:
        return binop(L, IR_MUL, IR_L, lower_expr(L, a0), lower_expr(L, a1));
    default:
        return ir_none();
    }
}

static IrValue lower_str_method(Lowerer *L, Expr *e) {
    ArgVec *args = &e->as.call.args;
    IrValue self = lower_expr(L, e->as.call.callee->as.field.base);
    IrValue loc = ir_sym(loc_of(L, e->span));
    IrValue a0 = args->len > 0 ? lower_expr(L, args->data[0].value) : ir_none();
    IrValue a1 = args->len > 1 ? lower_expr(L, args->data[1].value) : ir_none();

    switch ((Builtin)e->as.call.builtin) {
    case BI_STR_CONTAINS:
        return call(L, "rv_str_contains", IR_W, 2, self, a0);
    case BI_STR_STARTS_WITH:
        return call(L, "rv_str_starts_with", IR_W, 2, self, a0);
    case BI_STR_ENDS_WITH:
        return call(L, "rv_str_ends_with", IR_W, 2, self, a0);
    case BI_STR_FIND:
        return call(L, "rv_str_find", IR_L, 2, self, a0);
    case BI_STR_SPLIT:
        return call(L, "rv_str_split", IR_L, 2, self, a0);
    case BI_STR_JOIN:
        return call(L, "rv_str_join", IR_L, 2, self, a0);
    case BI_STR_TRIM:
        return call(L, "rv_str_trim", IR_L, 1, self);
    case BI_STR_UPPER:
        return call(L, "rv_str_upper", IR_L, 1, self);
    case BI_STR_LOWER:
        return call(L, "rv_str_lower", IR_L, 1, self);
    case BI_STR_REPLACE:
        return call(L, "rv_str_replace", IR_L, 3, self, a0, a1);
    case BI_STR_REPEAT:
        return call(L, "rv_str_repeat", IR_L, 3, self, a0, loc);
    default:
        return ir_none();
    }
}

static IrValue lower_list_method(Lowerer *L, Expr *e) {
    ArgVec *args = &e->as.call.args;
    Expr *base = e->as.call.callee->as.field.base;
    const Type *elem = base->type->elem;
    IrType ec = cell_cls(elem);
    IrValue self = lower_expr(L, base);
    IrValue loc = ir_sym(loc_of(L, e->span));

    switch ((Builtin)e->as.call.builtin) {
    case BI_LIST_APPEND: {
        IrValue v = to_cell(L, lower_expr_to(L, args->data[0].value, elem), elem);
        call(L, list_fn(L, elem, "rv_list_append"), -1, 2, self, v);
        return ir_none();
    }
    case BI_LIST_POP:
        return from_cell(L, call(L, list_fn(L, elem, "rv_list_pop"), ec, 2, self, loc), elem);
    case BI_LIST_INSERT: {
        IrValue i = lower_expr_to(L, args->data[0].value, L->tt->t_int);
        IrValue v = to_cell(L, lower_expr_to(L, args->data[1].value, elem), elem);
        call(L, list_fn(L, elem, "rv_list_insert"), -1, 4, self, i, v, loc);
        return ir_none();
    }
    case BI_LIST_REMOVE_AT: {
        IrValue i = lower_expr_to(L, args->data[0].value, L->tt->t_int);
        return from_cell(L, call(L, list_fn(L, elem, "rv_list_remove_at"), ec, 3, self, i, loc), elem);
    }
    case BI_LIST_CLEAR:
        call(L, "rv_list_clear", -1, 1, self);
        return ir_none();
    case BI_LIST_CONTAINS: {
        IrValue v = to_cell(L, lower_expr_to(L, args->data[0].value, elem), elem);
        return call(L, list_fn(L, elem, "rv_list_contains"), IR_W, 2, self, v);
    }
    case BI_LIST_INDEX_OF: {
        IrValue v = to_cell(L, lower_expr_to(L, args->data[0].value, elem), elem);
        return call(L, list_fn(L, elem, "rv_list_index_of"), IR_L, 2, self, v);
    }
    default:
        return ir_none();
    }
}

static IrValue lower_call(Lowerer *L, Expr *e) {
    ArgVec *args = &e->as.call.args;
    Symbol *callee = e->as.call.callee_sym;
    IrValue argv[64];
    size_t n = 0;

    switch (e->as.call.call_kind) {
    case CALL_FUNC:
        for (size_t i = 0; i < args->len && n < ARRAY_LEN(argv); i++) {
            argv[n++] = lower_expr_to(L, args->data[i].value, callee->params.data[i]);
        }
        return ir_emit_call(L->f, L->b, callee->mangled, callee->type->kind != TY_VOID, cls(callee->type), argv, n);
    case CALL_METHOD:
        argv[n++] = lower_expr(L, e->as.call.callee->as.field.base);
        for (size_t i = 0; i < args->len && n < ARRAY_LEN(argv); i++) {
            argv[n++] = lower_expr_to(L, args->data[i].value, callee->params.data[i + 1]);
        }
        return ir_emit_call(L->f, L->b, callee->mangled, callee->type->kind != TY_VOID, cls(callee->type), argv, n);
    case CALL_CONSTRUCT: {
        IrValue obj = call(L, "rv_alloc", IR_L, 1, ir_sym(callee->mangled));
        for (size_t i = 0; i < args->len; i++) {
            Arg *a = &args->data[i];
            const Type *ft = callee->fields.data[a->field_index].type;
            IrValue v = lower_expr_to(L, a->value, ft);
            store_cell(L, v, obj, HEADER_SIZE + CELL_SIZE * a->field_index, ft);
        }
        return obj;
    }
    case CALL_BUILTIN:
        return lower_builtin(L, e);
    case CALL_STR_METHOD:
        return lower_str_method(L, e);
    case CALL_LIST_METHOD:
        return lower_list_method(L, e);
    default:
        return ir_none();
    }
}

static IrValue lower_expr(Lowerer *L, Expr *e) {
    switch (e->kind) {
    case EXPR_INT:
        return ir_int(IR_L, e->as.int_val);
    case EXPR_FLOAT:
        return ir_float(e->as.float_val);
    case EXPR_BOOL:
        return w_const(e->as.bool_val ? 1 : 0);
    case EXPR_STRING:
        return ir_sym(ir_string_literal(L->m, e->as.str_val));
    case EXPR_NULL:
        return null_ptr();
    case EXPR_NAME:
        return lower_name(L, e);
    case EXPR_FSTRING:
        return lower_fstring(L, e);
    case EXPR_LIST:
        return lower_list_literal(L, e);
    case EXPR_UNARY:
        return lower_unary(L, e);
    case EXPR_BINARY:
        return lower_binary(L, e);
    case EXPR_CALL:
        return lower_call(L, e);
    case EXPR_FIELD:
        return lower_field(L, e);
    case EXPR_INDEX:
        return lower_index(L, e);
    case EXPR_SLICE:
        return lower_slice(L, e);
    case EXPR_RANGE:
        return ir_none();
    }
    return ir_none();
}

/* ---- statements -------------------------------------------------------------------- */

static void lower_block(Lowerer *L, Block *b);
static void lower_stmt(Lowerer *L, Stmt *s);

static void lower_assign(Lowerer *L, Stmt *s) {
    Expr *target = s->as.assign.target;
    Expr *value = s->as.assign.value;
    TokKind op = s->as.assign.op;
    const Type *tt = target->type;

    if (target->kind == EXPR_NAME) {
        Symbol *sym = target->as.name.sym;
        IrValue v;
        if (op == TOK_ASSIGN) {
            v = lower_expr_to(L, value, sym->type);
        } else {
            IrValue cur = lower_name(L, target);
            IrValue rhs = lower_expr(L, value);
            TokKind bop = op == TOK_PLUS_ASSIGN    ? TOK_PLUS
                          : op == TOK_MINUS_ASSIGN ? TOK_MINUS
                          : op == TOK_STAR_ASSIGN  ? TOK_STAR
                          : op == TOK_SLASH_ASSIGN ? TOK_SLASH
                                                   : TOK_PERCENT;
            const Type *rt = ty_is_numeric(tt) && ty_is_numeric(value->type)
                                 ? (tt->kind == TY_INT && value->type->kind == TY_INT ? L->tt->t_int : L->tt->t_float)
                                 : tt;
            v = convert(L, lower_binop(L, bop, cur, tt, rhs, value->type, rt, s->span), rt, sym->type);
        }
        if (sym->kind == SYM_GLOBAL) {
            store_cell(L, v, ir_sym(sym->mangled), 0, sym->type);
        } else {
            store_slot(L, cls(sym->type), v, (uint32_t)sym->slot);
        }
        return;
    }
    if (target->kind == EXPR_FIELD) {
        IrValue base = lower_expr(L, target->as.field.base);
        int32_t off = HEADER_SIZE + CELL_SIZE * target->as.field.field_index;
        IrValue v;
        if (op == TOK_ASSIGN) {
            v = lower_expr_to(L, value, tt);
        } else {
            IrValue cur = load_cell(L, base, off, tt);
            IrValue rhs = lower_expr(L, value);
            TokKind bop = op == TOK_PLUS_ASSIGN    ? TOK_PLUS
                          : op == TOK_MINUS_ASSIGN ? TOK_MINUS
                          : op == TOK_STAR_ASSIGN  ? TOK_STAR
                          : op == TOK_SLASH_ASSIGN ? TOK_SLASH
                                                   : TOK_PERCENT;
            v = convert(L, lower_binop(L, bop, cur, tt, rhs, value->type, tt, s->span), tt, tt);
        }
        store_cell(L, v, base, off, tt);
        return;
    }
    if (target->kind == EXPR_INDEX) {
        Expr *base_expr = target->as.index.base;
        const Type *elem = base_expr->type->elem;
        IrValue base = lower_expr(L, base_expr);
        IrValue idx = lower_expr_to(L, target->as.index.index, L->tt->t_int);
        IrValue loc = ir_sym(loc_of(L, s->span));
        IrValue v;
        if (op == TOK_ASSIGN) {
            v = lower_expr_to(L, value, elem);
        } else {
            IrValue cur =
                from_cell(L, call(L, list_fn(L, elem, "rv_list_get"), cell_cls(elem), 3, base, idx, loc), elem);
            IrValue rhs = lower_expr(L, value);
            TokKind bop = op == TOK_PLUS_ASSIGN    ? TOK_PLUS
                          : op == TOK_MINUS_ASSIGN ? TOK_MINUS
                          : op == TOK_STAR_ASSIGN  ? TOK_STAR
                          : op == TOK_SLASH_ASSIGN ? TOK_SLASH
                                                   : TOK_PERCENT;
            v = lower_binop(L, bop, cur, elem, rhs, value->type, elem, s->span);
        }
        call(L, list_fn(L, elem, "rv_list_set"), -1, 4, base, idx, to_cell(L, v, elem), loc);
    }
}

static void lower_if(Lowerer *L, Stmt *s) {
    IrBlock *then_block = new_block(L);
    IrBlock *else_block = s->as.if_stmt.else_stmt != NULL ? new_block(L) : NULL;
    IrBlock *join = new_block(L);
    IrValue cond = lower_expr(L, s->as.if_stmt.cond);

    ir_emit_br(L->b, cond, then_block, else_block != NULL ? else_block : join);
    use_block(L, then_block);
    lower_block(L, s->as.if_stmt.then_block);
    if (!terminated(L)) {
        ir_emit_jmp(L->b, join);
    }
    if (else_block != NULL) {
        use_block(L, else_block);
        lower_stmt(L, s->as.if_stmt.else_stmt);
        if (!terminated(L)) {
            ir_emit_jmp(L->b, join);
        }
    }
    use_block(L, join);
}

static void push_loop(Lowerer *L, IrBlock *continue_target, IrBlock *break_target) {
    Loop loop = {continue_target, break_target};
    vec_push(&L->loops, loop);
}

static void pop_loop(Lowerer *L) {
    L->loops.len--;
}

static void lower_while(Lowerer *L, Stmt *s) {
    IrBlock *header = new_block(L);
    IrBlock *body = new_block(L);
    IrBlock *exit = new_block(L);
    IrValue cond;

    ir_emit_jmp(L->b, header);
    use_block(L, header);
    cond = lower_expr(L, s->as.while_stmt.cond);
    ir_emit_br(L->b, cond, body, exit);
    use_block(L, body);
    push_loop(L, header, exit);
    lower_block(L, s->as.while_stmt.body);
    pop_loop(L);
    if (!terminated(L)) {
        ir_emit_jmp(L->b, header);
    }
    use_block(L, exit);
}

static void lower_for_range(Lowerer *L, Stmt *s) {
    Expr *range = s->as.for_stmt.iter;
    uint32_t var = (uint32_t)s->as.for_stmt.sym->slot;
    uint32_t end = temp_slot(L);
    uint32_t cur = temp_slot(L);
    IrBlock *header = new_block(L);
    IrBlock *body = new_block(L);
    IrBlock *step = new_block(L);
    IrBlock *exit = new_block(L);
    IrValue i;
    IrValue e;

    store_slot(L, IR_L, lower_expr_to(L, range->as.range.lo, L->tt->t_int), cur);
    store_slot(L, IR_L, lower_expr_to(L, range->as.range.hi, L->tt->t_int), end);
    ir_emit_jmp(L->b, header);

    use_block(L, header);
    i = load_slot(L, IR_L, cur);
    e = load_slot(L, IR_L, end);
    ir_emit_br(L->b, cmp(L, range->as.range.inclusive ? IR_CLE : IR_CLT, IR_L, i, e), body, exit);

    use_block(L, body);
    store_slot(L, IR_L, load_slot(L, IR_L, cur), var);
    push_loop(L, step, exit);
    lower_block(L, s->as.for_stmt.body);
    pop_loop(L);
    if (!terminated(L)) {
        ir_emit_jmp(L->b, step);
    }

    /* the inclusive form stops before incrementing past the end, so i..=INT64_MAX terminates */
    use_block(L, step);
    i = load_slot(L, IR_L, cur);
    if (range->as.range.inclusive) {
        IrBlock *incr = new_block(L);
        e = load_slot(L, IR_L, end);
        ir_emit_br(L->b, cmp(L, IR_CEQ, IR_L, i, e), exit, incr);
        use_block(L, incr);
        i = load_slot(L, IR_L, cur);
    }
    store_slot(L, IR_L, binop(L, IR_ADD, IR_L, i, ir_int(IR_L, 1)), cur);
    ir_emit_jmp(L->b, header);

    use_block(L, exit);
}

static void lower_for_each(Lowerer *L, Stmt *s) {
    Expr *iter = s->as.for_stmt.iter;
    bool is_str = iter->type->kind == TY_STR;
    const Type *elem = is_str ? L->tt->t_str : iter->type->elem;
    uint32_t var = (uint32_t)s->as.for_stmt.sym->slot;
    uint32_t seq = temp_slot(L);
    uint32_t len = temp_slot(L);
    uint32_t idx = temp_slot(L);
    IrBlock *header = new_block(L);
    IrBlock *body = new_block(L);
    IrBlock *step = new_block(L);
    IrBlock *exit = new_block(L);
    IrValue seqv;
    IrValue i;
    IrValue item;

    seqv = lower_expr(L, iter);
    store_slot(L, IR_L, seqv, seq);
    store_slot(L, IR_L, load(L, IR_L, seqv, HEADER_SIZE), len);
    store_slot(L, IR_L, ir_int(IR_L, 0), idx);
    ir_emit_jmp(L->b, header);

    use_block(L, header);
    ir_emit_br(L->b, cmp(L, IR_CLT, IR_L, load_slot(L, IR_L, idx), load_slot(L, IR_L, len)), body, exit);

    use_block(L, body);
    seqv = load_slot(L, IR_L, seq);
    i = load_slot(L, IR_L, idx);
    if (is_str) {
        item = call(L, "rv_str_index", IR_L, 3, seqv, i, ir_sym(loc_of(L, iter->span)));
    } else {
        item = from_cell(
            L, call(L, list_fn(L, elem, "rv_list_get"), cell_cls(elem), 3, seqv, i, ir_sym(loc_of(L, iter->span))),
            elem);
    }
    store_slot(L, cls(elem), item, var);
    push_loop(L, step, exit);
    lower_block(L, s->as.for_stmt.body);
    pop_loop(L);
    if (!terminated(L)) {
        ir_emit_jmp(L->b, step);
    }

    use_block(L, step);
    store_slot(L, IR_L, binop(L, IR_ADD, IR_L, load_slot(L, IR_L, idx), ir_int(IR_L, 1)), idx);
    ir_emit_jmp(L->b, header);

    use_block(L, exit);
}

static void lower_return(Lowerer *L, Stmt *s) {
    if (s->as.ret.value != NULL) {
        ir_emit_ret(L->b, lower_expr_to(L, s->as.ret.value, L->fn->type));
    } else {
        ir_emit_ret_void(L->b);
    }
    use_block(L, new_block(L));
}

static void lower_stmt(Lowerer *L, Stmt *s) {
    switch (s->kind) {
    case STMT_VAR: {
        Symbol *sym = s->as.var.sym;
        store_slot(L, cls(sym->type), lower_expr_to(L, s->as.var.init, sym->type), (uint32_t)sym->slot);
        return;
    }
    case STMT_ASSIGN:
        lower_assign(L, s);
        return;
    case STMT_EXPR:
        lower_expr(L, s->as.expr.expr);
        return;
    case STMT_IF:
        lower_if(L, s);
        return;
    case STMT_WHILE:
        lower_while(L, s);
        return;
    case STMT_FOR:
        if (s->as.for_stmt.iter->kind == EXPR_RANGE) {
            lower_for_range(L, s);
        } else {
            lower_for_each(L, s);
        }
        return;
    case STMT_BREAK:
        ir_emit_jmp(L->b, vec_last(&L->loops).break_target);
        use_block(L, new_block(L));
        return;
    case STMT_CONTINUE:
        ir_emit_jmp(L->b, vec_last(&L->loops).continue_target);
        use_block(L, new_block(L));
        return;
    case STMT_RETURN:
        lower_return(L, s);
        return;
    case STMT_BLOCK:
        lower_block(L, s->as.block);
        return;
    }
}

static void lower_block(Lowerer *L, Block *b) {
    for (size_t i = 0; i < b->stmts.len; i++) {
        lower_stmt(L, b->stmts.data[i]);
    }
}

/* ---- declarations ------------------------------------------------------------------ */

static void lower_function(Lowerer *L, Symbol *fn) {
    IrFunc *f = ir_func_new(L->m, fn->mangled);
    IrBlock *entry;
    IrBlock *body;

    L->f = f;
    L->fn = fn;
    for (size_t i = 0; i < fn->params.len; i++) {
        vec_push(&f->params, cls(fn->params.data[i]));
    }
    f->has_ret = fn->type->kind != TY_VOID;
    f->ret = f->has_ret ? cls(fn->type) : IR_W;
    f->nslots = (uint32_t)fn->nlocals;

    /* the entry block only spills parameters (done by the backend); the body starts in its own block
       so that a loop at the top of the function never targets the entry block */
    entry = new_block(L);
    body = new_block(L);
    ir_emit_jmp(entry, body);
    use_block(L, body);
    lower_block(L, fn->func->body);
    for (size_t i = 0; i < f->blocks.len; i++) {
        IrBlock *b = f->blocks.data[i];
        if (!ir_block_terminated(b)) {
            if (f->has_ret) {
                ir_emit_hlt(b);
            } else {
                ir_emit_ret_void(b);
            }
        }
    }
}

static void lower_globals(Lowerer *L) {
    for (size_t i = 0; i < L->prog->globals.len; i++) {
        Symbol *sym = L->prog->globals.data[i];
        IrGlobal g;
        memset(&g, 0, sizeof g);
        g.sym = sym->mangled;
        switch (sym->type->kind) {
        case TY_INT:
            g.kind = IRG_INT;
            g.init_int = sym->value.as.i;
            break;
        case TY_BOOL:
            g.kind = IRG_INT;
            g.init_int = sym->value.as.b ? 1 : 0;
            break;
        case TY_FLOAT:
            g.kind = IRG_FLOAT;
            g.init_float = sym->value.as.f;
            break;
        case TY_STR:
            g.kind = IRG_PTR;
            g.init_sym = ir_string_literal(L->m, sym->value.as.s);
            break;
        default:
            g.kind = IRG_PTR;
            break;
        }
        vec_push(&L->m->globals, g);
    }
}

static void lower_typeinfos(Lowerer *L) {
    for (size_t i = 0; i < L->prog->structs.len; i++) {
        Symbol *st = L->prog->structs.data[i];
        IrTypeInfo ti;
        memset(&ti, 0, sizeof ti);
        ti.sym = st->mangled;
        ti.name = st->name;
        for (size_t j = 0; j < st->fields.len; j++) {
            vec_push(&ti.field_kinds, (uint8_t)field_kind(st->fields.data[j].type));
        }
        vec_push(&L->m->typeinfos, ti);
    }
}

void lower_program(Program *prog, Arena *arena, IrModule *out) {
    Lowerer L;

    memset(&L, 0, sizeof L);
    L.prog = prog;
    L.tt = &prog->types;
    L.m = out;
    L.src = prog->module->src;
    L.arena = arena;

    lower_typeinfos(&L);
    lower_globals(&L);
    for (size_t i = 0; i < prog->funcs.len; i++) {
        lower_function(&L, prog->funcs.data[i]);
    }
    if (prog->main_fn != NULL) {
        out->main_sym = prog->main_fn->mangled;
        out->main_returns_int = prog->main_fn->type->kind == TY_INT;
    }
    vec_free(&L.loops);
}
