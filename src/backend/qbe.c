#include "backend/qbe.h"

#include "../../runtime/rivel_rt.h"

/*
 * IR to QBE IL. The mapping is almost one to one; the interesting parts are
 * the static data (string literals, type descriptors, panic locations,
 * globals) whose layouts must match runtime/rivel_rt.h byte for byte, and
 * the C-ABI `main` that boots the runtime.
 */

static const char *ty(IrType t) {
    switch (t) {
    case IR_W:
        return "w";
    case IR_L:
        return "l";
    case IR_D:
        return "d";
    }
    return "l";
}

static void value(FILE *out, IrValue v) {
    switch (v.kind) {
    case IRV_NONE:
        fputs("0", out);
        return;
    case IRV_TEMP:
        fprintf(out, "%%t%u", v.as.temp);
        return;
    case IRV_INT:
        fprintf(out, "%lld", (long long)v.as.i);
        return;
    case IRV_FLOAT:
        fprintf(out, "d_%.17g", v.as.f);
        return;
    case IRV_SYMBOL:
        fprintf(out, "$%s", v.as.sym);
        return;
    }
}

/* Emits bytes as a data string item: printable ASCII verbatim, everything else as octal. */
static void data_string(FILE *out, const char *bytes, size_t n) {
    fputs("b \"", out);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == '"' || c == '\\' || c < 32 || c >= 127) {
            fprintf(out, "\\%03o", c);
        } else {
            fputc(c, out);
        }
    }
    fputs("\", b 0", out);
}

static void emit_data(const IrModule *m, FILE *out) {
    for (size_t i = 0; i < m->strings.len; i++) {
        const IrStrLit *s = &m->strings.data[i];
        fprintf(out, "data $%s = align 8 { l $rv_ti_str, w %u, w 0, l %zu, ", s->sym, RV_F_STATIC, s->bytes.n);
        data_string(out, s->bytes.p, s->bytes.n);
        fputs(" }\n", out);
    }
    for (size_t i = 0; i < m->typeinfos.len; i++) {
        const IrTypeInfo *ti = &m->typeinfos.data[i];
        fprintf(out, "data $%s.name = { ", ti->sym);
        data_string(out, ti->name.p, ti->name.n);
        fputs(" }\n", out);
        fprintf(out, "data $%s.fields = { b", ti->sym);
        for (size_t j = 0; j < ti->field_kinds.len; j++) {
            fprintf(out, " %u", ti->field_kinds.data[j]);
        }
        if (ti->field_kinds.len == 0) {
            fputs(" 0", out);
        }
        fputs(" }\n", out);
        fprintf(out, "data $%s = align 8 { l $%s.name, l %d, l %zu, l $%s.fields }\n", ti->sym, ti->sym, RV_KIND_STRUCT,
                ti->field_kinds.len, ti->sym);
    }
    if (m->locs.len > 0) {
        fputs("data $rv_file = { ", out);
        data_string(out, m->source_name, strlen(m->source_name));
        fputs(" }\n", out);
    }
    for (size_t i = 0; i < m->locs.len; i++) {
        const IrLoc *loc = &m->locs.data[i];
        fprintf(out, "data $%s = align 8 { l $rv_file, l %d, l %d }\n", loc->sym, loc->line, loc->col);
    }
    for (size_t i = 0; i < m->globals.len; i++) {
        const IrGlobal *g = &m->globals.data[i];
        switch (g->kind) {
        case IRG_INT:
            fprintf(out, "data $%s = align 8 { l %lld }\n", g->sym, (long long)g->init_int);
            break;
        case IRG_FLOAT:
            fprintf(out, "data $%s = align 8 { d d_%.17g }\n", g->sym, g->init_float);
            break;
        case IRG_PTR:
            if (g->init_sym != NULL) {
                fprintf(out, "data $%s = align 8 { l $%s }\n", g->sym, g->init_sym);
            } else {
                fprintf(out, "data $%s = align 8 { l 0 }\n", g->sym);
            }
            break;
        }
    }
    {
        size_t nroots = 0;
        for (size_t i = 0; i < m->globals.len; i++) {
            if (m->globals.data[i].kind == IRG_PTR) {
                nroots++;
            }
        }
        fputs("data $rv_roots = align 8 {", out);
        for (size_t i = 0; i < m->globals.len; i++) {
            if (m->globals.data[i].kind == IRG_PTR) {
                fprintf(out, " l $%s", m->globals.data[i].sym);
            }
        }
        if (nroots == 0) {
            fputs(" l 0", out);
        }
        fputs(" }\n", out);
        fprintf(out, "data $rv_roots_count = align 8 { l %zu }\n", nroots);
    }
    fputc('\n', out);
}

static const char *cmp_name(IrOp op, IrType t) {
    static char buf[8];
    const char *base;
    switch (op) {
    case IR_CEQ:
        base = "eq";
        break;
    case IR_CNE:
        base = "ne";
        break;
    case IR_CLT:
        base = t == IR_D ? "lt" : "slt";
        break;
    case IR_CLE:
        base = t == IR_D ? "le" : "sle";
        break;
    case IR_CGT:
        base = t == IR_D ? "gt" : "sgt";
        break;
    default:
        base = t == IR_D ? "ge" : "sge";
        break;
    }
    snprintf(buf, sizeof buf, "c%s%s", base, ty(t));
    return buf;
}

static void emit_instr(FILE *out, const IrInstr *in) {
    static const char *const BINOPS[] = {
        [IR_ADD] = "add", [IR_SUB] = "sub", [IR_MUL] = "mul", [IR_DIV] = "div", [IR_REM] = "rem",
        [IR_AND] = "and", [IR_OR] = "or",   [IR_XOR] = "xor", [IR_SHL] = "shl", [IR_SAR] = "sar",
    };
    static const char *const UNOPS[] = {
        [IR_COPY] = "copy",   [IR_NEG] = "neg",     [IR_EXTSW] = "extsw", [IR_EXTUW] = "extuw",
        [IR_SLTOF] = "sltof", [IR_DTOSI] = "dtosi", [IR_CAST] = "cast",
    };

    fputs("\t", out);
    switch (in->op) {
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_DIV:
    case IR_REM:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_SHL:
    case IR_SAR:
        fprintf(out, "%%t%u =%s %s ", in->dst, ty(in->type), BINOPS[in->op]);
        value(out, in->a);
        fputs(", ", out);
        value(out, in->b);
        break;
    case IR_CEQ:
    case IR_CNE:
    case IR_CLT:
    case IR_CLE:
    case IR_CGT:
    case IR_CGE:
        fprintf(out, "%%t%u =w %s ", in->dst, cmp_name(in->op, in->type));
        value(out, in->a);
        fputs(", ", out);
        value(out, in->b);
        break;
    case IR_COPY:
    case IR_NEG:
    case IR_EXTSW:
    case IR_EXTUW:
    case IR_SLTOF:
    case IR_DTOSI:
    case IR_CAST:
        fprintf(out, "%%t%u =%s %s ", in->dst, ty(in->type), UNOPS[in->op]);
        value(out, in->a);
        break;
    case IR_LOAD:
        if (in->offset != 0) {
            fprintf(out, "%%a%u =l add ", in->dst);
            value(out, in->a);
            fprintf(out, ", %d\n\t%%t%u =%s load%s %%a%u", in->offset, in->dst, ty(in->type), ty(in->type), in->dst);
        } else {
            fprintf(out, "%%t%u =%s load%s ", in->dst, ty(in->type), ty(in->type));
            value(out, in->a);
        }
        break;
    case IR_STORE:
        if (in->offset != 0) {
            fputs("%addr =l add ", out);
            value(out, in->b);
            fprintf(out, ", %d\n\tstore%s ", in->offset, ty(in->type));
            value(out, in->a);
            fputs(", %addr", out);
        } else {
            fprintf(out, "store%s ", ty(in->type));
            value(out, in->a);
            fputs(", ", out);
            value(out, in->b);
        }
        break;
    case IR_LOAD_SLOT:
        fprintf(out, "%%t%u =%s load%s %%s%u", in->dst, ty(in->type), ty(in->type), in->slot);
        break;
    case IR_STORE_SLOT:
        fprintf(out, "store%s ", ty(in->type));
        value(out, in->a);
        fprintf(out, ", %%s%u", in->slot);
        break;
    case IR_SLOT_ADDR:
        fprintf(out, "%%t%u =l copy %%s%u", in->dst, in->slot);
        break;
    case IR_CALL:
        if (in->has_dst) {
            fprintf(out, "%%t%u =%s ", in->dst, ty(in->type));
        }
        fprintf(out, "call $%s(", in->callee);
        for (size_t i = 0; i < in->args.len; i++) {
            if (i > 0) {
                fputs(", ", out);
            }
            fprintf(out, "%s ", ty(in->args.data[i].type));
            value(out, in->args.data[i]);
        }
        fputc(')', out);
        break;
    case IR_JMP:
        fprintf(out, "jmp @b%u", in->target);
        break;
    case IR_BR:
        fputs("jnz ", out);
        value(out, in->a);
        fprintf(out, ", @b%u, @b%u", in->target, in->target2);
        break;
    case IR_RET:
        fputs("ret", out);
        if (in->a.kind != IRV_NONE) {
            fputc(' ', out);
            value(out, in->a);
        }
        break;
    case IR_HLT:
        fputs("hlt", out);
        break;
    }
    fputc('\n', out);
}

static void emit_func(const IrFunc *f, FILE *out) {
    fprintf(out, "%sfunction %s%s$%s(", f->exported ? "export " : "", f->has_ret ? ty(f->ret) : "",
            f->has_ret ? " " : "", f->name);
    for (size_t i = 0; i < f->params.len; i++) {
        fprintf(out, "%s%s %%p%zu", i ? ", " : "", ty(f->params.data[i]), i);
    }
    fputs(") {\n@start\n", out);
    for (uint32_t i = 0; i < f->nslots; i++) {
        fprintf(out, "\t%%s%u =l alloc8 8\n", i);
    }
    for (size_t i = 0; i < f->params.len; i++) {
        fprintf(out, "\tstore%s %%p%zu, %%s%zu\n", ty(f->params.data[i]), i, i);
    }
    for (size_t i = 0; i < f->blocks.len; i++) {
        const IrBlock *b = f->blocks.data[i];
        fprintf(out, "@b%u\n", b->id);
        for (size_t j = 0; j < b->instrs.len; j++) {
            emit_instr(out, &b->instrs.data[j]);
        }
    }
    fputs("}\n\n", out);
}

static void emit_main(const IrModule *m, FILE *out) {
    fputs("export function w $main(w %argc, l %argv) {\n@start\n", out);
    fputs("\t%bottom =l alloc8 8\n", out);
    fputs("\tcall $rv_init(w %argc, l %argv, l %bottom)\n", out);
    fputs("\t%nroots =l loadl $rv_roots_count\n", out);
    fputs("\tcall $rv_add_roots(l $rv_roots, l %nroots)\n", out);
    if (m->main_returns_int) {
        fprintf(out, "\t%%code =l call $%s()\n", m->main_sym);
        fputs("\tcall $rv_flush()\n", out);
        fputs("\t%status =w copy %code\n", out);
        fputs("\t%status =w and %status, 255\n", out);
        fputs("\tret %status\n", out);
    } else {
        fprintf(out, "\tcall $%s()\n", m->main_sym);
        fputs("\tcall $rv_flush()\n", out);
        fputs("\tret 0\n", out);
    }
    fputs("}\n", out);
}

void qbe_emit(const IrModule *m, FILE *out) {
    emit_data(m, out);
    for (size_t i = 0; i < m->funcs.len; i++) {
        emit_func(m->funcs.data[i], out);
    }
    if (m->main_sym != NULL) {
        emit_main(m, out);
    }
}
