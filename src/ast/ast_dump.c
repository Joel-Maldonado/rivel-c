#include <stdio.h>

#include "ast/ast.h"

/*
 * S-expression dump. The format is stable because tests compare against it:
 * one node per line, children indented by two spaces, literals escaped.
 */

static void indent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) {
        fputs("  ", out);
    }
}

static void dump_str(FILE *out, Str s) {
    fputc('"', out);
    for (size_t i = 0; i < s.n; i++) {
        unsigned char c = (unsigned char)s.p[i];
        if (c == '"' || c == '\\') {
            fprintf(out, "\\%c", c);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c >= 32 && c < 127) {
            fputc(c, out);
        } else {
            fprintf(out, "\\x%02X", c);
        }
    }
    fputc('"', out);
}

static void dump_type(FILE *out, const TypeExpr *t) {
    if (t == NULL) {
        fputs("void", out);
        return;
    }
    switch (t->kind) {
    case TYPEX_NAME:
        fprintf(out, STR_FMT, STR_ARG(t->name));
        break;
    case TYPEX_LIST:
        fputs("list[", out);
        dump_type(out, t->inner);
        fputc(']', out);
        break;
    case TYPEX_OPTIONAL:
        dump_type(out, t->inner);
        fputc('?', out);
        break;
    }
}

static const char *op_text(TokKind op) {
    switch (op) {
    case TOK_PLUS:
        return "+";
    case TOK_MINUS:
        return "-";
    case TOK_STAR:
        return "*";
    case TOK_SLASH:
        return "/";
    case TOK_PERCENT:
        return "%";
    case TOK_AMP:
        return "&";
    case TOK_PIPE:
        return "|";
    case TOK_CARET:
        return "^";
    case TOK_TILDE:
        return "~";
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
    case TOK_QQ:
        return "??";
    case TOK_ASSIGN:
        return "=";
    case TOK_PLUS_ASSIGN:
        return "+=";
    case TOK_MINUS_ASSIGN:
        return "-=";
    case TOK_STAR_ASSIGN:
        return "*=";
    case TOK_SLASH_ASSIGN:
        return "/=";
    case TOK_PERCENT_ASSIGN:
        return "%=";
    default:
        return "?";
    }
}

static void dump_expr(FILE *out, const Expr *e, int depth);

static void dump_expr_list(FILE *out, const ExprVec *items, int depth) {
    for (size_t i = 0; i < items->len; i++) {
        fputc('\n', out);
        dump_expr(out, items->data[i], depth);
    }
}

static void dump_expr(FILE *out, const Expr *e, int depth) {
    indent(out, depth);
    if (e == NULL) {
        fputs("(missing)", out);
        return;
    }
    switch (e->kind) {
    case EXPR_INT:
        fprintf(out, "(int %lld)", (long long)e->as.int_val);
        return;
    case EXPR_FLOAT:
        fprintf(out, "(float %.17g)", e->as.float_val);
        return;
    case EXPR_BOOL:
        fprintf(out, "(bool %s)", e->as.bool_val ? "true" : "false");
        return;
    case EXPR_STRING:
        fputs("(str ", out);
        dump_str(out, e->as.str_val);
        fputc(')', out);
        return;
    case EXPR_NULL:
        fputs("(null)", out);
        return;
    case EXPR_NAME:
        fprintf(out, "(name " STR_FMT ")", STR_ARG(e->as.name.name));
        return;
    case EXPR_FSTRING:
        fputs("(fstring", out);
        dump_expr_list(out, &e->as.fstring.parts, depth + 1);
        fputc(')', out);
        return;
    case EXPR_LIST:
        fputs("(list", out);
        dump_expr_list(out, &e->as.list.items, depth + 1);
        fputc(')', out);
        return;
    case EXPR_UNARY:
        fprintf(out, "(unary %s\n", op_text(e->as.unary.op));
        dump_expr(out, e->as.unary.operand, depth + 1);
        fputc(')', out);
        return;
    case EXPR_BINARY:
        fprintf(out, "(binary %s\n", op_text(e->as.binary.op));
        dump_expr(out, e->as.binary.lhs, depth + 1);
        fputc('\n', out);
        dump_expr(out, e->as.binary.rhs, depth + 1);
        fputc(')', out);
        return;
    case EXPR_CALL:
        fputs("(call\n", out);
        dump_expr(out, e->as.call.callee, depth + 1);
        for (size_t i = 0; i < e->as.call.args.len; i++) {
            const Arg *a = &e->as.call.args.data[i];
            fputc('\n', out);
            if (a->label.n != 0) {
                indent(out, depth + 1);
                fprintf(out, "(arg " STR_FMT "\n", STR_ARG(a->label));
                dump_expr(out, a->value, depth + 2);
                fputc(')', out);
            } else {
                dump_expr(out, a->value, depth + 1);
            }
        }
        fputc(')', out);
        return;
    case EXPR_FIELD:
        fprintf(out, "(field " STR_FMT "\n", STR_ARG(e->as.field.name));
        dump_expr(out, e->as.field.base, depth + 1);
        fputc(')', out);
        return;
    case EXPR_INDEX:
        fputs("(index\n", out);
        dump_expr(out, e->as.index.base, depth + 1);
        fputc('\n', out);
        dump_expr(out, e->as.index.index, depth + 1);
        fputc(')', out);
        return;
    case EXPR_SLICE:
        fputs("(slice\n", out);
        dump_expr(out, e->as.slice.base, depth + 1);
        fputc('\n', out);
        dump_expr(out, e->as.slice.lo, depth + 1);
        fputc('\n', out);
        dump_expr(out, e->as.slice.hi, depth + 1);
        fputc(')', out);
        return;
    case EXPR_RANGE:
        fprintf(out, "(range %s\n", e->as.range.inclusive ? "inclusive" : "exclusive");
        dump_expr(out, e->as.range.lo, depth + 1);
        fputc('\n', out);
        dump_expr(out, e->as.range.hi, depth + 1);
        fputc(')', out);
        return;
    }
}

static void dump_block(FILE *out, const Block *b, int depth);

static void dump_stmt(FILE *out, const Stmt *s, int depth) {
    if (s->kind == STMT_BLOCK) {
        dump_block(out, s->as.block, depth);
        return;
    }
    indent(out, depth);
    switch (s->kind) {
    case STMT_VAR:
        fprintf(out, "(var " STR_FMT " ", STR_ARG(s->as.var.name));
        if (s->as.var.type != NULL) {
            dump_type(out, s->as.var.type);
        } else {
            fputs("infer", out);
        }
        fputc('\n', out);
        dump_expr(out, s->as.var.init, depth + 1);
        fputc(')', out);
        return;
    case STMT_ASSIGN:
        fprintf(out, "(assign %s\n", op_text(s->as.assign.op));
        dump_expr(out, s->as.assign.target, depth + 1);
        fputc('\n', out);
        dump_expr(out, s->as.assign.value, depth + 1);
        fputc(')', out);
        return;
    case STMT_EXPR:
        fputs("(expr\n", out);
        dump_expr(out, s->as.expr.expr, depth + 1);
        fputc(')', out);
        return;
    case STMT_IF:
        fputs("(if\n", out);
        dump_expr(out, s->as.if_stmt.cond, depth + 1);
        fputc('\n', out);
        dump_block(out, s->as.if_stmt.then_block, depth + 1);
        if (s->as.if_stmt.else_stmt != NULL) {
            fputc('\n', out);
            dump_stmt(out, s->as.if_stmt.else_stmt, depth + 1);
        }
        fputc(')', out);
        return;
    case STMT_WHILE:
        fputs("(while\n", out);
        dump_expr(out, s->as.while_stmt.cond, depth + 1);
        fputc('\n', out);
        dump_block(out, s->as.while_stmt.body, depth + 1);
        fputc(')', out);
        return;
    case STMT_FOR:
        fprintf(out, "(for " STR_FMT "\n", STR_ARG(s->as.for_stmt.name));
        dump_expr(out, s->as.for_stmt.iter, depth + 1);
        fputc('\n', out);
        dump_block(out, s->as.for_stmt.body, depth + 1);
        fputc(')', out);
        return;
    case STMT_BREAK:
        fputs("(break)", out);
        return;
    case STMT_CONTINUE:
        fputs("(continue)", out);
        return;
    case STMT_RETURN:
        if (s->as.ret.value == NULL) {
            fputs("(return)", out);
        } else {
            fputs("(return\n", out);
            dump_expr(out, s->as.ret.value, depth + 1);
            fputc(')', out);
        }
        return;
    case STMT_BLOCK:
        return;
    }
}

static void dump_block(FILE *out, const Block *b, int depth) {
    indent(out, depth);
    fputs("(block", out);
    for (size_t i = 0; i < b->stmts.len; i++) {
        fputc('\n', out);
        dump_stmt(out, b->stmts.data[i], depth + 1);
    }
    fputc(')', out);
}

static void dump_func(FILE *out, const FuncDecl *f, int depth) {
    indent(out, depth);
    fprintf(out, "(%s " STR_FMT " (params", f->owner ? "method" : "func", STR_ARG(f->name));
    for (size_t i = 0; i < f->params.len; i++) {
        const Param *param = &f->params.data[i];
        fprintf(out, " (" STR_FMT " ", STR_ARG(param->name));
        if (param->type != NULL) {
            dump_type(out, param->type);
        } else {
            fputs("self", out);
        }
        fputc(')', out);
    }
    fputs(") -> ", out);
    dump_type(out, f->ret);
    fputc('\n', out);
    dump_block(out, f->body, depth + 1);
    fputc(')', out);
}

void ast_dump(const Module *m, FILE *out) {
    fputs("(module", out);
    for (size_t i = 0; i < m->decls.len; i++) {
        const Decl *d = m->decls.data[i];
        fputc('\n', out);
        switch (d->kind) {
        case DECL_FUNC:
            dump_func(out, d->as.func, 1);
            break;
        case DECL_STRUCT: {
            const StructDecl *st = d->as.st;
            indent(out, 1);
            fprintf(out, "(struct " STR_FMT, STR_ARG(st->name));
            for (size_t j = 0; j < st->fields.len; j++) {
                fputc('\n', out);
                indent(out, 2);
                fprintf(out, "(field " STR_FMT " ", STR_ARG(st->fields.data[j].name));
                dump_type(out, st->fields.data[j].type);
                fputc(')', out);
            }
            for (size_t j = 0; j < st->methods.len; j++) {
                fputc('\n', out);
                dump_func(out, st->methods.data[j], 2);
            }
            fputc(')', out);
            break;
        }
        case DECL_GLOBAL: {
            const GlobalDecl *g = d->as.global;
            indent(out, 1);
            fprintf(out, "(global " STR_FMT " ", STR_ARG(g->name));
            if (g->type != NULL) {
                dump_type(out, g->type);
            } else {
                fputs("infer", out);
            }
            fputc('\n', out);
            dump_expr(out, g->init, 2);
            fputc(')', out);
            break;
        }
        }
    }
    fputs(")\n", out);
}
