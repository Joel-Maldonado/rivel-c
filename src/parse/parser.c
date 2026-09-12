#include "parse/parser.h"

#include <stdarg.h>

typedef struct Parser {
    const Source *src;
    const Token *toks;
    size_t ntoks;
    size_t pos;
    Arena *arena;
    Diags *diags;
    bool panicking; /* suppress cascaded errors until the next sync point */
} Parser;

/* ---- token access -------------------------------------------------------- */

static const Token *peek(const Parser *p) {
    return &p->toks[p->pos];
}

static const Token *peek_at(const Parser *p, size_t ahead) {
    size_t i = p->pos + ahead;
    return &p->toks[i < p->ntoks ? i : p->ntoks - 1];
}

static const Token *prev(const Parser *p) {
    return &p->toks[p->pos > 0 ? p->pos - 1 : 0];
}

static bool at(const Parser *p, TokKind k) {
    return peek(p)->kind == k;
}

static bool at_eof(const Parser *p) {
    return at(p, TOK_EOF);
}

static const Token *advance(Parser *p) {
    const Token *t = peek(p);
    if (!at_eof(p)) {
        p->pos++;
    }
    return t;
}

static bool match(Parser *p, TokKind k) {
    if (at(p, k)) {
        advance(p);
        return true;
    }
    return false;
}

/* ---- errors ------------------------------------------------------------- */

static void error_span(Parser *p, Span span, const char *fmt, ...) PRINTF_LIKE(3, 4);
static void error_span(Parser *p, Span span, const char *fmt, ...) {
    va_list args;
    char *msg;

    if (p->panicking) {
        return;
    }
    p->panicking = true;
    va_start(args, fmt);
    msg = arena_vprintf(p->arena, fmt, args);
    va_end(args);
    diag_error(p->diags, p->src, span, "%s", msg);
}

/* Reports without entering panic mode: parsing can continue normally after these. */
static void soft_error(Parser *p, Span span, const char *fmt, ...) PRINTF_LIKE(3, 4);
static void soft_error(Parser *p, Span span, const char *fmt, ...) {
    va_list args;
    char *msg;

    if (p->panicking) {
        return;
    }
    va_start(args, fmt);
    msg = arena_vprintf(p->arena, fmt, args);
    va_end(args);
    diag_error(p->diags, p->src, span, "%s", msg);
}

static void error_here(Parser *p, const char *what) {
    const Token *t = peek(p);
    error_span(p, t->span, "expected %s, found %s", what, tok_kind_name(t->kind));
}

static const Token *expect(Parser *p, TokKind k, const char *what) {
    if (at(p, k)) {
        return advance(p);
    }
    error_here(p, what);
    return NULL;
}

/* Skips to the end of the current statement: past a `;` or up to a `}` or EOF. */
static void sync_stmt(Parser *p) {
    int depth = 0;
    p->panicking = false;
    while (!at_eof(p)) {
        TokKind k = peek(p)->kind;
        if (depth == 0) {
            if (k == TOK_SEMI) {
                advance(p);
                return;
            }
            if (k == TOK_RBRACE) {
                return;
            }
            if (k == TOK_KW_IF || k == TOK_KW_WHILE || k == TOK_KW_FOR || k == TOK_KW_RETURN || k == TOK_KW_FUNC ||
                k == TOK_KW_STRUCT) {
                return;
            }
        }
        if (k == TOK_LBRACE) {
            depth++;
        } else if (k == TOK_RBRACE) {
            depth--;
        }
        advance(p);
    }
}

/* Skips to the next top-level declaration. */
static void sync_decl(Parser *p) {
    int depth = 0;
    p->panicking = false;
    while (!at_eof(p)) {
        TokKind k = peek(p)->kind;
        if (depth == 0 && (k == TOK_KW_FUNC || k == TOK_KW_STRUCT)) {
            return;
        }
        if (depth == 0 && k == TOK_SEMI) {
            advance(p);
            return;
        }
        if (k == TOK_LBRACE) {
            depth++;
        } else if (k == TOK_RBRACE) {
            if (depth > 0) {
                depth--;
            }
            if (depth == 0) {
                advance(p);
                return;
            }
        }
        advance(p);
    }
}

/* ---- node constructors -------------------------------------------------- */

static Expr *new_expr(Parser *p, ExprKind kind, Span span) {
    Expr *e = arena_new(p->arena, Expr);
    e->kind = kind;
    e->span = span;
    return e;
}

static Stmt *new_stmt(Parser *p, StmtKind kind, Span span) {
    Stmt *s = arena_new(p->arena, Stmt);
    s->kind = kind;
    s->span = span;
    return s;
}

static Span span_from(const Parser *p, uint32_t lo) {
    return span_make(lo, prev(p)->span.hi);
}

/* ---- types -------------------------------------------------------------- */

static TypeExpr *parse_type(Parser *p) {
    TypeExpr *t;
    const Token *name;

    if (!at(p, TOK_IDENT)) {
        error_here(p, "a type");
        return NULL;
    }
    name = advance(p);
    t = arena_new(p->arena, TypeExpr);
    if (str_eq_c(name->text, "list") && at(p, TOK_LBRACKET)) {
        advance(p);
        t->kind = TYPEX_LIST;
        t->inner = parse_type(p);
        expect(p, TOK_RBRACKET, "`]` after list element type");
    } else {
        t->kind = TYPEX_NAME;
        t->name = name->text;
    }
    t->span = span_from(p, name->span.lo);
    while (at(p, TOK_QUESTION)) {
        TypeExpr *opt = arena_new(p->arena, TypeExpr);
        advance(p);
        opt->kind = TYPEX_OPTIONAL;
        opt->inner = t;
        opt->span = span_from(p, name->span.lo);
        t = opt;
    }
    return t;
}

/* ---- expressions ---------------------------------------------------------- */

static Expr *parse_expr(Parser *p);

static void parse_args(Parser *p, ArgVec *args, TokKind close, const char *close_name) {
    while (!at(p, close) && !at_eof(p)) {
        Arg arg;
        memset(&arg, 0, sizeof arg);
        if (at(p, TOK_IDENT) && peek_at(p, 1)->kind == TOK_COLON) {
            const Token *label = advance(p);
            arg.label = label->text;
            arg.label_span = label->span;
            advance(p);
        }
        arg.value = parse_expr(p);
        avec_push(p->arena, args, arg);
        if (!match(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, close, close_name);
}

static Expr *parse_fstring(Parser *p) {
    const Token *start = advance(p);
    Expr *e = new_expr(p, EXPR_FSTRING, start->span);

    while (!at(p, TOK_FSTR_END) && !at_eof(p)) {
        if (at(p, TOK_FSTR_TEXT)) {
            const Token *t = advance(p);
            Expr *text = new_expr(p, EXPR_STRING, t->span);
            text->as.str_val = t->as.str_val;
            avec_push(p->arena, &e->as.fstring.parts, text);
        } else if (at(p, TOK_FSTR_EXPR_START)) {
            Expr *hole;
            advance(p);
            if (at(p, TOK_FSTR_EXPR_END)) {
                soft_error(p, peek(p)->span, "empty `{}` in f-string");
            }
            hole = parse_expr(p);
            avec_push(p->arena, &e->as.fstring.parts, hole);
            expect(p, TOK_FSTR_EXPR_END, "`}` to close the f-string expression");
        } else {
            error_here(p, "f-string text or `{`");
            break;
        }
    }
    match(p, TOK_FSTR_END);
    e->span = span_from(p, start->span.lo);
    return e;
}

static Expr *parse_primary(Parser *p) {
    const Token *t = peek(p);
    Expr *e;

    switch (t->kind) {
    case TOK_INT:
        advance(p);
        e = new_expr(p, EXPR_INT, t->span);
        if (t->as.int_val > (uint64_t)INT64_MAX) {
            error_span(p, t->span, "integer literal is too large for int");
        } else {
            e->as.int_val = (int64_t)t->as.int_val;
        }
        return e;
    case TOK_FLOAT:
        advance(p);
        e = new_expr(p, EXPR_FLOAT, t->span);
        e->as.float_val = t->as.float_val;
        return e;
    case TOK_STRING:
        advance(p);
        e = new_expr(p, EXPR_STRING, t->span);
        e->as.str_val = t->as.str_val;
        return e;
    case TOK_FSTR_START:
        return parse_fstring(p);
    case TOK_KW_TRUE:
    case TOK_KW_FALSE:
        advance(p);
        e = new_expr(p, EXPR_BOOL, t->span);
        e->as.bool_val = t->kind == TOK_KW_TRUE;
        return e;
    case TOK_KW_NULL:
        advance(p);
        return new_expr(p, EXPR_NULL, t->span);
    case TOK_IDENT:
    case TOK_KW_SELF:
        advance(p);
        e = new_expr(p, EXPR_NAME, t->span);
        e->as.name.name = t->text;
        return e;
    case TOK_LPAREN: {
        Expr *inner;
        advance(p);
        inner = parse_expr(p);
        expect(p, TOK_RPAREN, "`)` to close the parenthesized expression");
        return inner;
    }
    case TOK_LBRACKET: {
        ArgVec args = {0};
        advance(p);
        e = new_expr(p, EXPR_LIST, t->span);
        parse_args(p, &args, TOK_RBRACKET, "`]` to close the list");
        for (size_t i = 0; i < args.len; i++) {
            if (args.data[i].label.n != 0) {
                soft_error(p, args.data[i].label_span, "list elements cannot have labels");
            }
            avec_push(p->arena, &e->as.list.items, args.data[i].value);
        }
        e->span = span_from(p, t->span.lo);
        return e;
    }
    default:
        error_here(p, "an expression");
        e = new_expr(p, EXPR_INT, t->span);
        return e;
    }
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);

    for (;;) {
        if (at(p, TOK_LPAREN)) {
            Expr *call = new_expr(p, EXPR_CALL, e->span);
            advance(p);
            call->as.call.callee = e;
            parse_args(p, &call->as.call.args, TOK_RPAREN, "`)` to close the argument list");
            call->span = span_from(p, e->span.lo);
            e = call;
        } else if (at(p, TOK_DOT)) {
            const Token *name;
            Expr *field;
            advance(p);
            name = expect(p, TOK_IDENT, "a field or method name after `.`");
            field = new_expr(p, EXPR_FIELD, e->span);
            field->as.field.base = e;
            if (name != NULL) {
                field->as.field.name = name->text;
                field->as.field.name_span = name->span;
            }
            field->span = span_from(p, e->span.lo);
            e = field;
        } else if (at(p, TOK_LBRACKET)) {
            Expr *lo;
            advance(p);
            lo = parse_expr(p);
            if (at(p, TOK_DOTDOT)) {
                Expr *slice = new_expr(p, EXPR_SLICE, e->span);
                advance(p);
                slice->as.slice.base = e;
                slice->as.slice.lo = lo;
                slice->as.slice.hi = parse_expr(p);
                expect(p, TOK_RBRACKET, "`]` to close the slice");
                slice->span = span_from(p, e->span.lo);
                e = slice;
            } else {
                Expr *index = new_expr(p, EXPR_INDEX, e->span);
                index->as.index.base = e;
                index->as.index.index = lo;
                expect(p, TOK_RBRACKET, "`]` to close the index");
                index->span = span_from(p, e->span.lo);
                e = index;
            }
        } else {
            return e;
        }
    }
}

static Expr *parse_unary(Parser *p) {
    const Token *t = peek(p);

    if (t->kind == TOK_MINUS || t->kind == TOK_BANG || t->kind == TOK_TILDE) {
        Expr *operand;
        Expr *e;
        advance(p);
        if (t->kind == TOK_MINUS && at(p, TOK_INT)) {
            const Token *lit = advance(p);
            e = new_expr(p, EXPR_INT, span_join(t->span, lit->span));
            e->as.int_val = lit->as.int_val == (uint64_t)1 << 63 ? INT64_MIN : -(int64_t)lit->as.int_val;
            return e;
        }
        operand = parse_unary(p);
        e = new_expr(p, EXPR_UNARY, span_join(t->span, operand->span));
        e->as.unary.op = t->kind;
        e->as.unary.operand = operand;
        return e;
    }
    return parse_postfix(p);
}

static Expr *make_binary(Parser *p, TokKind op, Expr *lhs, Expr *rhs) {
    Expr *e = new_expr(p, EXPR_BINARY, span_join(lhs->span, rhs->span));
    e->as.binary.op = op;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    return e;
}

static bool is_mul_op(TokKind k) {
    return k == TOK_STAR || k == TOK_SLASH || k == TOK_PERCENT || k == TOK_SHL || k == TOK_SHR || k == TOK_AMP;
}

static bool is_add_op(TokKind k) {
    return k == TOK_PLUS || k == TOK_MINUS || k == TOK_PIPE || k == TOK_CARET;
}

static bool is_cmp_op(TokKind k) {
    return k == TOK_EQ || k == TOK_NE || k == TOK_LT || k == TOK_LE || k == TOK_GT || k == TOK_GE;
}

static Expr *parse_mul(Parser *p) {
    Expr *e = parse_unary(p);
    while (is_mul_op(peek(p)->kind)) {
        TokKind op = advance(p)->kind;
        e = make_binary(p, op, e, parse_unary(p));
    }
    return e;
}

static Expr *parse_add(Parser *p) {
    Expr *e = parse_mul(p);
    while (is_add_op(peek(p)->kind)) {
        TokKind op = advance(p)->kind;
        e = make_binary(p, op, e, parse_mul(p));
    }
    return e;
}

static Expr *parse_cmp(Parser *p) {
    Expr *e = parse_add(p);
    if (is_cmp_op(peek(p)->kind)) {
        TokKind op = advance(p)->kind;
        e = make_binary(p, op, e, parse_add(p));
        if (is_cmp_op(peek(p)->kind)) {
            soft_error(p, peek(p)->span, "comparisons cannot be chained; use `&&` between them");
            advance(p);
            parse_add(p);
        }
    }
    return e;
}

static Expr *parse_and(Parser *p) {
    Expr *e = parse_cmp(p);
    while (at(p, TOK_ANDAND)) {
        advance(p);
        e = make_binary(p, TOK_ANDAND, e, parse_cmp(p));
    }
    return e;
}

static Expr *parse_or(Parser *p) {
    Expr *e = parse_and(p);
    while (at(p, TOK_OROR)) {
        advance(p);
        e = make_binary(p, TOK_OROR, e, parse_and(p));
    }
    return e;
}

static Expr *parse_expr(Parser *p) {
    Expr *e = parse_or(p);
    if (at(p, TOK_QQ)) {
        advance(p);
        e = make_binary(p, TOK_QQ, e, parse_expr(p));
    }
    return e;
}

/* ---- statements ----------------------------------------------------------- */

static Block *parse_block(Parser *p);
static Stmt *parse_stmt(Parser *p);

static void expect_semi(Parser *p, const char *after) {
    if (at(p, TOK_DOTDOT) || at(p, TOK_DOTDOTEQ)) {
        error_span(p, peek(p)->span, "a range is only valid as the iterable of a `for` loop");
        return;
    }
    if (!match(p, TOK_SEMI)) {
        const Token *t = peek(p);
        Span where = prev(p)->span;
        where.lo = where.hi;
        (void)t;
        error_span(p, where, "expected `;` after %s", after);
    }
}

static Stmt *parse_var_stmt(Parser *p) {
    const Token *name = advance(p);
    Stmt *s = new_stmt(p, STMT_VAR, name->span);

    s->as.var.name = name->text;
    s->as.var.name_span = name->span;
    if (match(p, TOK_COLON)) {
        s->as.var.type = parse_type(p);
        expect(p, TOK_ASSIGN, "`=` after the variable's type");
    } else {
        expect(p, TOK_DEFINE, "`:=`");
    }
    s->as.var.init = parse_expr(p);
    expect_semi(p, "the variable declaration");
    s->span = span_from(p, name->span.lo);
    return s;
}

static Stmt *parse_if_stmt(Parser *p) {
    const Token *kw = advance(p);
    Stmt *s = new_stmt(p, STMT_IF, kw->span);

    s->as.if_stmt.cond = parse_expr(p);
    s->as.if_stmt.then_block = parse_block(p);
    if (match(p, TOK_KW_ELSE)) {
        if (at(p, TOK_KW_IF)) {
            s->as.if_stmt.else_stmt = parse_if_stmt(p);
        } else {
            Stmt *b = new_stmt(p, STMT_BLOCK, peek(p)->span);
            b->as.block = parse_block(p);
            b->span = b->as.block->span;
            s->as.if_stmt.else_stmt = b;
        }
    }
    s->span = span_from(p, kw->span.lo);
    return s;
}

static Stmt *parse_while_stmt(Parser *p) {
    const Token *kw = advance(p);
    Stmt *s = new_stmt(p, STMT_WHILE, kw->span);

    s->as.while_stmt.cond = parse_expr(p);
    s->as.while_stmt.body = parse_block(p);
    s->span = span_from(p, kw->span.lo);
    return s;
}

static Stmt *parse_for_stmt(Parser *p) {
    const Token *kw = advance(p);
    const Token *name = expect(p, TOK_IDENT, "a loop variable name after `for`");
    Stmt *s = new_stmt(p, STMT_FOR, kw->span);
    Expr *iter;

    if (name != NULL) {
        s->as.for_stmt.name = name->text;
        s->as.for_stmt.name_span = name->span;
    }
    expect(p, TOK_KW_IN, "`in` after the loop variable");
    iter = parse_expr(p);
    if (at(p, TOK_DOTDOT) || at(p, TOK_DOTDOTEQ)) {
        Expr *range = new_expr(p, EXPR_RANGE, iter->span);
        range->as.range.inclusive = advance(p)->kind == TOK_DOTDOTEQ;
        range->as.range.lo = iter;
        range->as.range.hi = parse_expr(p);
        range->span = span_join(iter->span, range->as.range.hi->span);
        iter = range;
    }
    s->as.for_stmt.iter = iter;
    s->as.for_stmt.body = parse_block(p);
    s->span = span_from(p, kw->span.lo);
    return s;
}

static bool is_assign_op(TokKind k) {
    return k == TOK_ASSIGN || k == TOK_PLUS_ASSIGN || k == TOK_MINUS_ASSIGN || k == TOK_STAR_ASSIGN ||
           k == TOK_SLASH_ASSIGN || k == TOK_PERCENT_ASSIGN;
}

static Stmt *parse_expr_stmt(Parser *p) {
    Expr *e = parse_expr(p);
    Stmt *s;

    if (is_assign_op(peek(p)->kind)) {
        TokKind op = advance(p)->kind;
        s = new_stmt(p, STMT_ASSIGN, e->span);
        s->as.assign.target = e;
        s->as.assign.op = op;
        s->as.assign.value = parse_expr(p);
        if (e->kind != EXPR_NAME && e->kind != EXPR_FIELD && e->kind != EXPR_INDEX) {
            soft_error(p, e->span,
                       "cannot assign to this expression; the target must be a variable, field, or element");
        }
        expect_semi(p, "the assignment");
        s->span = span_from(p, e->span.lo);
        return s;
    }
    s = new_stmt(p, STMT_EXPR, e->span);
    s->as.expr.expr = e;
    if (e->kind != EXPR_CALL) {
        soft_error(p, e->span, "only calls can be used as statements");
    }
    expect_semi(p, "the call");
    s->span = span_from(p, e->span.lo);
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    const Token *t = peek(p);
    Stmt *s;

    switch (t->kind) {
    case TOK_IDENT:
        if (peek_at(p, 1)->kind == TOK_DEFINE || peek_at(p, 1)->kind == TOK_COLON) {
            return parse_var_stmt(p);
        }
        return parse_expr_stmt(p);
    case TOK_KW_IF:
        return parse_if_stmt(p);
    case TOK_KW_WHILE:
        return parse_while_stmt(p);
    case TOK_KW_FOR:
        return parse_for_stmt(p);
    case TOK_KW_BREAK:
    case TOK_KW_CONTINUE:
        advance(p);
        s = new_stmt(p, t->kind == TOK_KW_BREAK ? STMT_BREAK : STMT_CONTINUE, t->span);
        expect_semi(p, t->kind == TOK_KW_BREAK ? "`break`" : "`continue`");
        return s;
    case TOK_KW_RETURN:
        advance(p);
        s = new_stmt(p, STMT_RETURN, t->span);
        if (!at(p, TOK_SEMI)) {
            s->as.ret.value = parse_expr(p);
        }
        expect_semi(p, "`return`");
        s->span = span_from(p, t->span.lo);
        return s;
    case TOK_LBRACE:
        s = new_stmt(p, STMT_BLOCK, t->span);
        s->as.block = parse_block(p);
        s->span = s->as.block->span;
        return s;
    case TOK_KW_FUNC:
    case TOK_KW_STRUCT:
        error_span(p, t->span, "%s declarations are only allowed at the top level",
                   t->kind == TOK_KW_FUNC ? "function" : "struct");
        return NULL;
    default:
        return parse_expr_stmt(p);
    }
}

static Block *parse_block(Parser *p) {
    Block *b = arena_new(p->arena, Block);
    const Token *open = expect(p, TOK_LBRACE, "`{`");

    b->span = open ? open->span : peek(p)->span;
    if (open == NULL) {
        sync_stmt(p);
        return b;
    }
    while (!at(p, TOK_RBRACE) && !at_eof(p)) {
        Stmt *s = parse_stmt(p);
        if (s != NULL) {
            avec_push(p->arena, &b->stmts, s);
        }
        if (p->panicking) {
            sync_stmt(p);
        }
    }
    expect(p, TOK_RBRACE, "`}` to close the block");
    b->span = span_from(p, b->span.lo);
    return b;
}

/* ---- declarations ---------------------------------------------------------- */

static void parse_params(Parser *p, ParamVec *params) {
    while (!at(p, TOK_RPAREN) && !at_eof(p)) {
        Param param;
        const Token *name;

        memset(&param, 0, sizeof param);
        name = expect(p, TOK_IDENT, "a parameter name");
        if (name == NULL) {
            return;
        }
        param.name = name->text;
        param.span = name->span;
        expect(p, TOK_COLON, "`:` after the parameter name");
        param.type = parse_type(p);
        avec_push(p->arena, params, param);
        if (!match(p, TOK_COMMA)) {
            break;
        }
    }
}

static FuncDecl *parse_func(Parser *p, StructDecl *owner) {
    const Token *kw = advance(p);
    const Token *name = expect(p, TOK_IDENT, "a function name after `func`");
    FuncDecl *f = arena_new(p->arena, FuncDecl);

    f->span = kw->span;
    f->owner = owner;
    if (name != NULL) {
        f->name = name->text;
        f->name_span = name->span;
    }
    expect(p, TOK_LPAREN, "`(` after the function name");
    if (owner != NULL) {
        if (at(p, TOK_KW_SELF)) {
            Param self;
            const Token *t = advance(p);
            memset(&self, 0, sizeof self);
            self.name = t->text;
            self.span = t->span;
            avec_push(p->arena, &f->params, self);
            if (!at(p, TOK_RPAREN)) {
                expect(p, TOK_COMMA, "`,` after `self`");
            }
        } else {
            soft_error(p, peek(p)->span, "a method's first parameter must be `self`");
        }
    }
    parse_params(p, &f->params);
    expect(p, TOK_RPAREN, "`)` after the parameter list");
    if (match(p, TOK_ARROW)) {
        f->ret = parse_type(p);
    }
    f->body = parse_block(p);
    f->span = span_from(p, kw->span.lo);
    return f;
}

static StructDecl *parse_struct(Parser *p) {
    const Token *kw = advance(p);
    const Token *name = expect(p, TOK_IDENT, "a struct name after `struct`");
    StructDecl *st = arena_new(p->arena, StructDecl);

    st->span = kw->span;
    if (name != NULL) {
        st->name = name->text;
        st->name_span = name->span;
    }
    expect(p, TOK_LBRACE, "`{` after the struct name");
    while (!at(p, TOK_RBRACE) && !at_eof(p)) {
        if (at(p, TOK_KW_FUNC)) {
            avec_push(p->arena, &st->methods, parse_func(p, st));
        } else if (at(p, TOK_IDENT)) {
            FieldDecl field;
            const Token *fname = advance(p);
            memset(&field, 0, sizeof field);
            field.name = fname->text;
            field.span = fname->span;
            expect(p, TOK_COLON, "`:` after the field name");
            field.type = parse_type(p);
            expect_semi(p, "the field declaration");
            avec_push(p->arena, &st->fields, field);
        } else {
            error_here(p, "a field or method");
        }
        if (p->panicking) {
            sync_stmt(p);
        }
    }
    expect(p, TOK_RBRACE, "`}` to close the struct");
    st->span = span_from(p, kw->span.lo);
    return st;
}

static GlobalDecl *parse_global(Parser *p) {
    const Token *name = advance(p);
    GlobalDecl *g = arena_new(p->arena, GlobalDecl);

    g->name = name->text;
    g->name_span = name->span;
    if (match(p, TOK_COLON)) {
        g->type = parse_type(p);
        expect(p, TOK_ASSIGN, "`=` after the global's type");
    } else {
        expect(p, TOK_DEFINE, "`:=`");
    }
    g->init = parse_expr(p);
    expect_semi(p, "the global declaration");
    g->span = span_from(p, name->span.lo);
    return g;
}

Module *parse_module(const Source *src, const TokenVec *tokens, Arena *arena, Diags *diags) {
    Parser p;
    Module *m = arena_new(arena, Module);

    memset(&p, 0, sizeof p);
    p.src = src;
    p.toks = tokens->data;
    p.ntoks = tokens->len;
    p.arena = arena;
    p.diags = diags;
    m->src = src;

    while (!at_eof(&p)) {
        const Token *t = peek(&p);
        Decl *d = arena_new(arena, Decl);

        if (t->kind == TOK_KW_FUNC) {
            d->kind = DECL_FUNC;
            d->as.func = parse_func(&p, NULL);
            d->span = d->as.func->span;
        } else if (t->kind == TOK_KW_STRUCT) {
            d->kind = DECL_STRUCT;
            d->as.st = parse_struct(&p);
            d->span = d->as.st->span;
        } else if (t->kind == TOK_IDENT && (peek_at(&p, 1)->kind == TOK_DEFINE || peek_at(&p, 1)->kind == TOK_COLON)) {
            d->kind = DECL_GLOBAL;
            d->as.global = parse_global(&p);
            d->span = d->as.global->span;
        } else {
            error_span(&p, t->span, "expected a declaration (`func`, `struct`, or `name := value;`), found %s",
                       tok_kind_name(t->kind));
            sync_decl(&p);
            continue;
        }
        avec_push(p.arena, &m->decls, d);
        if (p.panicking) {
            sync_decl(&p);
        }
    }
    return m;
}
