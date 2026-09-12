#include "ir/ir.h"

#include "base/strmap.h"

void ir_module_init(IrModule *m, Arena *arena, const char *source_name) {
    StrMap *map = xmalloc(sizeof *map);
    memset(m, 0, sizeof *m);
    m->arena = arena;
    m->source_name = source_name;
    strmap_init(map);
    m->string_map = map;
}

void ir_module_free(IrModule *m) {
    for (size_t i = 0; i < m->funcs.len; i++) {
        IrFunc *f = m->funcs.data[i];
        for (size_t j = 0; j < f->blocks.len; j++) {
            IrBlock *b = f->blocks.data[j];
            for (size_t k = 0; k < b->instrs.len; k++) {
                vec_free(&b->instrs.data[k].args);
            }
            vec_free(&b->instrs);
        }
        vec_free(&f->blocks);
        vec_free(&f->params);
    }
    for (size_t i = 0; i < m->typeinfos.len; i++) {
        vec_free(&m->typeinfos.data[i].field_kinds);
    }
    vec_free(&m->funcs);
    vec_free(&m->strings);
    vec_free(&m->typeinfos);
    vec_free(&m->locs);
    vec_free(&m->globals);
    strmap_free(m->string_map);
    free(m->string_map);
}

IrFunc *ir_func_new(IrModule *m, const char *name) {
    IrFunc *f = arena_new(m->arena, IrFunc);
    f->arena = m->arena;
    f->name = name;
    vec_push(&m->funcs, f);
    return f;
}

IrBlock *ir_block_new(IrFunc *f) {
    IrBlock *b = arena_new(f->arena, IrBlock);
    b->id = (uint32_t)f->blocks.len;
    vec_push(&f->blocks, b);
    return b;
}

uint32_t ir_temp(IrFunc *f) {
    return f->ntemps++;
}

uint32_t ir_slot(IrFunc *f) {
    return f->nslots++;
}

const char *ir_string_literal(IrModule *m, Str bytes) {
    StrMap *map = m->string_map;
    const char *sym = strmap_get(map, bytes);
    IrStrLit lit;

    if (sym != NULL) {
        return sym;
    }
    lit.sym = arena_printf(m->arena, "rv_s%zu", m->strings.len);
    lit.bytes = bytes;
    vec_push(&m->strings, lit);
    strmap_put(map, bytes, (void *)lit.sym);
    return lit.sym;
}

const char *ir_loc(IrModule *m, int line, int col) {
    IrLoc loc;
    for (size_t i = 0; i < m->locs.len; i++) {
        if (m->locs.data[i].line == line && m->locs.data[i].col == col) {
            return m->locs.data[i].sym;
        }
    }
    loc.sym = arena_printf(m->arena, "rv_loc%zu", m->locs.len);
    loc.line = line;
    loc.col = col;
    vec_push(&m->locs, loc);
    return loc.sym;
}

/* ---- emitters ------------------------------------------------------------------ */

static IrInstr *push(IrBlock *b, IrOp op, IrType t) {
    IrInstr in;
    memset(&in, 0, sizeof in);
    in.op = op;
    in.type = t;
    vec_push(&b->instrs, in);
    return &vec_last(&b->instrs);
}

static IrValue with_dst(IrFunc *f, IrInstr *in, IrType t) {
    in->has_dst = true;
    in->dst = ir_temp(f);
    return ir_tmp(t, in->dst);
}

IrValue ir_emit_binary(IrFunc *f, IrBlock *b, IrOp op, IrType t, IrValue a, IrValue c) {
    IrInstr *in = push(b, op, t);
    in->a = a;
    in->b = c;
    return with_dst(f, in, t);
}

IrValue ir_emit_cmp(IrFunc *f, IrBlock *b, IrOp op, IrType operand_type, IrValue a, IrValue c) {
    IrInstr *in = push(b, op, operand_type);
    in->a = a;
    in->b = c;
    return with_dst(f, in, IR_W);
}

IrValue ir_emit_unary(IrFunc *f, IrBlock *b, IrOp op, IrType t, IrValue a) {
    IrInstr *in = push(b, op, t);
    in->a = a;
    return with_dst(f, in, t);
}

IrValue ir_emit_load(IrFunc *f, IrBlock *b, IrType t, IrValue addr, int32_t offset) {
    IrInstr *in = push(b, IR_LOAD, t);
    in->a = addr;
    in->offset = offset;
    return with_dst(f, in, t);
}

void ir_emit_store(IrBlock *b, IrType t, IrValue value, IrValue addr, int32_t offset) {
    IrInstr *in = push(b, IR_STORE, t);
    in->a = value;
    in->b = addr;
    in->offset = offset;
}

IrValue ir_emit_load_slot(IrFunc *f, IrBlock *b, IrType t, uint32_t slot) {
    IrInstr *in = push(b, IR_LOAD_SLOT, t);
    in->slot = slot;
    return with_dst(f, in, t);
}

void ir_emit_store_slot(IrBlock *b, IrType t, IrValue value, uint32_t slot) {
    IrInstr *in = push(b, IR_STORE_SLOT, t);
    in->a = value;
    in->slot = slot;
}

IrValue ir_emit_slot_addr(IrFunc *f, IrBlock *b, uint32_t slot) {
    IrInstr *in = push(b, IR_SLOT_ADDR, IR_L);
    in->slot = slot;
    return with_dst(f, in, IR_L);
}

IrValue ir_emit_call(IrFunc *f, IrBlock *b, const char *callee, bool has_ret, IrType ret, const IrValue *args,
                     size_t nargs) {
    IrInstr *in = push(b, IR_CALL, ret);
    in->callee = callee;
    for (size_t i = 0; i < nargs; i++) {
        vec_push(&in->args, args[i]);
    }
    if (!has_ret) {
        return ir_none();
    }
    return with_dst(f, in, ret);
}

void ir_emit_jmp(IrBlock *b, IrBlock *target) {
    IrInstr *in = push(b, IR_JMP, IR_W);
    in->target = target->id;
}

void ir_emit_br(IrBlock *b, IrValue cond, IrBlock *then_block, IrBlock *else_block) {
    IrInstr *in = push(b, IR_BR, IR_W);
    in->a = cond;
    in->target = then_block->id;
    in->target2 = else_block->id;
}

void ir_emit_ret(IrBlock *b, IrValue value) {
    IrInstr *in = push(b, IR_RET, value.type);
    in->a = value;
}

void ir_emit_ret_void(IrBlock *b) {
    push(b, IR_RET, IR_W);
}

void ir_emit_hlt(IrBlock *b) {
    push(b, IR_HLT, IR_W);
}

bool ir_block_terminated(const IrBlock *b) {
    IrOp op;
    if (b->instrs.len == 0) {
        return false;
    }
    op = vec_last(&b->instrs).op;
    return op == IR_JMP || op == IR_BR || op == IR_RET || op == IR_HLT;
}

/* ---- dump ------------------------------------------------------------------------ */

static const char *type_name(IrType t) {
    switch (t) {
    case IR_W:
        return "w";
    case IR_L:
        return "l";
    case IR_D:
        return "d";
    }
    return "?";
}

static void dump_value(FILE *out, IrValue v) {
    switch (v.kind) {
    case IRV_NONE:
        fputs("<none>", out);
        return;
    case IRV_TEMP:
        fprintf(out, "%%t%u", v.as.temp);
        return;
    case IRV_INT:
        fprintf(out, "%lld", (long long)v.as.i);
        return;
    case IRV_FLOAT:
        fprintf(out, "%.17g", v.as.f);
        return;
    case IRV_SYMBOL:
        fprintf(out, "$%s", v.as.sym);
        return;
    }
}

static const char *op_name(IrOp op) {
    static const char *const NAMES[] = {
        [IR_ADD] = "add",
        [IR_SUB] = "sub",
        [IR_MUL] = "mul",
        [IR_DIV] = "div",
        [IR_REM] = "rem",
        [IR_AND] = "and",
        [IR_OR] = "or",
        [IR_XOR] = "xor",
        [IR_SHL] = "shl",
        [IR_SAR] = "sar",
        [IR_CEQ] = "ceq",
        [IR_CNE] = "cne",
        [IR_CLT] = "clt",
        [IR_CLE] = "cle",
        [IR_CGT] = "cgt",
        [IR_CGE] = "cge",
        [IR_COPY] = "copy",
        [IR_NEG] = "neg",
        [IR_EXTSW] = "extsw",
        [IR_EXTUW] = "extuw",
        [IR_SLTOF] = "sltof",
        [IR_DTOSI] = "dtosi",
        [IR_CAST] = "cast",
        [IR_LOAD] = "load",
        [IR_STORE] = "store",
        [IR_LOAD_SLOT] = "loadslot",
        [IR_STORE_SLOT] = "storeslot",
        [IR_SLOT_ADDR] = "slotaddr",
        [IR_CALL] = "call",
        [IR_JMP] = "jmp",
        [IR_BR] = "br",
        [IR_RET] = "ret",
        [IR_HLT] = "hlt",
    };
    return NAMES[op];
}

static void dump_instr(FILE *out, const IrInstr *in) {
    fputs("    ", out);
    if (in->has_dst) {
        fprintf(out, "%%t%u =%s ", in->dst, type_name(in->type));
    }
    switch (in->op) {
    case IR_LOAD:
        fprintf(out, "load%s ", type_name(in->type));
        dump_value(out, in->a);
        fprintf(out, " + %d\n", in->offset);
        return;
    case IR_STORE:
        fprintf(out, "store%s ", type_name(in->type));
        dump_value(out, in->a);
        fputs(" -> ", out);
        dump_value(out, in->b);
        fprintf(out, " + %d\n", in->offset);
        return;
    case IR_LOAD_SLOT:
        fprintf(out, "loadslot%s s%u\n", type_name(in->type), in->slot);
        return;
    case IR_STORE_SLOT:
        fprintf(out, "storeslot%s ", type_name(in->type));
        dump_value(out, in->a);
        fprintf(out, " -> s%u\n", in->slot);
        return;
    case IR_SLOT_ADDR:
        fprintf(out, "slotaddr s%u\n", in->slot);
        return;
    case IR_CALL:
        if (!in->has_dst) {
            fputs("call ", out);
        }
        fprintf(out, "$%s(", in->callee);
        for (size_t i = 0; i < in->args.len; i++) {
            if (i > 0) {
                fputs(", ", out);
            }
            fprintf(out, "%s ", type_name(in->args.data[i].type));
            dump_value(out, in->args.data[i]);
        }
        fputs(")\n", out);
        return;
    case IR_JMP:
        fprintf(out, "jmp @b%u\n", in->target);
        return;
    case IR_BR:
        fputs("br ", out);
        dump_value(out, in->a);
        fprintf(out, ", @b%u, @b%u\n", in->target, in->target2);
        return;
    case IR_RET:
        fputs("ret", out);
        if (in->a.kind != IRV_NONE) {
            fputc(' ', out);
            dump_value(out, in->a);
        }
        fputc('\n', out);
        return;
    case IR_HLT:
        fputs("hlt\n", out);
        return;
    default:
        fprintf(out, "%s%s ", op_name(in->op), in->op >= IR_CEQ && in->op <= IR_CGE ? type_name(in->type) : "");
        dump_value(out, in->a);
        if (in->b.kind != IRV_NONE) {
            fputs(", ", out);
            dump_value(out, in->b);
        }
        fputc('\n', out);
        return;
    }
}

void ir_dump(const IrModule *m, FILE *out) {
    for (size_t i = 0; i < m->strings.len; i++) {
        const IrStrLit *s = &m->strings.data[i];
        fprintf(out, "string $%s = \"", s->sym);
        for (size_t j = 0; j < s->bytes.n; j++) {
            unsigned char c = (unsigned char)s->bytes.p[j];
            if (c == '"' || c == '\\') {
                fprintf(out, "\\%c", c);
            } else if (c >= 32 && c < 127) {
                fputc(c, out);
            } else {
                fprintf(out, "\\x%02X", c);
            }
        }
        fputs("\"\n", out);
    }
    for (size_t i = 0; i < m->typeinfos.len; i++) {
        const IrTypeInfo *ti = &m->typeinfos.data[i];
        fprintf(out, "typeinfo $%s " STR_FMT " {", ti->sym, STR_ARG(ti->name));
        for (size_t j = 0; j < ti->field_kinds.len; j++) {
            fprintf(out, "%s%d", j ? ", " : " ", ti->field_kinds.data[j]);
        }
        fputs(" }\n", out);
    }
    for (size_t i = 0; i < m->globals.len; i++) {
        const IrGlobal *g = &m->globals.data[i];
        fprintf(out, "global $%s = ", g->sym);
        switch (g->kind) {
        case IRG_INT:
            fprintf(out, "%lld\n", (long long)g->init_int);
            break;
        case IRG_FLOAT:
            fprintf(out, "%.17g\n", g->init_float);
            break;
        case IRG_PTR:
            if (g->init_sym != NULL) {
                fprintf(out, "$%s\n", g->init_sym);
            } else {
                fputs("null\n", out);
            }
            break;
        }
    }
    for (size_t i = 0; i < m->funcs.len; i++) {
        const IrFunc *f = m->funcs.data[i];
        fprintf(out, "\nfunction %s$%s(", f->has_ret ? type_name(f->ret) : "", f->name);
        for (size_t j = 0; j < f->params.len; j++) {
            fprintf(out, "%s%s s%zu", j ? ", " : "", type_name(f->params.data[j]), j);
        }
        fprintf(out, ") slots=%u {\n", f->nslots);
        for (size_t j = 0; j < f->blocks.len; j++) {
            const IrBlock *b = f->blocks.data[j];
            fprintf(out, "@b%u\n", b->id);
            for (size_t k = 0; k < b->instrs.len; k++) {
                dump_instr(out, &b->instrs.data[k]);
            }
        }
        fputs("}\n", out);
    }
}
