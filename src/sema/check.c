#include <stdarg.h>

#include "sema/fold.h"
#include "sema/sema.h"

/*
 * Name resolution and type checking in one pass over each function body.
 *
 * Scopes are a chain of string maps. Optional narrowing is an overlay: a
 * stack of (symbol, type) facts consulted before the symbol's declared type.
 * Entering an `if` body pushes the facts its condition implies and leaving
 * pops them; assigning to a narrowed variable pushes its declared type back.
 */

typedef struct Scope {
    StrMap names;
    struct Scope *parent;
    SymVec locals; /* for unused-variable warnings, in declaration order */
} Scope;

typedef struct Narrow {
    Symbol *sym;
    Type *type;
} Narrow;

typedef Vec(Narrow) NarrowVec;

typedef struct Checker {
    Module *m;
    Arena *arena;
    Diags *diags;
    const Source *src;
    Program *prog;
    TypeTable *tt;
    Scope *global;
    Scope *scope;
    Symbol *cur_func;
    int loop_depth;
    NarrowVec narrows;
} Checker;

/* ---- diagnostics ---------------------------------------------------------- */

static void error(Checker *C, Span span, const char *fmt, ...) PRINTF_LIKE(3, 4);
static void error(Checker *C, Span span, const char *fmt, ...) {
    va_list args;
    char *msg;
    va_start(args, fmt);
    msg = arena_vprintf(C->arena, fmt, args);
    va_end(args);
    diag_error(C->diags, C->src, span, "%s", msg);
}

static void warning(Checker *C, Span span, const char *fmt, ...) PRINTF_LIKE(3, 4);
static void warning(Checker *C, Span span, const char *fmt, ...) {
    va_list args;
    char *msg;
    va_start(args, fmt);
    msg = arena_vprintf(C->arena, fmt, args);
    va_end(args);
    diag_warning(C->diags, C->src, span, "%s", msg);
}

static void note(Checker *C, Span span, const char *fmt, ...) PRINTF_LIKE(3, 4);
static void note(Checker *C, Span span, const char *fmt, ...) {
    va_list args;
    char *msg;
    va_start(args, fmt);
    msg = arena_vprintf(C->arena, fmt, args);
    va_end(args);
    diag_note(C->diags, C->src, span, "%s", msg);
}

/* ---- scopes and symbols ---------------------------------------------------- */

static Scope *scope_new(Checker *C, Scope *parent) {
    Scope *s = arena_new(C->arena, Scope);
    strmap_init(&s->names);
    s->parent = parent;
    return s;
}

static void scope_release(Scope *s) {
    strmap_free(&s->names);
    vec_free(&s->locals);
}

static void scope_push(Checker *C) {
    C->scope = scope_new(C, C->scope);
}

static void scope_pop(Checker *C) {
    Scope *s = C->scope;
    for (size_t i = 0; i < s->locals.len; i++) {
        Symbol *sym = s->locals.data[i];
        if (sym->kind == SYM_LOCAL && !sym->used && sym->name.n > 0 && sym->name.p[0] != '_') {
            warning(C, sym->span, "`" STR_FMT "` is never used", STR_ARG(sym->name));
        }
    }
    C->scope = s->parent;
    scope_release(s);
}

static Symbol *lookup(const Checker *C, Str name) {
    for (Scope *s = C->scope; s != NULL; s = s->parent) {
        Symbol *sym = strmap_get(&s->names, name);
        if (sym != NULL) {
            return sym;
        }
    }
    return NULL;
}

static Symbol *new_sym(Checker *C, SymKind kind, Str name, Span span) {
    Symbol *sym = arena_new(C->arena, Symbol);
    sym->kind = kind;
    sym->name = name;
    sym->span = span;
    return sym;
}

static const char *sym_kind_desc(const Symbol *sym) {
    switch (sym->kind) {
    case SYM_TYPE:
        return "a type";
    case SYM_BUILTIN:
        return "a builtin function";
    case SYM_FUNC:
        return "a function";
    case SYM_METHOD:
        return "a method";
    case SYM_STRUCT:
        return "a struct";
    case SYM_GLOBAL:
        return "a global";
    case SYM_PARAM:
        return "a parameter";
    case SYM_LOCAL:
        return "a variable";
    }
    return "a name";
}

/* Declares in the current scope, rejecting any name that is already visible. */
static bool declare(Checker *C, Symbol *sym) {
    Symbol *existing = lookup(C, sym->name);
    if (existing != NULL) {
        if (existing->kind == SYM_TYPE || existing->kind == SYM_BUILTIN) {
            error(C, sym->span, "`" STR_FMT "` is %s and cannot be redefined", STR_ARG(sym->name),
                  sym_kind_desc(existing));
        } else {
            error(C, sym->span, "`" STR_FMT "` is already defined as %s", STR_ARG(sym->name), sym_kind_desc(existing));
            note(C, existing->span, "previous definition is here");
        }
        return false;
    }
    strmap_put(&C->scope->names, sym->name, sym);
    vec_push(&C->scope->locals, sym);
    return true;
}

static Symbol *declare_local(Checker *C, SymKind kind, Str name, Span span, Type *type, bool immutable) {
    Symbol *sym = new_sym(C, kind, name, span);
    sym->type = type;
    sym->immutable = immutable;
    sym->slot = C->cur_func->nlocals++;
    declare(C, sym);
    return sym;
}

/* ---- narrowing ------------------------------------------------------------ */

static Type *effective_type(const Checker *C, const Symbol *sym) {
    for (size_t i = C->narrows.len; i > 0; i--) {
        if (C->narrows.data[i - 1].sym == sym) {
            return C->narrows.data[i - 1].type;
        }
    }
    return sym->type;
}

static void narrow_push(Checker *C, Symbol *sym, Type *type) {
    Narrow n = {sym, type};
    vec_push(&C->narrows, n);
}

static Symbol *narrowable_var(const Checker *C, const Expr *e) {
    Symbol *sym;
    if (e->kind != EXPR_NAME) {
        return NULL;
    }
    sym = e->as.name.sym;
    if (sym == NULL || (sym->kind != SYM_LOCAL && sym->kind != SYM_PARAM)) {
        return NULL;
    }
    return effective_type(C, sym)->kind == TY_OPTIONAL ? sym : NULL;
}

/* Pushes the facts `cond` establishes when it evaluates to `when_true`. */
static void collect_facts(Checker *C, const Expr *cond, bool when_true) {
    if (cond->kind == EXPR_UNARY && cond->as.unary.op == TOK_BANG) {
        collect_facts(C, cond->as.unary.operand, !when_true);
        return;
    }
    if (cond->kind != EXPR_BINARY) {
        return;
    }
    switch (cond->as.binary.op) {
    case TOK_ANDAND:
        if (when_true) {
            collect_facts(C, cond->as.binary.lhs, true);
            collect_facts(C, cond->as.binary.rhs, true);
        }
        return;
    case TOK_OROR:
        if (!when_true) {
            collect_facts(C, cond->as.binary.lhs, false);
            collect_facts(C, cond->as.binary.rhs, false);
        }
        return;
    case TOK_EQ:
    case TOK_NE: {
        const Expr *lhs = cond->as.binary.lhs;
        const Expr *rhs = cond->as.binary.rhs;
        const Expr *var = NULL;
        Symbol *sym;
        bool is_ne = cond->as.binary.op == TOK_NE;
        if (rhs->kind == EXPR_NULL) {
            var = lhs;
        } else if (lhs->kind == EXPR_NULL) {
            var = rhs;
        }
        if (var == NULL) {
            return;
        }
        sym = narrowable_var(C, var);
        if (sym != NULL && (is_ne == when_true)) {
            narrow_push(C, sym, effective_type(C, sym)->elem);
        }
        return;
    }
    default:
        return;
    }
}

/* ---- type resolution ------------------------------------------------------- */

static Type *resolve_type(Checker *C, const TypeExpr *t, bool allow_void) {
    if (t == NULL) {
        return C->tt->t_error;
    }
    switch (t->kind) {
    case TYPEX_NAME: {
        Symbol *sym = lookup(C, t->name);
        if (sym == NULL) {
            error(C, t->span, "unknown type `" STR_FMT "`", STR_ARG(t->name));
            return C->tt->t_error;
        }
        if (sym->kind == SYM_TYPE) {
            if (sym->type->kind == TY_VOID && !allow_void) {
                error(C, t->span, "`void` is only valid as a return type");
                return C->tt->t_error;
            }
            if (sym->type == NULL) {
                error(C, t->span, "`list` needs an element type, like `list[int]`");
                return C->tt->t_error;
            }
            return sym->type;
        }
        if (sym->kind == SYM_STRUCT) {
            return sym->type;
        }
        error(C, t->span, "`" STR_FMT "` is %s, not a type", STR_ARG(t->name), sym_kind_desc(sym));
        return C->tt->t_error;
    }
    case TYPEX_LIST:
        return ty_list(C->tt, resolve_type(C, t->inner, false));
    case TYPEX_OPTIONAL: {
        Type *inner = resolve_type(C, t->inner, false);
        if (inner->kind == TY_OPTIONAL) {
            error(C, t->span, "`%s?` is already optional", inner->name);
        }
        return ty_optional(C->tt, inner);
    }
    }
    return C->tt->t_error;
}

/* ---- expressions --------------------------------------------------------- */

static Type *check_expr(Checker *C, Expr *e, Type *expected);

static Type *set_type(Expr *e, Type *t) {
    e->type = t;
    return t;
}

static bool is_error(const Type *t) {
    return t->kind == TY_ERROR;
}

static const char *op_name(TokKind op) {
    switch (op) {
    case TOK_PLUS:
    case TOK_PLUS_ASSIGN:
        return "+";
    case TOK_MINUS:
    case TOK_MINUS_ASSIGN:
        return "-";
    case TOK_STAR:
    case TOK_STAR_ASSIGN:
        return "*";
    case TOK_SLASH:
    case TOK_SLASH_ASSIGN:
        return "/";
    case TOK_PERCENT:
    case TOK_PERCENT_ASSIGN:
        return "%";
    case TOK_AMP:
        return "&";
    case TOK_PIPE:
        return "|";
    case TOK_CARET:
        return "^";
    case TOK_SHL:
        return "<<";
    case TOK_SHR:
        return ">>";
    case TOK_EQ:
        return "==";
    case TOK_NE:
        return "!=";
    case TOK_LT:
        return "<";
    case TOK_LE:
        return "<=";
    case TOK_GT:
        return ">";
    case TOK_GE:
        return ">=";
    case TOK_ANDAND:
        return "&&";
    case TOK_OROR:
        return "||";
    case TOK_BANG:
        return "!";
    case TOK_TILDE:
        return "~";
    case TOK_QQ:
        return "??";
    default:
        return "?";
    }
}

static TokKind compound_base_op(TokKind op) {
    switch (op) {
    case TOK_PLUS_ASSIGN:
        return TOK_PLUS;
    case TOK_MINUS_ASSIGN:
        return TOK_MINUS;
    case TOK_STAR_ASSIGN:
        return TOK_STAR;
    case TOK_SLASH_ASSIGN:
        return TOK_SLASH;
    case TOK_PERCENT_ASSIGN:
        return TOK_PERCENT;
    default:
        return op;
    }
}

/* Requires `actual` to convert to `wanted`; reports with `what` describing the slot. */
static bool require_convertible(Checker *C, Span span, Type *actual, Type *wanted, const char *what) {
    if (ty_convertible(actual, wanted)) {
        return true;
    }
    if (actual->kind == TY_VOID) {
        error(C, span, "%s expects `%s`, but this call returns nothing", what, wanted->name);
    } else if (actual->kind == TY_OPTIONAL && actual->elem == wanted) {
        error(C, span, "%s expects `%s`, but this value is `%s` and may be null; check `!= null` first", what,
              wanted->name, actual->name);
    } else {
        error(C, span, "%s expects `%s`, found `%s`", what, wanted->name, actual->name);
    }
    return false;
}

static Type *check_name(Checker *C, Expr *e) {
    Symbol *sym = lookup(C, e->as.name.name);
    if (sym == NULL) {
        error(C, e->span, "unknown name `" STR_FMT "`", STR_ARG(e->as.name.name));
        return set_type(e, C->tt->t_error);
    }
    e->as.name.sym = sym;
    switch (sym->kind) {
    case SYM_LOCAL:
    case SYM_PARAM:
        sym->used = true;
        return set_type(e, effective_type(C, sym));
    case SYM_GLOBAL:
        if (sym->type == NULL) {
            error(C, e->span, "`" STR_FMT "` is used before its declaration; globals can only refer to earlier globals",
                  STR_ARG(e->as.name.name));
            return set_type(e, C->tt->t_error);
        }
        return set_type(e, sym->type);
    default:
        error(C, e->span, "`" STR_FMT "` is %s, not a value", STR_ARG(e->as.name.name), sym_kind_desc(sym));
        return set_type(e, C->tt->t_error);
    }
}

static Type *binary_result(Checker *C, Expr *e, TokKind op, Type *l, Type *r) {
    Span span = e->span;

    if (is_error(l) || is_error(r)) {
        return C->tt->t_error;
    }
    switch (op) {
    case TOK_PLUS:
        if (l->kind == TY_STR && r->kind == TY_STR) {
            return C->tt->t_str;
        }
        /* fallthrough */
    case TOK_MINUS:
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:
        if (ty_is_numeric(l) && ty_is_numeric(r)) {
            return l->kind == TY_INT && r->kind == TY_INT ? C->tt->t_int : C->tt->t_float;
        }
        break;
    case TOK_AMP:
    case TOK_PIPE:
    case TOK_CARET:
    case TOK_SHL:
    case TOK_SHR:
        if (l->kind == TY_INT && r->kind == TY_INT) {
            return C->tt->t_int;
        }
        break;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        if ((ty_is_numeric(l) && ty_is_numeric(r)) || (l->kind == TY_STR && r->kind == TY_STR)) {
            return C->tt->t_bool;
        }
        break;
    case TOK_EQ:
    case TOK_NE:
        if (l == r && l->kind != TY_VOID) {
            return C->tt->t_bool;
        }
        if (ty_is_numeric(l) && ty_is_numeric(r)) {
            return C->tt->t_bool;
        }
        if (l->kind == TY_NULL && (r->kind == TY_OPTIONAL || r->kind == TY_NULL)) {
            return C->tt->t_bool;
        }
        if (r->kind == TY_NULL && l->kind == TY_OPTIONAL) {
            return C->tt->t_bool;
        }
        if (l->kind == TY_OPTIONAL && ty_convertible(r, l)) {
            return C->tt->t_bool;
        }
        if (r->kind == TY_OPTIONAL && ty_convertible(l, r)) {
            return C->tt->t_bool;
        }
        if (l->kind == TY_NULL || r->kind == TY_NULL) {
            error(C, span, "`%s` is never null, so comparing it with `null` makes no sense",
                  l->kind == TY_NULL ? r->name : l->name);
            return C->tt->t_error;
        }
        break;
    case TOK_ANDAND:
    case TOK_OROR:
        if (l->kind == TY_BOOL && r->kind == TY_BOOL) {
            return C->tt->t_bool;
        }
        break;
    default:
        break;
    }
    if (l == r) {
        error(C, span, "operator `%s` is not defined for `%s`", op_name(op), l->name);
    } else {
        error(C, span, "operator `%s` is not defined for `%s` and `%s`", op_name(op), l->name, r->name);
    }
    return C->tt->t_error;
}

static Type *check_binary(Checker *C, Expr *e, Type *expected) {
    TokKind op = e->as.binary.op;
    Expr *lhs = e->as.binary.lhs;
    Expr *rhs = e->as.binary.rhs;
    Type *l;
    Type *r;
    size_t base = C->narrows.len;

    if (op == TOK_QQ) {
        l = check_expr(C, lhs, NULL);
        if (is_error(l)) {
            check_expr(C, rhs, NULL);
            return set_type(e, l);
        }
        if (l->kind != TY_OPTIONAL) {
            error(C, lhs->span, "left side of `??` must be optional, found `%s`", l->name);
            check_expr(C, rhs, NULL);
            return set_type(e, C->tt->t_error);
        }
        r = check_expr(C, rhs, l->elem);
        require_convertible(C, rhs->span, r, l->elem, "right side of `??`");
        return set_type(e, l->elem);
    }
    if (op == TOK_ANDAND || op == TOK_OROR) {
        l = check_expr(C, lhs, C->tt->t_bool);
        collect_facts(C, lhs, op == TOK_ANDAND);
        r = check_expr(C, rhs, C->tt->t_bool);
        C->narrows.len = base;
        return set_type(e, binary_result(C, e, op, l, r));
    }
    (void)expected;
    l = check_expr(C, lhs, NULL);
    r = check_expr(C, rhs, l->kind == TY_NULL ? NULL : l);
    if (l->kind == TY_NULL && r->kind == TY_OPTIONAL) {
        set_type(lhs, r);
        l = r;
    }
    if (r->kind == TY_NULL && l->kind == TY_OPTIONAL) {
        set_type(rhs, l);
        r = l;
    }
    return set_type(e, binary_result(C, e, op, l, r));
}

static Type *check_unary(Checker *C, Expr *e) {
    Type *t = check_expr(C, e->as.unary.operand, NULL);
    TokKind op = e->as.unary.op;

    if (is_error(t)) {
        return set_type(e, t);
    }
    if (op == TOK_MINUS && ty_is_numeric(t)) {
        return set_type(e, t);
    }
    if (op == TOK_BANG && t->kind == TY_BOOL) {
        return set_type(e, t);
    }
    if (op == TOK_TILDE && t->kind == TY_INT) {
        return set_type(e, t);
    }
    error(C, e->span, "operator `%s` is not defined for `%s`", op_name(op), t->name);
    return set_type(e, C->tt->t_error);
}

static Type *check_list_literal(Checker *C, Expr *e, Type *expected) {
    ExprVec *items = &e->as.list.items;
    Type *elem = expected != NULL && expected->kind == TY_LIST ? expected->elem : NULL;

    if (items->len == 0) {
        if (elem == NULL) {
            error(C, e->span,
                  "cannot infer the element type of an empty list; write a type, like `xs: list[int] = [];`");
            return set_type(e, C->tt->t_error);
        }
        return set_type(e, expected);
    }
    for (size_t i = 0; i < items->len; i++) {
        Expr *item = items->data[i];
        Type *t = check_expr(C, item, elem);
        if (elem == NULL) {
            if (t->kind == TY_NULL) {
                error(C, item->span,
                      "cannot infer a list type from `null`; write a type, like `xs: list[int?] = [null];`");
                return set_type(e, C->tt->t_error);
            }
            if (t->kind == TY_VOID) {
                error(C, item->span, "this call returns nothing, so it cannot be a list element");
                return set_type(e, C->tt->t_error);
            }
            elem = t;
        } else if (!ty_convertible(t, elem)) {
            if (ty_convertible(elem, t) && expected == NULL) {
                elem = t;
            } else {
                error(C, item->span, "list element has type `%s`, but earlier elements are `%s`", t->name, elem->name);
                return set_type(e, C->tt->t_error);
            }
        }
    }
    if (is_error(elem)) {
        return set_type(e, C->tt->t_error);
    }
    return set_type(e, ty_list(C->tt, elem));
}

static Type *check_field(Checker *C, Expr *e) {
    Type *base = check_expr(C, e->as.field.base, NULL);
    Str name = e->as.field.name;

    if (is_error(base)) {
        return set_type(e, base);
    }
    if (base->kind == TY_STRUCT) {
        FieldInfo *field = strmap_get(&base->st->members, name);
        if (field != NULL) {
            e->as.field.field_index = field->index;
            return set_type(e, field->type);
        }
        if (strmap_get(&base->st->methods, name) != NULL) {
            error(C, e->as.field.name_span, "`" STR_FMT "` is a method of `%s`; call it with `()`", STR_ARG(name),
                  base->name);
        } else {
            error(C, e->as.field.name_span, "`%s` has no field `" STR_FMT "`", base->name, STR_ARG(name));
        }
        return set_type(e, C->tt->t_error);
    }
    if (base->kind == TY_OPTIONAL) {
        error(C, e->as.field.base->span, "value of type `%s` may be null; check `!= null` before using it", base->name);
    } else if (base->kind == TY_STR || base->kind == TY_LIST) {
        error(C, e->as.field.name_span, "`%s` has no field `" STR_FMT "`; use `len(x)` for the length", base->name,
              STR_ARG(name));
    } else {
        error(C, e->as.field.name_span, "`%s` has no fields", base->name);
    }
    return set_type(e, C->tt->t_error);
}

static Type *check_index(Checker *C, Expr *e) {
    Type *base = check_expr(C, e->as.index.base, NULL);
    Type *idx = check_expr(C, e->as.index.index, C->tt->t_int);

    require_convertible(C, e->as.index.index->span, idx, C->tt->t_int, "an index");
    if (is_error(base)) {
        return set_type(e, base);
    }
    if (base->kind == TY_LIST) {
        return set_type(e, base->elem);
    }
    if (base->kind == TY_STR) {
        return set_type(e, C->tt->t_str);
    }
    if (base->kind == TY_OPTIONAL) {
        error(C, e->as.index.base->span, "value of type `%s` may be null; check `!= null` before indexing it",
              base->name);
    } else {
        error(C, e->span, "cannot index a value of type `%s`", base->name);
    }
    return set_type(e, C->tt->t_error);
}

static Type *check_slice(Checker *C, Expr *e) {
    Type *base = check_expr(C, e->as.slice.base, NULL);
    Type *lo = check_expr(C, e->as.slice.lo, C->tt->t_int);
    Type *hi = check_expr(C, e->as.slice.hi, C->tt->t_int);

    require_convertible(C, e->as.slice.lo->span, lo, C->tt->t_int, "a slice bound");
    require_convertible(C, e->as.slice.hi->span, hi, C->tt->t_int, "a slice bound");
    if (is_error(base)) {
        return set_type(e, base);
    }
    if (base->kind == TY_LIST || base->kind == TY_STR) {
        return set_type(e, base);
    }
    error(C, e->span, "cannot slice a value of type `%s`", base->name);
    return set_type(e, C->tt->t_error);
}

static Type *check_fstring(Checker *C, Expr *e) {
    for (size_t i = 0; i < e->as.fstring.parts.len; i++) {
        Expr *part = e->as.fstring.parts.data[i];
        Type *t = check_expr(C, part, NULL);
        if (!is_error(t) && !ty_is_printable(t)) {
            if (t->kind == TY_OPTIONAL) {
                error(C, part->span, "cannot format `%s` because it may be null; use `?? ` to give a default", t->name);
            } else {
                error(C, part->span, "cannot format a value of type `%s`; f-strings accept int, float, bool, and str",
                      t->name);
            }
        }
    }
    return set_type(e, C->tt->t_str);
}

/* ---- calls ---------------------------------------------------------------- */

static bool check_arg_count(Checker *C, Expr *call, const char *what, size_t got, size_t want) {
    if (got == want) {
        return true;
    }
    error(C, call->span, "%s takes %zu argument%s, but %zu %s given", what, want, want == 1 ? "" : "s", got,
          got == 1 ? "was" : "were");
    for (size_t i = 0; i < call->as.call.args.len; i++) {
        check_expr(C, call->as.call.args.data[i].value, NULL);
    }
    return false;
}

static bool reject_labels(Checker *C, Expr *call) {
    bool ok = true;
    for (size_t i = 0; i < call->as.call.args.len; i++) {
        Arg *a = &call->as.call.args.data[i];
        if (a->label.n != 0) {
            error(C, a->label_span, "only struct construction uses labeled arguments");
            ok = false;
        }
    }
    return ok;
}

/* Checks positional arguments against a parameter list starting at `first`. */
static void check_positional_args(Checker *C, Expr *call, const char *what, TypeVec *params, size_t first) {
    ArgVec *args = &call->as.call.args;
    size_t want = params->len - first;

    reject_labels(C, call);
    if (!check_arg_count(C, call, what, args->len, want)) {
        return;
    }
    for (size_t i = 0; i < args->len; i++) {
        Type *param = params->data[first + i];
        Type *t = check_expr(C, args->data[i].value, param);
        char *slot = arena_printf(C->arena, "argument %zu of %s", i + 1, what);
        require_convertible(C, args->data[i].value->span, t, param, slot);
    }
}

static Type *check_construct(Checker *C, Expr *call, Symbol *st) {
    ArgVec *args = &call->as.call.args;
    bool *seen = xcalloc(st->fields.len ? st->fields.len : 1, sizeof *seen);

    call->as.call.call_kind = CALL_CONSTRUCT;
    call->as.call.callee_sym = st;
    for (size_t i = 0; i < args->len; i++) {
        Arg *a = &args->data[i];
        FieldInfo *field;
        Type *t;
        char *slot;

        if (a->label.n == 0) {
            error(C, a->value->span, "struct construction needs a field label, like `" STR_FMT "(name: value)`",
                  STR_ARG(st->name));
            check_expr(C, a->value, NULL);
            continue;
        }
        field = strmap_get(&st->members, a->label);
        if (field == NULL) {
            error(C, a->label_span, "`" STR_FMT "` has no field `" STR_FMT "`", STR_ARG(st->name), STR_ARG(a->label));
            check_expr(C, a->value, NULL);
            continue;
        }
        if (seen[field->index]) {
            error(C, a->label_span, "field `" STR_FMT "` is given twice", STR_ARG(a->label));
        }
        seen[field->index] = true;
        a->field_index = field->index;
        t = check_expr(C, a->value, field->type);
        slot = arena_printf(C->arena, "field `" STR_FMT "`", STR_ARG(a->label));
        require_convertible(C, a->value->span, t, field->type, slot);
    }
    for (size_t i = 0; i < st->fields.len; i++) {
        if (!seen[i]) {
            error(C, call->span, "construction of `" STR_FMT "` is missing field `" STR_FMT "`", STR_ARG(st->name),
                  STR_ARG(st->fields.data[i].name));
        }
    }
    free(seen);
    return set_type(call, st->type);
}

static Type *arg_type(Checker *C, Expr *call, size_t i, Type *expected) {
    return check_expr(C, call->as.call.args.data[i].value, expected);
}

static bool arg_require(Checker *C, Expr *call, size_t i, Type *wanted, const char *fname) {
    Type *t = arg_type(C, call, i, wanted);
    char *slot = arena_printf(C->arena, "argument %zu of `%s`", i + 1, fname);
    return require_convertible(C, call->as.call.args.data[i].value->span, t, wanted, slot);
}

static Type *check_builtin_call(Checker *C, Expr *call, Builtin b) {
    const char *name = builtin_name(b);
    char *quoted = arena_printf(C->arena, "`%s`", name);
    size_t n = call->as.call.args.len;
    Type *t;

    call->as.call.call_kind = CALL_BUILTIN;
    call->as.call.builtin = b;
    reject_labels(C, call);

#define ARGS(count)                                                                                                    \
    if (!check_arg_count(C, call, quoted, n, (count))) {                                                               \
        return set_type(call, C->tt->t_error);                                                                         \
    }

    switch (b) {
    case BI_PRINT:
    case BI_PRINTLN:
    case BI_EPRINTLN:
        ARGS(1);
        t = arg_type(C, call, 0, NULL);
        if (!is_error(t) && !ty_is_printable(t)) {
            if (t->kind == TY_OPTIONAL) {
                error(C, call->as.call.args.data[0].value->span,
                      "cannot print `%s` because it may be null; use `?? ` to give a default", t->name);
            } else {
                error(C, call->as.call.args.data[0].value->span,
                      "cannot print a value of type `%s`; `%s` accepts int, float, bool, and str", t->name, name);
            }
        }
        return set_type(call, C->tt->t_void);
    case BI_LEN:
        ARGS(1);
        t = arg_type(C, call, 0, NULL);
        if (!is_error(t) && t->kind != TY_STR && t->kind != TY_LIST) {
            error(C, call->as.call.args.data[0].value->span, "`len` expects a str or list, found `%s`", t->name);
        }
        return set_type(call, C->tt->t_int);
    case BI_STR:
        ARGS(1);
        t = arg_type(C, call, 0, NULL);
        if (!is_error(t) && !ty_is_printable(t)) {
            error(C, call->as.call.args.data[0].value->span, "`str` cannot convert a value of type `%s`", t->name);
        }
        return set_type(call, C->tt->t_str);
    case BI_INT:
        ARGS(1);
        t = arg_type(C, call, 0, NULL);
        if (t->kind == TY_STR) {
            return set_type(call, ty_optional(C->tt, C->tt->t_int));
        }
        if (!is_error(t) && !ty_is_numeric(t)) {
            error(C, call->as.call.args.data[0].value->span, "`int` converts a float or a str, found `%s`", t->name);
        }
        return set_type(call, C->tt->t_int);
    case BI_FLOAT:
        ARGS(1);
        t = arg_type(C, call, 0, NULL);
        if (t->kind == TY_STR) {
            return set_type(call, ty_optional(C->tt, C->tt->t_float));
        }
        if (!is_error(t) && !ty_is_numeric(t)) {
            error(C, call->as.call.args.data[0].value->span, "`float` converts an int or a str, found `%s`", t->name);
        }
        return set_type(call, C->tt->t_float);
    case BI_ORD:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_str, name);
        return set_type(call, C->tt->t_int);
    case BI_CHR:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_int, name);
        return set_type(call, C->tt->t_str);
    case BI_PANIC:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_str, name);
        return set_type(call, C->tt->t_void);
    case BI_ASSERT:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_bool, name);
        return set_type(call, C->tt->t_void);
    case BI_EXIT:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_int, name);
        return set_type(call, C->tt->t_void);
    case BI_ARGS:
        ARGS(0);
        return set_type(call, ty_list(C->tt, C->tt->t_str));
    case BI_READ_FILE:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_str, name);
        return set_type(call, ty_optional(C->tt, C->tt->t_str));
    case BI_WRITE_FILE:
        ARGS(2);
        arg_require(C, call, 0, C->tt->t_str, name);
        arg_require(C, call, 1, C->tt->t_str, name);
        return set_type(call, C->tt->t_bool);
    case BI_READ_LINE:
        ARGS(0);
        return set_type(call, ty_optional(C->tt, C->tt->t_str));
    case BI_SQRT:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_float, name);
        return set_type(call, C->tt->t_float);
    case BI_ABS:
        ARGS(1);
        t = arg_type(C, call, 0, NULL);
        if (!is_error(t) && !ty_is_numeric(t)) {
            error(C, call->as.call.args.data[0].value->span, "`abs` expects an int or float, found `%s`", t->name);
            return set_type(call, C->tt->t_error);
        }
        return set_type(call, t);
    case BI_MIN:
    case BI_MAX: {
        Type *a;
        Type *b2;
        ARGS(2);
        a = arg_type(C, call, 0, NULL);
        b2 = arg_type(C, call, 1, NULL);
        if (is_error(a) || is_error(b2)) {
            return set_type(call, C->tt->t_error);
        }
        if (!ty_is_numeric(a) || !ty_is_numeric(b2)) {
            error(C, call->span, "`%s` expects two numbers, found `%s` and `%s`", name, a->name, b2->name);
            return set_type(call, C->tt->t_error);
        }
        return set_type(call, a->kind == TY_INT && b2->kind == TY_INT ? C->tt->t_int : C->tt->t_float);
    }
    case BI_WRAPPING_ADD:
    case BI_WRAPPING_SUB:
    case BI_WRAPPING_MUL:
        ARGS(2);
        arg_require(C, call, 0, C->tt->t_int, name);
        arg_require(C, call, 1, C->tt->t_int, name);
        return set_type(call, C->tt->t_int);
    default:
        break;
    }
#undef ARGS
    error(C, call->span, "internal error: unhandled builtin `%s`", name);
    return set_type(call, C->tt->t_error);
}

static Type *check_str_method(Checker *C, Expr *call, Builtin b) {
    const char *name = builtin_name(b);
    char *quoted = arena_printf(C->arena, "`str.%s`", name);
    size_t n = call->as.call.args.len;
    Type *list_str = ty_list(C->tt, C->tt->t_str);

    call->as.call.call_kind = CALL_STR_METHOD;
    call->as.call.builtin = b;
    reject_labels(C, call);

#define ARGS(count)                                                                                                    \
    if (!check_arg_count(C, call, quoted, n, (count))) {                                                               \
        return set_type(call, C->tt->t_error);                                                                         \
    }
    switch (b) {
    case BI_STR_CONTAINS:
    case BI_STR_STARTS_WITH:
    case BI_STR_ENDS_WITH:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_str, name);
        return set_type(call, C->tt->t_bool);
    case BI_STR_FIND:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_str, name);
        return set_type(call, C->tt->t_int);
    case BI_STR_SPLIT:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_str, name);
        return set_type(call, list_str);
    case BI_STR_JOIN:
        ARGS(1);
        arg_require(C, call, 0, list_str, name);
        return set_type(call, C->tt->t_str);
    case BI_STR_TRIM:
    case BI_STR_UPPER:
    case BI_STR_LOWER:
        ARGS(0);
        return set_type(call, C->tt->t_str);
    case BI_STR_REPLACE:
        ARGS(2);
        arg_require(C, call, 0, C->tt->t_str, name);
        arg_require(C, call, 1, C->tt->t_str, name);
        return set_type(call, C->tt->t_str);
    case BI_STR_REPEAT:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_int, name);
        return set_type(call, C->tt->t_str);
    default:
        break;
    }
#undef ARGS
    error(C, call->span, "internal error: unhandled str method `%s`", name);
    return set_type(call, C->tt->t_error);
}

static Type *check_list_method(Checker *C, Expr *call, Builtin b, Type *list) {
    const char *name = builtin_name(b);
    char *quoted = arena_printf(C->arena, "`%s.%s`", list->name, name);
    size_t n = call->as.call.args.len;
    Type *elem = list->elem;

    call->as.call.call_kind = CALL_LIST_METHOD;
    call->as.call.builtin = b;
    reject_labels(C, call);

#define ARGS(count)                                                                                                    \
    if (!check_arg_count(C, call, quoted, n, (count))) {                                                               \
        return set_type(call, C->tt->t_error);                                                                         \
    }
    switch (b) {
    case BI_LIST_APPEND:
        ARGS(1);
        arg_require(C, call, 0, elem, name);
        return set_type(call, C->tt->t_void);
    case BI_LIST_POP:
        ARGS(0);
        return set_type(call, elem);
    case BI_LIST_INSERT:
        ARGS(2);
        arg_require(C, call, 0, C->tt->t_int, name);
        arg_require(C, call, 1, elem, name);
        return set_type(call, C->tt->t_void);
    case BI_LIST_REMOVE_AT:
        ARGS(1);
        arg_require(C, call, 0, C->tt->t_int, name);
        return set_type(call, elem);
    case BI_LIST_CLEAR:
        ARGS(0);
        return set_type(call, C->tt->t_void);
    case BI_LIST_CONTAINS:
        ARGS(1);
        arg_require(C, call, 0, elem, name);
        return set_type(call, C->tt->t_bool);
    case BI_LIST_INDEX_OF:
        ARGS(1);
        arg_require(C, call, 0, elem, name);
        return set_type(call, C->tt->t_int);
    default:
        break;
    }
#undef ARGS
    error(C, call->span, "internal error: unhandled list method `%s`", name);
    return set_type(call, C->tt->t_error);
}

static void check_args_loosely(Checker *C, Expr *call) {
    for (size_t i = 0; i < call->as.call.args.len; i++) {
        check_expr(C, call->as.call.args.data[i].value, NULL);
    }
}

static Type *check_call(Checker *C, Expr *call) {
    Expr *callee = call->as.call.callee;

    callee->type = C->tt->t_void;
    if (callee->kind == EXPR_NAME) {
        Str name = callee->as.name.name;
        Symbol *sym = lookup(C, name);
        char *what;

        if (sym == NULL) {
            error(C, callee->span, "unknown function `" STR_FMT "`", STR_ARG(name));
            check_args_loosely(C, call);
            return set_type(call, C->tt->t_error);
        }
        callee->as.name.sym = sym;
        switch (sym->kind) {
        case SYM_FUNC:
            call->as.call.call_kind = CALL_FUNC;
            call->as.call.callee_sym = sym;
            what = arena_printf(C->arena, "`" STR_FMT "`", STR_ARG(name));
            check_positional_args(C, call, what, &sym->params, 0);
            return set_type(call, sym->type);
        case SYM_STRUCT:
            return check_construct(C, call, sym);
        case SYM_BUILTIN:
            return check_builtin_call(C, call, sym->builtin);
        case SYM_TYPE:
            if (sym->type == C->tt->t_int) {
                return check_builtin_call(C, call, BI_INT);
            }
            if (sym->type == C->tt->t_float) {
                return check_builtin_call(C, call, BI_FLOAT);
            }
            if (sym->type == C->tt->t_str) {
                return check_builtin_call(C, call, BI_STR);
            }
            error(C, callee->span, "`" STR_FMT "` is a type, not a function", STR_ARG(name));
            check_args_loosely(C, call);
            return set_type(call, C->tt->t_error);
        default:
            error(C, callee->span, "`" STR_FMT "` is %s, not a function", STR_ARG(name), sym_kind_desc(sym));
            check_args_loosely(C, call);
            return set_type(call, C->tt->t_error);
        }
    }
    if (callee->kind == EXPR_FIELD) {
        Expr *base = callee->as.field.base;
        Str name = callee->as.field.name;
        Type *bt = check_expr(C, base, NULL);
        Builtin b;

        if (is_error(bt)) {
            check_args_loosely(C, call);
            return set_type(call, bt);
        }
        if (bt->kind == TY_STRUCT) {
            Symbol *method = strmap_get(&bt->st->methods, name);
            char *what;
            if (method == NULL) {
                if (strmap_get(&bt->st->members, name) != NULL) {
                    error(C, callee->as.field.name_span, "`" STR_FMT "` is a field of `%s`, not a method",
                          STR_ARG(name), bt->name);
                } else {
                    error(C, callee->as.field.name_span, "`%s` has no method `" STR_FMT "`", bt->name, STR_ARG(name));
                }
                check_args_loosely(C, call);
                return set_type(call, C->tt->t_error);
            }
            call->as.call.call_kind = CALL_METHOD;
            call->as.call.callee_sym = method;
            what = arena_printf(C->arena, "`%s." STR_FMT "`", bt->name, STR_ARG(name));
            check_positional_args(C, call, what, &method->params, 1);
            return set_type(call, method->type);
        }
        if (bt->kind == TY_STR) {
            b = builtin_lookup(STR_METHODS, name);
            if (b != BI_NONE) {
                return check_str_method(C, call, b);
            }
            error(C, callee->as.field.name_span, "`str` has no method `" STR_FMT "`", STR_ARG(name));
        } else if (bt->kind == TY_LIST) {
            b = builtin_lookup(LIST_METHODS, name);
            if (b != BI_NONE) {
                return check_list_method(C, call, b, bt);
            }
            error(C, callee->as.field.name_span, "`%s` has no method `" STR_FMT "`", bt->name, STR_ARG(name));
        } else if (bt->kind == TY_OPTIONAL) {
            error(C, base->span, "value of type `%s` may be null; check `!= null` before calling a method on it",
                  bt->name);
        } else {
            error(C, callee->as.field.name_span, "`%s` has no methods", bt->name);
        }
        check_args_loosely(C, call);
        return set_type(call, C->tt->t_error);
    }
    error(C, callee->span, "this expression is not callable");
    check_expr(C, callee, NULL);
    check_args_loosely(C, call);
    return set_type(call, C->tt->t_error);
}

static Type *check_expr(Checker *C, Expr *e, Type *expected) {
    switch (e->kind) {
    case EXPR_INT:
        return set_type(e, C->tt->t_int);
    case EXPR_FLOAT:
        return set_type(e, C->tt->t_float);
    case EXPR_BOOL:
        return set_type(e, C->tt->t_bool);
    case EXPR_STRING:
        return set_type(e, C->tt->t_str);
    case EXPR_NULL:
        if (expected != NULL && expected->kind == TY_OPTIONAL) {
            return set_type(e, expected);
        }
        return set_type(e, C->tt->t_null);
    case EXPR_NAME:
        return check_name(C, e);
    case EXPR_FSTRING:
        return check_fstring(C, e);
    case EXPR_LIST:
        return check_list_literal(C, e, expected);
    case EXPR_UNARY:
        return check_unary(C, e);
    case EXPR_BINARY:
        return check_binary(C, e, expected);
    case EXPR_CALL:
        return check_call(C, e);
    case EXPR_FIELD:
        return check_field(C, e);
    case EXPR_INDEX:
        return check_index(C, e);
    case EXPR_SLICE:
        return check_slice(C, e);
    case EXPR_RANGE:
        error(C, e->span, "a range is only valid as the iterable of a `for` loop");
        check_expr(C, e->as.range.lo, NULL);
        check_expr(C, e->as.range.hi, NULL);
        return set_type(e, C->tt->t_error);
    }
    return set_type(e, C->tt->t_error);
}

/* ---- control flow facts ------------------------------------------------------ */

static bool block_falls_through(const Block *b);

static bool block_has_break(const Block *b);

static bool stmt_has_break(const Stmt *s) {
    switch (s->kind) {
    case STMT_BREAK:
        return true;
    case STMT_IF:
        return block_has_break(s->as.if_stmt.then_block) ||
               (s->as.if_stmt.else_stmt != NULL && stmt_has_break(s->as.if_stmt.else_stmt));
    case STMT_BLOCK:
        return block_has_break(s->as.block);
    default:
        return false;
    }
}

static bool block_has_break(const Block *b) {
    for (size_t i = 0; i < b->stmts.len; i++) {
        if (stmt_has_break(b->stmts.data[i])) {
            return true;
        }
    }
    return false;
}

static bool expr_diverges(const Expr *e) {
    return e->kind == EXPR_CALL && e->as.call.call_kind == CALL_BUILTIN &&
           (e->as.call.builtin == BI_PANIC || e->as.call.builtin == BI_EXIT);
}

static bool stmt_falls_through(const Stmt *s) {
    switch (s->kind) {
    case STMT_RETURN:
    case STMT_BREAK:
    case STMT_CONTINUE:
        return false;
    case STMT_EXPR:
        return !expr_diverges(s->as.expr.expr);
    case STMT_IF:
        return block_falls_through(s->as.if_stmt.then_block) ||
               (s->as.if_stmt.else_stmt == NULL || stmt_falls_through(s->as.if_stmt.else_stmt));
    case STMT_WHILE: {
        const Expr *cond = s->as.while_stmt.cond;
        bool forever = cond->kind == EXPR_BOOL && cond->as.bool_val;
        return !(forever && !block_has_break(s->as.while_stmt.body));
    }
    case STMT_BLOCK:
        return block_falls_through(s->as.block);
    default:
        return true;
    }
}

static bool block_falls_through(const Block *b) {
    for (size_t i = 0; i < b->stmts.len; i++) {
        if (!stmt_falls_through(b->stmts.data[i])) {
            return false;
        }
    }
    return true;
}

/* ---- statements ---------------------------------------------------------------- */

static void check_block(Checker *C, Block *b, bool new_scope);
static void check_stmt(Checker *C, Stmt *s);

static void check_var(Checker *C, Stmt *s) {
    Type *declared = s->as.var.type != NULL ? resolve_type(C, s->as.var.type, false) : NULL;
    Type *init = check_expr(C, s->as.var.init, declared);
    Type *type;

    if (declared != NULL) {
        char *what = arena_printf(C->arena, "`" STR_FMT ": %s`", STR_ARG(s->as.var.name), declared->name);
        require_convertible(C, s->as.var.init->span, init, declared, what);
        type = declared;
    } else if (init->kind == TY_NULL) {
        error(C, s->as.var.init->span,
              "cannot infer a type from `null`; write the type, like `" STR_FMT ": int? = null;`",
              STR_ARG(s->as.var.name));
        type = C->tt->t_error;
    } else if (init->kind == TY_VOID) {
        error(C, s->as.var.init->span, "this call returns nothing, so there is no value to store");
        type = C->tt->t_error;
    } else {
        type = init;
    }
    s->as.var.sym = declare_local(C, SYM_LOCAL, s->as.var.name, s->as.var.name_span, type, false);
}

static void check_assign(Checker *C, Stmt *s) {
    Expr *target = s->as.assign.target;
    Expr *value = s->as.assign.value;
    TokKind op = s->as.assign.op;
    Type *target_type;
    Type *value_type;
    Symbol *var = NULL;

    if (target->kind == EXPR_NAME) {
        Symbol *sym = lookup(C, target->as.name.name);
        if (sym == NULL) {
            error(C, target->span, "unknown name `" STR_FMT "`", STR_ARG(target->as.name.name));
            check_expr(C, value, NULL);
            set_type(target, C->tt->t_error);
            return;
        }
        target->as.name.sym = sym;
        if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
            if (sym->immutable) {
                error(C, target->span,
                      str_eq_c(sym->name, "self") ? "cannot assign to `self`"
                                                  : "cannot assign to `" STR_FMT "`; loop variables are immutable",
                      STR_ARG(sym->name));
            }
            var = sym;
            if (op != TOK_ASSIGN) {
                sym->used = true;
            }
        } else if (sym->kind != SYM_GLOBAL) {
            error(C, target->span, "cannot assign to `" STR_FMT "`; it is %s", STR_ARG(sym->name), sym_kind_desc(sym));
            check_expr(C, value, NULL);
            set_type(target, C->tt->t_error);
            return;
        }
        target_type = sym->type;
        set_type(target, op == TOK_ASSIGN ? target_type : effective_type(C, sym));
    } else if (target->kind == EXPR_FIELD) {
        target_type = check_field(C, target);
    } else if (target->kind == EXPR_INDEX) {
        Type *base = check_expr(C, target->as.index.base, NULL);
        if (base->kind == TY_STR) {
            error(C, target->span, "strings are immutable; build a new one instead");
            check_expr(C, target->as.index.index, NULL);
            check_expr(C, value, NULL);
            set_type(target, C->tt->t_error);
            return;
        }
        target_type = check_index(C, target);
    } else {
        target_type = C->tt->t_error;
    }

    if (op == TOK_ASSIGN) {
        value_type = check_expr(C, value, target_type);
        require_convertible(C, value->span, value_type, target_type, "assignment");
    } else {
        Type *result;
        value_type = check_expr(C, value, NULL);
        result = binary_result(C, s->as.assign.value, compound_base_op(op), target->type, value_type);
        if (!is_error(result) && !ty_convertible(result, target_type)) {
            error(C, s->span, "`%s=` would store a `%s` into a variable of type `%s`", op_name(op), result->name,
                  target_type->name);
        }
    }
    if (var != NULL) {
        narrow_push(C, var, var->type);
    }
}

static void check_condition(Checker *C, Expr *cond, const char *what) {
    Type *t = check_expr(C, cond, C->tt->t_bool);
    if (!is_error(t) && t->kind != TY_BOOL) {
        error(C, cond->span, "%s must be a `bool`, found `%s`", what, t->name);
    }
}

static void check_if(Checker *C, Stmt *s) {
    size_t base = C->narrows.len;

    check_condition(C, s->as.if_stmt.cond, "an `if` condition");
    collect_facts(C, s->as.if_stmt.cond, true);
    check_block(C, s->as.if_stmt.then_block, true);
    C->narrows.len = base;
    if (s->as.if_stmt.else_stmt != NULL) {
        collect_facts(C, s->as.if_stmt.cond, false);
        check_stmt(C, s->as.if_stmt.else_stmt);
        C->narrows.len = base;
    }
}

static void check_while(Checker *C, Stmt *s) {
    size_t base = C->narrows.len;

    check_condition(C, s->as.while_stmt.cond, "a `while` condition");
    collect_facts(C, s->as.while_stmt.cond, true);
    C->loop_depth++;
    check_block(C, s->as.while_stmt.body, true);
    C->loop_depth--;
    C->narrows.len = base;
}

static void check_for(Checker *C, Stmt *s) {
    Expr *iter = s->as.for_stmt.iter;
    Type *var_type;

    if (iter->kind == EXPR_RANGE) {
        Type *lo = check_expr(C, iter->as.range.lo, C->tt->t_int);
        Type *hi = check_expr(C, iter->as.range.hi, C->tt->t_int);
        require_convertible(C, iter->as.range.lo->span, lo, C->tt->t_int, "a range bound");
        require_convertible(C, iter->as.range.hi->span, hi, C->tt->t_int, "a range bound");
        set_type(iter, C->tt->t_int);
        var_type = C->tt->t_int;
    } else {
        Type *t = check_expr(C, iter, NULL);
        if (t->kind == TY_LIST) {
            var_type = t->elem;
        } else if (t->kind == TY_STR) {
            var_type = C->tt->t_str;
        } else {
            if (!is_error(t)) {
                if (t->kind == TY_OPTIONAL) {
                    error(C, iter->span, "value of type `%s` may be null; check `!= null` before iterating", t->name);
                } else {
                    error(C, iter->span, "cannot iterate over a value of type `%s`; use a range, list, or str",
                          t->name);
                }
            }
            var_type = C->tt->t_error;
        }
    }
    scope_push(C);
    s->as.for_stmt.sym = declare_local(C, SYM_LOCAL, s->as.for_stmt.name, s->as.for_stmt.name_span, var_type, true);
    s->as.for_stmt.sym->used = true;
    C->loop_depth++;
    check_block(C, s->as.for_stmt.body, false);
    C->loop_depth--;
    scope_pop(C);
}

static void check_return(Checker *C, Stmt *s) {
    Type *ret = C->cur_func->type;
    Expr *value = s->as.ret.value;

    if (ret->kind == TY_VOID) {
        if (value != NULL) {
            error(C, value->span, "`" STR_FMT "` returns nothing, so `return` takes no value",
                  STR_ARG(C->cur_func->name));
            check_expr(C, value, NULL);
        }
        return;
    }
    if (value == NULL) {
        error(C, s->span, "`" STR_FMT "` must return a value of type `%s`", STR_ARG(C->cur_func->name), ret->name);
        return;
    }
    require_convertible(C, value->span, check_expr(C, value, ret), ret, "the return value");
}

static void check_stmt(Checker *C, Stmt *s) {
    switch (s->kind) {
    case STMT_VAR:
        check_var(C, s);
        return;
    case STMT_ASSIGN:
        check_assign(C, s);
        return;
    case STMT_EXPR:
        check_expr(C, s->as.expr.expr, NULL);
        return;
    case STMT_IF:
        check_if(C, s);
        return;
    case STMT_WHILE:
        check_while(C, s);
        return;
    case STMT_FOR:
        check_for(C, s);
        return;
    case STMT_BREAK:
    case STMT_CONTINUE:
        if (C->loop_depth == 0) {
            error(C, s->span, "`%s` outside of a loop", s->kind == STMT_BREAK ? "break" : "continue");
        }
        return;
    case STMT_RETURN:
        check_return(C, s);
        return;
    case STMT_BLOCK:
        check_block(C, s->as.block, true);
        return;
    }
}

static void check_block(Checker *C, Block *b, bool new_scope) {
    size_t base = C->narrows.len;
    bool reachable = true;
    bool warned = false;

    if (new_scope) {
        scope_push(C);
    }
    for (size_t i = 0; i < b->stmts.len; i++) {
        Stmt *s = b->stmts.data[i];
        if (!reachable && !warned) {
            warning(C, s->span, "unreachable code");
            warned = true;
        }
        check_stmt(C, s);
        if (!stmt_falls_through(s)) {
            reachable = false;
        }
        if (s->kind == STMT_IF) {
            bool then_ft = block_falls_through(s->as.if_stmt.then_block);
            bool else_ft = s->as.if_stmt.else_stmt == NULL || stmt_falls_through(s->as.if_stmt.else_stmt);
            if (!then_ft && else_ft) {
                collect_facts(C, s->as.if_stmt.cond, false);
            } else if (then_ft && !else_ft) {
                collect_facts(C, s->as.if_stmt.cond, true);
            }
        }
    }
    C->narrows.len = base;
    if (new_scope) {
        scope_pop(C);
    }
}

/* ---- declarations ------------------------------------------------------------- */

static void check_function(Checker *C, Symbol *fn) {
    FuncDecl *decl = fn->func;

    C->cur_func = fn;
    C->loop_depth = 0;
    fn->nlocals = 0;
    scope_push(C);
    for (size_t i = 0; i < decl->params.len; i++) {
        Param *p = &decl->params.data[i];
        Symbol *sym = declare_local(C, SYM_PARAM, p->name, p->span, fn->params.data[i], p->type == NULL);
        sym->used = true;
        p->sym = sym;
    }
    check_block(C, decl->body, false);
    if (fn->type->kind != TY_VOID && block_falls_through(decl->body)) {
        error(C, decl->name_span, "`" STR_FMT "` can reach the end of its body without returning a `%s`",
              STR_ARG(fn->name), fn->type->name);
    }
    scope_pop(C);
    C->cur_func = NULL;
}

static void add_type_name(Checker *C, const char *name, Type *type) {
    Symbol *sym = new_sym(C, SYM_TYPE, str_from(name), span_make(0, 0));
    sym->type = type;
    strmap_put(&C->global->names, sym->name, sym);
}

static void add_builtins(Checker *C) {
    add_type_name(C, "int", C->tt->t_int);
    add_type_name(C, "float", C->tt->t_float);
    add_type_name(C, "bool", C->tt->t_bool);
    add_type_name(C, "str", C->tt->t_str);
    add_type_name(C, "void", C->tt->t_void);
    add_type_name(C, "list", NULL);
    for (const BuiltinInfo *b = BUILTIN_FUNCS; b->name != NULL; b++) {
        Symbol *sym = new_sym(C, SYM_BUILTIN, str_from(b->name), span_make(0, 0));
        sym->builtin = b->id;
        strmap_put(&C->global->names, sym->name, sym);
    }
}

static char *mangle(Checker *C, const char *prefix, Str a, Str b) {
    if (b.n == 0) {
        return arena_printf(C->arena, "%s" STR_FMT, prefix, STR_ARG(a));
    }
    return arena_printf(C->arena, "%s" STR_FMT "__" STR_FMT, prefix, STR_ARG(a), STR_ARG(b));
}

/* Pass 1: every top-level name goes into the global scope. */
static void collect_decls(Checker *C) {
    for (size_t i = 0; i < C->m->decls.len; i++) {
        Decl *d = C->m->decls.data[i];
        switch (d->kind) {
        case DECL_FUNC: {
            Symbol *sym = new_sym(C, SYM_FUNC, d->as.func->name, d->as.func->name_span);
            sym->func = d->as.func;
            sym->mangled = mangle(C, "rv_fn_", sym->name, (Str){0});
            d->as.func->sym = sym;
            if (declare(C, sym)) {
                vec_push(&C->prog->funcs, sym);
            }
            break;
        }
        case DECL_STRUCT: {
            StructDecl *st = d->as.st;
            Symbol *sym = new_sym(C, SYM_STRUCT, st->name, st->name_span);
            sym->st = st;
            sym->type = ty_struct(C->tt, sym);
            sym->mangled = mangle(C, "rv_ti_", sym->name, (Str){0});
            strmap_init(&sym->members);
            strmap_init(&sym->methods);
            st->sym = sym;
            if (declare(C, sym)) {
                vec_push(&C->prog->structs, sym);
            }
            for (size_t j = 0; j < st->methods.len; j++) {
                FuncDecl *m = st->methods.data[j];
                Symbol *msym = new_sym(C, SYM_METHOD, m->name, m->name_span);
                msym->func = m;
                msym->owner = sym;
                msym->mangled = mangle(C, "rv_fn_", sym->name, m->name);
                m->sym = msym;
                if (strmap_get(&sym->methods, m->name) != NULL) {
                    error(C, m->name_span, "`" STR_FMT "` already has a method named `" STR_FMT "`", STR_ARG(st->name),
                          STR_ARG(m->name));
                    continue;
                }
                strmap_put(&sym->methods, m->name, msym);
                vec_push(&C->prog->funcs, msym);
            }
            break;
        }
        case DECL_GLOBAL: {
            Symbol *sym = new_sym(C, SYM_GLOBAL, d->as.global->name, d->as.global->name_span);
            sym->global = d->as.global;
            sym->mangled = mangle(C, "rv_g_", sym->name, (Str){0});
            d->as.global->sym = sym;
            if (declare(C, sym)) {
                vec_push(&C->prog->globals, sym);
            }
            break;
        }
        }
    }
}

static void resolve_signature(Checker *C, Symbol *fn) {
    FuncDecl *decl = fn->func;
    for (size_t i = 0; i < decl->params.len; i++) {
        Param *p = &decl->params.data[i];
        Type *t = p->type == NULL ? fn->owner->type : resolve_type(C, p->type, false);
        for (size_t j = 0; j < i; j++) {
            if (str_eq(decl->params.data[j].name, p->name)) {
                error(C, p->span, "duplicate parameter name `" STR_FMT "`", STR_ARG(p->name));
            }
        }
        vec_push(&fn->params, t);
    }
    fn->type = decl->ret == NULL ? C->tt->t_void : resolve_type(C, decl->ret, true);
}

/* Reports structs that require themselves through non-optional fields. */
static void check_struct_cycles(Checker *C) {
    SymVec *structs = &C->prog->structs;
    int *state = xcalloc(structs->len ? structs->len : 1, sizeof *state); /* 0 new, 1 visiting, 2 done */
    Symbol **stack = xcalloc(structs->len ? structs->len : 1, sizeof *stack);
    size_t depth = 0;

    for (size_t i = 0; i < structs->len; i++) {
        structs->data[i]->slot = (int)i;
    }
    for (size_t root = 0; root < structs->len; root++) {
        /* iterative DFS with an explicit stack of (struct, next field) pairs */
        size_t *next = xcalloc(structs->len ? structs->len : 1, sizeof *next);
        if (state[root] != 0) {
            free(next);
            continue;
        }
        stack[depth++] = structs->data[root];
        state[root] = 1;
        while (depth > 0) {
            Symbol *st = stack[depth - 1];
            size_t idx = (size_t)st->slot;
            bool pushed = false;
            while (next[idx] < st->fields.len) {
                FieldInfo *f = &st->fields.data[next[idx]++];
                Symbol *target;
                if (f->type->kind != TY_STRUCT) {
                    continue;
                }
                target = f->type->st;
                if (state[target->slot] == 1) {
                    error(C, f->span,
                          "field `" STR_FMT "` of `" STR_FMT "` requires a `%s`, and `%s` in turn requires a `" STR_FMT
                          "`; nothing could ever be constructed, so make one of the fields optional",
                          STR_ARG(f->name), STR_ARG(st->name), f->type->name, f->type->name, STR_ARG(st->name));
                    continue;
                }
                if (state[target->slot] == 0) {
                    state[target->slot] = 1;
                    stack[depth++] = target;
                    pushed = true;
                    break;
                }
            }
            if (!pushed) {
                state[idx] = 2;
                depth--;
            }
        }
        free(next);
    }
    free(state);
    free(stack);
}

/* Pass 2: field types, method and function signatures, global annotations. */
static void resolve_decls(Checker *C) {
    for (size_t i = 0; i < C->prog->structs.len; i++) {
        Symbol *st = C->prog->structs.data[i];
        for (size_t j = 0; j < st->st->fields.len; j++) {
            FieldDecl *fd = &st->st->fields.data[j];
            FieldInfo info;
            if (strmap_get(&st->members, fd->name) != NULL || strmap_get(&st->methods, fd->name) != NULL) {
                error(C, fd->span, "`" STR_FMT "` already has a member named `" STR_FMT "`", STR_ARG(st->name),
                      STR_ARG(fd->name));
                continue;
            }
            info.name = fd->name;
            info.span = fd->span;
            info.type = resolve_type(C, fd->type, false);
            info.index = (int)st->fields.len;
            vec_push(&st->fields, info);
            strmap_put(&st->members, info.name, &st->fields.data[st->fields.len - 1]);
        }
        /* the vec may have reallocated while growing; rebuild the pointers */
        for (size_t j = 0; j < st->fields.len; j++) {
            strmap_put(&st->members, st->fields.data[j].name, &st->fields.data[j]);
        }
    }
    for (size_t i = 0; i < C->prog->funcs.len; i++) {
        resolve_signature(C, C->prog->funcs.data[i]);
    }
    for (size_t i = 0; i < C->prog->globals.len; i++) {
        Symbol *g = C->prog->globals.data[i];
        if (g->global->type != NULL) {
            g->type = resolve_type(C, g->global->type, false);
        }
    }
    check_struct_cycles(C);
}

/* Pass 3: globals, in order, typed and folded. */
static void check_globals(Checker *C) {
    for (size_t i = 0; i < C->prog->globals.len; i++) {
        Symbol *g = C->prog->globals.data[i];
        Expr *init = g->global->init;
        Type *t = check_expr(C, init, g->type);

        if (g->type != NULL) {
            char *what = arena_printf(C->arena, "`" STR_FMT ": %s`", STR_ARG(g->name), g->type->name);
            require_convertible(C, init->span, t, g->type, what);
        } else if (t->kind == TY_NULL) {
            error(C, init->span, "cannot infer a type from `null`; write the type, like `" STR_FMT ": int? = null;`",
                  STR_ARG(g->name));
            g->type = C->tt->t_error;
        } else {
            g->type = t;
        }
        if (!is_error(g->type) && !is_error(t)) {
            if (g->type->kind == TY_LIST || g->type->kind == TY_STRUCT ||
                (g->type->kind == TY_OPTIONAL && init->kind != EXPR_NULL)) {
                error(C, init->span, "a global of type `%s` cannot be initialized at compile time; build it in `main`",
                      g->type->name);
            } else if (fold_const(init, C->tt, C->arena, C->diags, C->src, &g->value)) {
                if (g->type->kind == TY_FLOAT && g->value.type->kind == TY_INT) {
                    g->value.as.f = (double)g->value.as.i;
                }
                g->value.type = g->type;
                g->folded = true;
            }
        }
    }
}

static void check_main(Checker *C) {
    Symbol *sym = lookup(C, STR("main"));
    if (sym == NULL || sym->kind != SYM_FUNC) {
        diag_error_nowhere(C->diags, "no `main` function; every program needs `func main() -> int` or `func main()`");
        return;
    }
    if (sym->params.len != 0) {
        error(C, sym->span, "`main` takes no parameters; use `args()` to read the command line");
    }
    if (sym->type->kind != TY_INT && sym->type->kind != TY_VOID) {
        error(C, sym->span, "`main` must return `int` or nothing, not `%s`", sym->type->name);
    }
    C->prog->main_fn = sym;
}

bool sema_check(Module *m, Arena *arena, Diags *diags, Program *out) {
    Checker C;

    memset(&C, 0, sizeof C);
    memset(out, 0, sizeof *out);
    out->module = m;
    types_init(&out->types, arena);
    C.m = m;
    C.arena = arena;
    C.diags = diags;
    C.src = m->src;
    C.prog = out;
    C.tt = &out->types;
    C.global = scope_new(&C, NULL);
    C.scope = C.global;

    add_builtins(&C);
    collect_decls(&C);
    resolve_decls(&C);
    check_globals(&C);
    for (size_t i = 0; i < out->funcs.len; i++) {
        check_function(&C, out->funcs.data[i]);
    }
    check_main(&C);

    vec_free(&C.narrows);
    scope_release(C.global);
    return diags_ok(diags);
}

void program_free(Program *p) {
    for (size_t i = 0; i < p->structs.len; i++) {
        strmap_free(&p->structs.data[i]->members);
        strmap_free(&p->structs.data[i]->methods);
        vec_free(&p->structs.data[i]->fields);
    }
    for (size_t i = 0; i < p->funcs.len; i++) {
        vec_free(&p->funcs.data[i]->params);
    }
    vec_free(&p->funcs);
    vec_free(&p->structs);
    vec_free(&p->globals);
    types_free(&p->types);
}
