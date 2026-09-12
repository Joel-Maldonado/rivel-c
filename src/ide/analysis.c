#include "ide/analysis.h"

#include <stdio.h>
#include <string.h>

#include "base/strbuf.h"

#include "lex/lexer.h"
#include "parse/parser.h"
#include "sema/sema.h"

/* Byte ranges stay in the compiler's coordinate system. The LSP adapter converts
 * them to the client's UTF-16 positions against the exact analyzed buffer. */
typedef struct Index {
    Program *program;
    StrBuf symbols;
    StrBuf refs;
    StrBuf expressions;
    StrBuf calls;
    StrBuf folds;
} Index;

static void json_string(StrBuf *out, Str value) {
    sb_putc(out, '"');
    for (size_t i = 0; i < value.n; i++) {
        unsigned char c = (unsigned char)value.p[i];
        if (c == '"' || c == '\\') {
            sb_putc(out, '\\');
            sb_putc(out, (char)c);
        } else if (c < 0x20) {
            sb_printf(out, "\\u%04x", c);
        } else {
            sb_putc(out, (char)c);
        }
    }
    sb_putc(out, '"');
}

static void item(StrBuf *out) {
    if (out->len != 0)
        sb_putc(out, ',');
}

static void range(StrBuf *out, Span span) {
    sb_printf(out, "[%u,%u]", span.lo, span.hi);
}

static const char *type_name(Type *type) {
    return type != NULL ? type->name : "?";
}

static void reference(Index *idx, Span use, Span declaration) {
    item(&idx->refs);
    sb_puts(&idx->refs, "{\"range\":");
    range(&idx->refs, use);
    sb_printf(&idx->refs, ",\"symbol\":%u}", declaration.lo);
}

static void symbol(Index *idx, Str name, Span selection, Span extent, Span scope, int kind, Type *type, int64_t owner,
                   FuncDecl *func, bool inferred, bool immutable) {
    if (name.n == 0)
        return;
    StrBuf *out = &idx->symbols;
    item(out);
    sb_printf(out, "{\"id\":%u,\"name\":", selection.lo);
    json_string(out, name);
    sb_printf(out, ",\"kind\":%d,\"owner\":%lld,\"range\":", kind, (long long)owner);
    range(out, extent);
    sb_puts(out, ",\"selection\":");
    range(out, selection);
    sb_puts(out, ",\"scope\":");
    range(out, scope);
    sb_puts(out, ",\"type\":");
    json_string(out, str_from(type_name(type)));
    sb_printf(out, ",\"inferred\":%s,\"immutable\":%s,\"parameters\":[", inferred ? "true" : "false",
              immutable ? "true" : "false");
    if (func != NULL) {
        for (size_t i = 0; i < func->params.len; i++) {
            Param *p = &func->params.data[i];
            if (i != 0)
                sb_putc(out, ',');
            sb_puts(out, "{\"name\":");
            json_string(out, p->name);
            sb_puts(out, ",\"type\":");
            json_string(out, str_from(type_name(p->sym != NULL ? p->sym->type : NULL)));
            sb_putc(out, '}');
        }
    }
    sb_puts(out, "]}");
}

static bool user_symbol(Symbol *sym) {
    return sym != NULL && sym->kind != SYM_BUILTIN && sym->kind != SYM_TYPE;
}

static Symbol *struct_named(Index *idx, Str name) {
    if (idx->program == NULL)
        return NULL;
    for (size_t i = 0; i < idx->program->structs.len; i++) {
        Symbol *s = idx->program->structs.data[i];
        if (str_eq(s->name, name))
            return s;
    }
    return NULL;
}

static void type_refs(Index *idx, TypeExpr *type) {
    if (type == NULL)
        return;
    if (type->kind == TYPEX_NAME) {
        Symbol *s = struct_named(idx, type->name);
        if (s != NULL)
            reference(idx, type->span, s->span);
    } else {
        type_refs(idx, type->inner);
    }
}

static void expression(Index *idx, Expr *e) {
    if (e == NULL)
        return;
    item(&idx->expressions);
    sb_puts(&idx->expressions, "{\"range\":");
    range(&idx->expressions, e->span);
    sb_puts(&idx->expressions, ",\"type\":");
    json_string(&idx->expressions, str_from(type_name(e->type)));
    sb_putc(&idx->expressions, '}');
    switch (e->kind) {
    case EXPR_NAME:
        if (user_symbol(e->as.name.sym))
            reference(idx, e->span, e->as.name.sym->span);
        break;
    case EXPR_FSTRING:
        for (size_t i = 0; i < e->as.fstring.parts.len; i++)
            expression(idx, e->as.fstring.parts.data[i]);
        break;
    case EXPR_LIST:
        for (size_t i = 0; i < e->as.list.items.len; i++)
            expression(idx, e->as.list.items.data[i]);
        break;
    case EXPR_UNARY:
        expression(idx, e->as.unary.operand);
        break;
    case EXPR_BINARY:
        expression(idx, e->as.binary.lhs);
        expression(idx, e->as.binary.rhs);
        break;
    case EXPR_CALL: {
        expression(idx, e->as.call.callee);
        Symbol *callee = e->as.call.callee_sym;
        for (size_t i = 0; i < e->as.call.args.len; i++) {
            Arg *arg = &e->as.call.args.data[i];
            expression(idx, arg->value);
            if (callee != NULL && callee->kind == SYM_STRUCT && arg->label.n != 0) {
                for (size_t j = 0; j < callee->fields.len; j++) {
                    FieldInfo *f = &callee->fields.data[j];
                    if (str_eq(f->name, arg->label))
                        reference(idx, arg->label_span, f->span);
                }
            }
        }
        item(&idx->calls);
        sb_puts(&idx->calls, "{\"range\":");
        range(&idx->calls, e->span);
        sb_puts(&idx->calls, ",\"callee\":");
        range(&idx->calls, e->as.call.callee->span);
        sb_printf(&idx->calls,
                  ",\"symbol\":%lld,\"builtin\":", user_symbol(callee) ? (long long)callee->span.lo : -1LL);
        json_string(&idx->calls, str_from(builtin_name((Builtin)e->as.call.builtin)));
        sb_putc(&idx->calls, '}');
        break;
    }
    case EXPR_FIELD: {
        Expr *base = e->as.field.base;
        expression(idx, base);
        if (base != NULL && base->type != NULL && base->type->kind == TY_STRUCT) {
            Symbol *st = base->type->st;
            FieldInfo *f = strmap_get(&st->members, e->as.field.name);
            Symbol *method = strmap_get(&st->methods, e->as.field.name);
            if (f != NULL)
                reference(idx, e->as.field.name_span, f->span);
            else if (method != NULL)
                reference(idx, e->as.field.name_span, method->span);
        }
        break;
    }
    case EXPR_INDEX:
        expression(idx, e->as.index.base);
        expression(idx, e->as.index.index);
        break;
    case EXPR_SLICE:
        expression(idx, e->as.slice.base);
        expression(idx, e->as.slice.lo);
        expression(idx, e->as.slice.hi);
        break;
    case EXPR_RANGE:
        expression(idx, e->as.range.lo);
        expression(idx, e->as.range.hi);
        break;
    default:
        break;
    }
}

static void block(Index *idx, Block *b, int64_t owner);

static void statement(Index *idx, Stmt *s, Span scope, int64_t owner) {
    if (s == NULL)
        return;
    switch (s->kind) {
    case STMT_VAR: {
        Symbol *sym = s->as.var.sym;
        symbol(idx, s->as.var.name, s->as.var.name_span, s->span, span_make(s->span.hi, scope.hi), 13,
               sym != NULL ? sym->type : NULL, owner, NULL, s->as.var.type == NULL, false);
        type_refs(idx, s->as.var.type);
        expression(idx, s->as.var.init);
        break;
    }
    case STMT_ASSIGN:
        expression(idx, s->as.assign.target);
        expression(idx, s->as.assign.value);
        break;
    case STMT_EXPR:
        expression(idx, s->as.expr.expr);
        break;
    case STMT_IF:
        expression(idx, s->as.if_stmt.cond);
        block(idx, s->as.if_stmt.then_block, owner);
        statement(idx, s->as.if_stmt.else_stmt, scope, owner);
        break;
    case STMT_WHILE:
        expression(idx, s->as.while_stmt.cond);
        block(idx, s->as.while_stmt.body, owner);
        break;
    case STMT_FOR: {
        Symbol *sym = s->as.for_stmt.sym;
        Block *body = s->as.for_stmt.body;
        symbol(idx, s->as.for_stmt.name, s->as.for_stmt.name_span, s->as.for_stmt.name_span,
               body != NULL ? body->span : scope, 13, sym != NULL ? sym->type : NULL, owner, NULL, true, true);
        expression(idx, s->as.for_stmt.iter);
        block(idx, body, owner);
        break;
    }
    case STMT_RETURN:
        expression(idx, s->as.ret.value);
        break;
    case STMT_BLOCK:
        block(idx, s->as.block, owner);
        break;
    default:
        break;
    }
}

static void block(Index *idx, Block *b, int64_t owner) {
    if (b == NULL)
        return;
    item(&idx->folds);
    range(&idx->folds, b->span);
    for (size_t i = 0; i < b->stmts.len; i++)
        statement(idx, b->stmts.data[i], b->span, owner);
}

static void function(Index *idx, FuncDecl *f, Span scope) {
    int64_t owner = f->owner != NULL ? (int64_t)f->owner->name_span.lo : -1;
    symbol(idx, f->name, f->name_span, f->span, scope, f->owner != NULL ? 6 : 12, f->sym != NULL ? f->sym->type : NULL,
           owner, f, false, false);
    type_refs(idx, f->ret);
    for (size_t i = 0; i < f->params.len; i++) {
        Param *p = &f->params.data[i];
        symbol(idx, p->name, p->span, p->span, f->body != NULL ? f->body->span : f->span, 13,
               p->sym != NULL ? p->sym->type : NULL, f->name_span.lo, NULL, false, str_eq_c(p->name, "self"));
        type_refs(idx, p->type);
    }
    block(idx, f->body, f->name_span.lo);
}

static void module_index(Index *idx, Module *m) {
    Span scope = span_make(0, (uint32_t)m->src->len);
    for (size_t i = 0; i < m->decls.len; i++) {
        Decl *d = m->decls.data[i];
        switch (d->kind) {
        case DECL_FUNC:
            function(idx, d->as.func, scope);
            break;
        case DECL_STRUCT: {
            StructDecl *s = d->as.st;
            symbol(idx, s->name, s->name_span, s->span, scope, 23, s->sym != NULL ? s->sym->type : NULL, -1, NULL,
                   false, false);
            item(&idx->folds);
            range(&idx->folds, s->span);
            for (size_t j = 0; j < s->fields.len; j++) {
                FieldDecl *f = &s->fields.data[j];
                FieldInfo *info = s->sym != NULL ? strmap_get(&s->sym->members, f->name) : NULL;
                symbol(idx, f->name, f->span, f->span, s->span, 8, info != NULL ? info->type : NULL, s->name_span.lo,
                       NULL, false, false);
                type_refs(idx, f->type);
            }
            for (size_t j = 0; j < s->methods.len; j++)
                function(idx, s->methods.data[j], s->span);
            break;
        }
        case DECL_GLOBAL: {
            GlobalDecl *g = d->as.global;
            symbol(idx, g->name, g->name_span, g->span, scope, 13, g->sym != NULL ? g->sym->type : NULL, -1, NULL,
                   g->type == NULL, false);
            type_refs(idx, g->type);
            expression(idx, g->init);
            break;
        }
        }
    }
}

int ide_analyze(const char *path) {
    char *err = NULL;
    Source *src;
    if (strcmp(path, "-") == 0) {
        StrBuf text = {0};
        char chunk[8192];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, stdin)) != 0)
            sb_putn(&text, chunk, n);
        if (ferror(stdin) || text.len > UINT32_MAX) {
            fputs("rivelc: could not read analysis input\n", stderr);
            sb_free(&text);
            return 2;
        }
        src = source_new("<stdin>", sb_cstr(&text), text.len);
        sb_free(&text);
    } else {
        src = source_from_file(path, &err);
        if (src == NULL) {
            fprintf(stderr, "rivelc: %s\n", err);
            free(err);
            return 2;
        }
    }
    Arena arena;
    Diags diags;
    TokenVec tokens = {0};
    Program program;
    arena_init(&arena, 0);
    diags_init(&diags);
    lex_source(src, &arena, &diags, &tokens);
    Module *m = parse_module(src, &tokens, &arena, &diags);
    /* The parser recovers with partial nodes. Keep valid declarations available
     * while the user is typing, even when another statement has a syntax error. */
    sema_check(m, &arena, &diags, &program);
    Index idx = {0};
    idx.program = &program;
    module_index(&idx, m);
    StrBuf out = {0};
    sb_puts(&out, "{\"schemaVersion\":1,\"diagnostics\":[");
    for (size_t i = 0; i < diags.items.len; i++) {
        Diag *d = &diags.items.data[i];
        if (i != 0)
            sb_putc(&out, ',');
        sb_puts(&out, "{\"range\":");
        range(&out, d->span);
        sb_printf(&out, ",\"severity\":%d,\"message\":", (int)d->level + 1);
        json_string(&out, str_from(d->msg));
        sb_putc(&out, '}');
    }
    sb_printf(
        &out, "],\"symbols\":[%s],\"references\":[%s],\"expressions\":[%s],\"calls\":[%s],\"folds\":[%s],\"tokens\":[",
        sb_cstr(&idx.symbols), sb_cstr(&idx.refs), sb_cstr(&idx.expressions), sb_cstr(&idx.calls), sb_cstr(&idx.folds));
    for (size_t i = 0; i < tokens.len; i++) {
        Token *t = &tokens.data[i];
        if (i != 0)
            sb_putc(&out, ',');
        sb_puts(&out, "{\"range\":");
        range(&out, t->span);
        sb_puts(&out, ",\"text\":");
        json_string(&out, t->text);
        sb_printf(&out, ",\"kind\":%d}", (int)t->kind);
    }
    sb_puts(&out, "]}\n");
    fputs(sb_cstr(&out), stdout);
    sb_free(&out);
    sb_free(&idx.symbols);
    sb_free(&idx.refs);
    sb_free(&idx.expressions);
    sb_free(&idx.calls);
    sb_free(&idx.folds);
    program_free(&program);
    vec_free(&tokens);
    diags_free(&diags);
    arena_free(&arena);
    source_free(src);
    return 0;
}
