#include "sema/fold.h"

#include <math.h>

#include "base/strbuf.h"

typedef struct Folder {
    TypeTable *tt;
    Arena *arena;
    Diags *diags;
    const Source *src;
} Folder;

static bool fail(Folder *f, Span span, const char *msg) {
    diag_error(f->diags, f->src, span, "%s", msg);
    return false;
}

static ConstValue make_int(Folder *f, int64_t v) {
    ConstValue c = {0};
    c.type = f->tt->t_int;
    c.as.i = v;
    return c;
}

static ConstValue make_float(Folder *f, double v) {
    ConstValue c = {0};
    c.type = f->tt->t_float;
    c.as.f = v;
    return c;
}

static ConstValue make_bool(Folder *f, bool v) {
    ConstValue c = {0};
    c.type = f->tt->t_bool;
    c.as.b = v;
    return c;
}

static ConstValue make_str(Folder *f, Str s) {
    ConstValue c = {0};
    c.type = f->tt->t_str;
    c.as.s = s;
    return c;
}

static double as_float(const ConstValue *v) {
    return v->type->kind == TY_FLOAT ? v->as.f : (double)v->as.i;
}

static bool fold(Folder *f, const Expr *e, ConstValue *out);

/* Same formatting as the runtime's rv_str_from_float. */
static Str float_to_str(Folder *f, double v) {
    char buf[64];
    if (isnan(v)) {
        return str_from("nan");
    }
    if (isinf(v)) {
        return str_from(v < 0 ? "-inf" : "inf");
    }
    for (int prec = 15; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (strtod(buf, NULL) == v) {
            break;
        }
    }
    if (strpbrk(buf, ".e") == NULL) {
        strcat(buf, ".0");
    }
    return str_from(arena_strdup(f->arena, buf));
}

static bool fold_str_of(Folder *f, const ConstValue *v, Str *out) {
    switch (v->type->kind) {
    case TY_STR:
        *out = v->as.s;
        return true;
    case TY_INT:
        *out = str_from(arena_printf(f->arena, "%lld", (long long)v->as.i));
        return true;
    case TY_FLOAT:
        *out = float_to_str(f, v->as.f);
        return true;
    case TY_BOOL:
        *out = str_from(v->as.b ? "true" : "false");
        return true;
    default:
        return false;
    }
}

static bool fold_binary(Folder *f, const Expr *e, ConstValue *out) {
    ConstValue l;
    ConstValue r;
    TokKind op = e->as.binary.op;

    if (!fold(f, e->as.binary.lhs, &l)) {
        return false;
    }
    if (op == TOK_ANDAND || op == TOK_OROR) {
        if (op == TOK_ANDAND ? !l.as.b : l.as.b) {
            *out = l;
            return true;
        }
        return fold(f, e->as.binary.rhs, out);
    }
    if (!fold(f, e->as.binary.rhs, &r)) {
        return false;
    }

    if (l.type->kind == TY_STR && r.type->kind == TY_STR) {
        switch (op) {
        case TOK_PLUS: {
            char *bytes = arena_alloc(f->arena, l.as.s.n + r.as.s.n + 1);
            memcpy(bytes, l.as.s.p, l.as.s.n);
            memcpy(bytes + l.as.s.n, r.as.s.p, r.as.s.n);
            *out = make_str(f, str_slice(bytes, l.as.s.n + r.as.s.n));
            return true;
        }
        case TOK_EQ:
            *out = make_bool(f, str_eq(l.as.s, r.as.s));
            return true;
        case TOK_NE:
            *out = make_bool(f, !str_eq(l.as.s, r.as.s));
            return true;
        case TOK_LT:
            *out = make_bool(f, str_cmp(l.as.s, r.as.s) < 0);
            return true;
        case TOK_LE:
            *out = make_bool(f, str_cmp(l.as.s, r.as.s) <= 0);
            return true;
        case TOK_GT:
            *out = make_bool(f, str_cmp(l.as.s, r.as.s) > 0);
            return true;
        case TOK_GE:
            *out = make_bool(f, str_cmp(l.as.s, r.as.s) >= 0);
            return true;
        default:
            break;
        }
    }
    if (l.type->kind == TY_BOOL && r.type->kind == TY_BOOL) {
        if (op == TOK_EQ) {
            *out = make_bool(f, l.as.b == r.as.b);
            return true;
        }
        if (op == TOK_NE) {
            *out = make_bool(f, l.as.b != r.as.b);
            return true;
        }
    }
    if (l.type->kind == TY_INT && r.type->kind == TY_INT) {
        int64_t a = l.as.i;
        int64_t b = r.as.i;
        int64_t v;
        switch (op) {
        case TOK_PLUS:
            if (__builtin_add_overflow(a, b, &v)) {
                return fail(f, e->span, "integer overflow in constant expression");
            }
            *out = make_int(f, v);
            return true;
        case TOK_MINUS:
            if (__builtin_sub_overflow(a, b, &v)) {
                return fail(f, e->span, "integer overflow in constant expression");
            }
            *out = make_int(f, v);
            return true;
        case TOK_STAR:
            if (__builtin_mul_overflow(a, b, &v)) {
                return fail(f, e->span, "integer overflow in constant expression");
            }
            *out = make_int(f, v);
            return true;
        case TOK_SLASH:
        case TOK_PERCENT: {
            int64_t q;
            int64_t rem;
            if (b == 0) {
                return fail(f, e->span, "division by zero in constant expression");
            }
            if (a == INT64_MIN && b == -1) {
                return fail(f, e->span, "integer overflow in constant expression");
            }
            q = a / b;
            rem = a % b;
            if (rem != 0 && ((rem < 0) != (b < 0))) {
                q -= 1;
                rem += b;
            }
            *out = make_int(f, op == TOK_SLASH ? q : rem);
            return true;
        }
        case TOK_AMP:
            *out = make_int(f, a & b);
            return true;
        case TOK_PIPE:
            *out = make_int(f, a | b);
            return true;
        case TOK_CARET:
            *out = make_int(f, a ^ b);
            return true;
        case TOK_SHL:
        case TOK_SHR:
            if (b < 0 || b >= 64) {
                return fail(f, e->span, "shift amount out of range in constant expression");
            }
            *out = make_int(f, op == TOK_SHL ? (int64_t)((uint64_t)a << b) : a >> b);
            return true;
        case TOK_EQ:
            *out = make_bool(f, a == b);
            return true;
        case TOK_NE:
            *out = make_bool(f, a != b);
            return true;
        case TOK_LT:
            *out = make_bool(f, a < b);
            return true;
        case TOK_LE:
            *out = make_bool(f, a <= b);
            return true;
        case TOK_GT:
            *out = make_bool(f, a > b);
            return true;
        case TOK_GE:
            *out = make_bool(f, a >= b);
            return true;
        default:
            break;
        }
    }
    if (ty_is_numeric(l.type) && ty_is_numeric(r.type)) {
        double a = as_float(&l);
        double b = as_float(&r);
        switch (op) {
        case TOK_PLUS:
            *out = make_float(f, a + b);
            return true;
        case TOK_MINUS:
            *out = make_float(f, a - b);
            return true;
        case TOK_STAR:
            *out = make_float(f, a * b);
            return true;
        case TOK_SLASH:
            *out = make_float(f, a / b);
            return true;
        case TOK_PERCENT:
            *out = make_float(f, fmod(a, b));
            return true;
        case TOK_EQ:
            *out = make_bool(f, a == b);
            return true;
        case TOK_NE:
            *out = make_bool(f, a != b);
            return true;
        case TOK_LT:
            *out = make_bool(f, a < b);
            return true;
        case TOK_LE:
            *out = make_bool(f, a <= b);
            return true;
        case TOK_GT:
            *out = make_bool(f, a > b);
            return true;
        case TOK_GE:
            *out = make_bool(f, a >= b);
            return true;
        default:
            break;
        }
    }
    return fail(f, e->span, "this operation is not allowed in a constant expression");
}

static bool fold(Folder *f, const Expr *e, ConstValue *out) {
    switch (e->kind) {
    case EXPR_INT:
        *out = make_int(f, e->as.int_val);
        return true;
    case EXPR_FLOAT:
        *out = make_float(f, e->as.float_val);
        return true;
    case EXPR_BOOL:
        *out = make_bool(f, e->as.bool_val);
        return true;
    case EXPR_STRING:
        *out = make_str(f, e->as.str_val);
        return true;
    case EXPR_NULL: {
        ConstValue c = {0};
        c.type = e->type;
        c.is_null = true;
        *out = c;
        return true;
    }
    case EXPR_NAME: {
        Symbol *sym = e->as.name.sym;
        if (sym == NULL || sym->kind != SYM_GLOBAL) {
            return fail(f, e->span, "only literals and other globals can appear in a global initializer");
        }
        if (!sym->folded) {
            return fail(f, e->span, "globals can only refer to globals declared before them");
        }
        *out = sym->value;
        return true;
    }
    case EXPR_UNARY: {
        ConstValue v;
        if (!fold(f, e->as.unary.operand, &v)) {
            return false;
        }
        switch (e->as.unary.op) {
        case TOK_MINUS:
            if (v.type->kind == TY_INT) {
                if (v.as.i == INT64_MIN) {
                    return fail(f, e->span, "integer overflow in constant expression");
                }
                *out = make_int(f, -v.as.i);
            } else {
                *out = make_float(f, -v.as.f);
            }
            return true;
        case TOK_BANG:
            *out = make_bool(f, !v.as.b);
            return true;
        case TOK_TILDE:
            *out = make_int(f, ~v.as.i);
            return true;
        default:
            break;
        }
        break;
    }
    case EXPR_BINARY:
        return fold_binary(f, e, out);
    case EXPR_FSTRING: {
        StrBuf sb;
        sb_init(&sb);
        for (size_t i = 0; i < e->as.fstring.parts.len; i++) {
            ConstValue part;
            Str text;
            if (!fold(f, e->as.fstring.parts.data[i], &part) || !fold_str_of(f, &part, &text)) {
                sb_free(&sb);
                return false;
            }
            sb_putstr(&sb, text);
        }
        *out = make_str(f, str_from(arena_strndup(f->arena, sb_cstr(&sb), sb.len)));
        sb_free(&sb);
        return true;
    }
    case EXPR_CALL: {
        ConstValue v;
        Str text;
        if (e->as.call.call_kind == CALL_BUILTIN && e->as.call.builtin == BI_STR && e->as.call.args.len == 1) {
            if (!fold(f, e->as.call.args.data[0].value, &v) || !fold_str_of(f, &v, &text)) {
                return false;
            }
            *out = make_str(f, text);
            return true;
        }
        if (e->as.call.call_kind == CALL_BUILTIN && e->as.call.builtin == BI_LEN && e->as.call.args.len == 1) {
            if (!fold(f, e->as.call.args.data[0].value, &v)) {
                return false;
            }
            if (v.type->kind != TY_STR) {
                break;
            }
            *out = make_int(f, (int64_t)v.as.s.n);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return fail(f, e->span, "global initializers must be constant expressions");
}

bool fold_const(const Expr *e, TypeTable *tt, Arena *arena, Diags *diags, const Source *src, ConstValue *out) {
    Folder f = {tt, arena, diags, src};
    return fold(&f, e, out);
}
