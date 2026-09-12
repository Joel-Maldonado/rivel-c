#ifndef RV_IR_IR_H
#define RV_IR_IR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "base/arena.h"
#include "base/str.h"
#include "base/vec.h"

/*
 * Rivel IR: a small three-address form over basic blocks.
 *
 * Values live in temporaries (single assignment within a function) and in
 * numbered stack slots (mutable; one per local variable and per hidden
 * loop counter). Every local is a slot so lowering never needs phi nodes;
 * QBE promotes slots that do not escape to registers.
 *
 * Only three machine classes exist: W (32-bit int, used for bool), L (64-bit
 * int and every pointer), and D (double). The garbage collector finds
 * pointers by conservatively scanning the stack, so the IR carries no
 * pointer-vs-int distinction beyond what the runtime type descriptors record.
 */

typedef enum IrType {
    IR_W,
    IR_L,
    IR_D,
} IrType;

typedef enum IrValueKind {
    IRV_NONE,
    IRV_TEMP,
    IRV_INT,    /* W or L immediate */
    IRV_FLOAT,  /* D immediate */
    IRV_SYMBOL, /* address of a global data or function symbol */
} IrValueKind;

typedef struct IrValue {
    IrValueKind kind;
    IrType type;
    union {
        uint32_t temp;
        int64_t i;
        double f;
        const char *sym;
    } as;
} IrValue;

typedef enum IrOp {
    /* dst = a op b */
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV, /* signed, truncating; floor semantics are done in the runtime */
    IR_REM,
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_SHL,
    IR_SAR,
    /* dst = a op b, result W, `type` is the operand class */
    IR_CEQ,
    IR_CNE,
    IR_CLT,
    IR_CLE,
    IR_CGT,
    IR_CGE,
    /* dst = op a */
    IR_COPY, /* same class, or L to W narrowing */
    IR_NEG,
    IR_EXTSW, /* W to L, sign extended */
    IR_EXTUW, /* W to L, zero extended */
    IR_SLTOF, /* L to D */
    IR_DTOSI, /* D to L, truncating (caller checks range) */
    IR_CAST,  /* bit move between L and D */
    /* memory */
    IR_LOAD,       /* dst = *(a + offset), class `type` */
    IR_STORE,      /* *(b + offset) = a */
    IR_LOAD_SLOT,  /* dst = slot[slot] */
    IR_STORE_SLOT, /* slot[slot] = a */
    IR_SLOT_ADDR,  /* dst = &slot[slot] (L) */
    /* calls */
    IR_CALL, /* dst = callee(args...); has_dst false for void calls */
    /* terminators */
    IR_JMP, /* target */
    IR_BR,  /* if a != 0 goto target else target2 */
    IR_RET, /* a, or none for void */
    IR_HLT, /* unreachable */
} IrOp;

typedef Vec(IrValue) IrValueVec;

typedef struct IrInstr {
    IrOp op;
    IrType type; /* result class, or operand class for compares and memory ops */
    bool has_dst;
    uint32_t dst;
    IrValue a;
    IrValue b;
    int32_t offset;
    uint32_t slot;
    const char *callee;
    IrValueVec args;
    uint32_t target;
    uint32_t target2;
} IrInstr;

typedef Vec(IrInstr) IrInstrVec;

typedef struct IrBlock {
    uint32_t id;
    IrInstrVec instrs; /* the last instruction is always a terminator once lowering finishes */
} IrBlock;

typedef Vec(IrBlock *) IrBlockVec;
typedef Vec(IrType) IrTypeVec;

typedef struct IrFunc {
    Arena *arena;
    const char *name; /* mangled symbol */
    bool exported;
    IrTypeVec params; /* parameter classes; parameter i is stored into slot i on entry */
    bool has_ret;
    IrType ret;
    uint32_t nslots;
    uint32_t ntemps;
    IrBlockVec blocks; /* blocks[0] is the entry block */
} IrFunc;

typedef Vec(IrFunc *) IrFuncVec;

/* A string literal: emitted as a static RvStr with the layout from rivel_rt.h. */
typedef struct IrStrLit {
    const char *sym;
    Str bytes;
} IrStrLit;

typedef Vec(IrStrLit) IrStrLitVec;

/* A struct type descriptor: emitted as an RvTypeInfo plus its field kind table. */
typedef struct IrTypeInfo {
    const char *sym;
    Str name;
    Vec(uint8_t) field_kinds; /* RV_FK_* per cell */
} IrTypeInfo;

typedef Vec(IrTypeInfo) IrTypeInfoVec;

/* A panic location: emitted as an RvLoc. */
typedef struct IrLoc {
    const char *sym;
    int line;
    int col;
} IrLoc;

typedef Vec(IrLoc) IrLocVec;

typedef enum IrGlobalKind {
    IRG_INT,   /* 8-byte cell holding an int or a bool */
    IRG_FLOAT, /* 8-byte cell holding a double */
    IRG_PTR,   /* 8-byte cell holding a pointer; a GC root */
} IrGlobalKind;

typedef struct IrGlobal {
    const char *sym;
    IrGlobalKind kind;
    int64_t init_int;
    double init_float;
    const char *init_sym; /* IRG_PTR: symbol of a string literal, or NULL for null */
} IrGlobal;

typedef Vec(IrGlobal) IrGlobalVec;

typedef struct IrModule {
    Arena *arena;
    void *string_map;        /* StrMap: literal bytes -> symbol, for interning */
    const char *source_name; /* file name recorded in panic locations */
    IrFuncVec funcs;
    IrStrLitVec strings;
    IrTypeInfoVec typeinfos;
    IrLocVec locs;
    IrGlobalVec globals;
    const char *main_sym; /* mangled name of the user's main, or NULL */
    bool main_returns_int;
} IrModule;

/* ---- construction ------------------------------------------------------------ */

void ir_module_init(IrModule *m, Arena *arena, const char *source_name);
void ir_module_free(IrModule *m);

IrFunc *ir_func_new(IrModule *m, const char *name);
IrBlock *ir_block_new(IrFunc *f);
uint32_t ir_temp(IrFunc *f);
uint32_t ir_slot(IrFunc *f);

/* Interns identical literals; returns the data symbol. */
const char *ir_string_literal(IrModule *m, Str bytes);
const char *ir_loc(IrModule *m, int line, int col);

static inline IrValue ir_none(void) {
    IrValue v = {0};
    v.kind = IRV_NONE;
    return v;
}

static inline IrValue ir_tmp(IrType t, uint32_t id) {
    IrValue v = {0};
    v.kind = IRV_TEMP;
    v.type = t;
    v.as.temp = id;
    return v;
}

static inline IrValue ir_int(IrType t, int64_t i) {
    IrValue v = {0};
    v.kind = IRV_INT;
    v.type = t;
    v.as.i = i;
    return v;
}

static inline IrValue ir_float(double f) {
    IrValue v = {0};
    v.kind = IRV_FLOAT;
    v.type = IR_D;
    v.as.f = f;
    return v;
}

static inline IrValue ir_sym(const char *sym) {
    IrValue v = {0};
    v.kind = IRV_SYMBOL;
    v.type = IR_L;
    v.as.sym = sym;
    return v;
}

/* Emitters append to `b` and return the result value where there is one. */
IrValue ir_emit_binary(IrFunc *f, IrBlock *b, IrOp op, IrType t, IrValue a, IrValue c);
IrValue ir_emit_cmp(IrFunc *f, IrBlock *b, IrOp op, IrType operand_type, IrValue a, IrValue c);
IrValue ir_emit_unary(IrFunc *f, IrBlock *b, IrOp op, IrType t, IrValue a);
IrValue ir_emit_load(IrFunc *f, IrBlock *b, IrType t, IrValue addr, int32_t offset);
void ir_emit_store(IrBlock *b, IrType t, IrValue value, IrValue addr, int32_t offset);
IrValue ir_emit_load_slot(IrFunc *f, IrBlock *b, IrType t, uint32_t slot);
void ir_emit_store_slot(IrBlock *b, IrType t, IrValue value, uint32_t slot);
IrValue ir_emit_slot_addr(IrFunc *f, IrBlock *b, uint32_t slot);
/* has_ret false makes a void call; args are copied. */
IrValue ir_emit_call(IrFunc *f, IrBlock *b, const char *callee, bool has_ret, IrType ret, const IrValue *args,
                     size_t nargs);
void ir_emit_jmp(IrBlock *b, IrBlock *target);
void ir_emit_br(IrBlock *b, IrValue cond, IrBlock *then_block, IrBlock *else_block);
void ir_emit_ret(IrBlock *b, IrValue value);
void ir_emit_ret_void(IrBlock *b);
void ir_emit_hlt(IrBlock *b);

/* True when the block already ends in a terminator. */
bool ir_block_terminated(const IrBlock *b);

/* --dump-ir */
void ir_dump(const IrModule *m, FILE *out);

#endif
