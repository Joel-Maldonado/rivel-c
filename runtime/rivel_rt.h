/*
 * Rivel runtime ABI.
 *
 * Everything the compiler emits calls into is declared here. The compiler
 * also emits static data (string literals, type descriptors, panic
 * locations) whose layout must match the structs below exactly, so this
 * header is a contract: field order, sizes, and offsets are frozen.
 *
 * Conventions:
 *   - All heap objects begin with RvHeader (16 bytes) and are 8-byte aligned.
 *   - Every field of a struct object, list element, and box payload is one
 *     8-byte cell. int and pointer cells hold their value; float cells hold
 *     the IEEE bits; bool cells hold 0 or 1.
 *   - Booleans cross the ABI as rv_bool (int32_t, 0 or 1), never as C bool.
 *   - Functions that can fail take a `const RvLoc *` naming the source
 *     location to report; they never return on failure.
 *   - Static objects (string literals) carry RV_F_STATIC and are never freed
 *     or scanned; they must not contain heap pointers.
 */
#ifndef RIVEL_RT_H
#define RIVEL_RT_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t rv_bool;
typedef struct RvTypeInfo RvTypeInfo;

/* ---- object header ---------------------------------------------------- */

typedef struct RvHeader {
    const RvTypeInfo *ti; /* offset 0  */
    uint32_t flags;       /* offset 8  */
    uint32_t reserved;    /* offset 12 */
} RvHeader;               /* size 16 */

enum {
    RV_F_STATIC = 1u << 0, /* not heap allocated; never freed, never scanned */
    RV_F_MARK = 1u << 1,   /* owned by the collector */
};

/* ---- type descriptors -------------------------------------------------- */

enum {
    RV_KIND_STRUCT = 0, /* RvObject: nfields cells after the header */
    RV_KIND_STR = 1,    /* RvStr */
    RV_KIND_LIST = 2,   /* RvList */
    RV_KIND_BOX = 3,    /* RvBox: one cell, kind given by field_kinds[0] */
    RV_KIND_CELLS = 4,  /* RvCells: raw list storage */
};

enum {
    RV_FK_INT = 0,
    RV_FK_FLOAT = 1,
    RV_FK_BOOL = 2,
    RV_FK_PTR = 3,
};

/*
 * Emitted by the compiler for every user struct as:
 *   data $rv_ti_Point = align 8 { l $name, l 0, l 2, l $field_kinds }
 */
struct RvTypeInfo {
    const char *name;           /* offset 0  */
    int64_t kind;               /* offset 8  RV_KIND_* */
    int64_t nfields;            /* offset 16 STRUCT/BOX: number of cells */
    const uint8_t *field_kinds; /* offset 24 STRUCT/BOX: nfields entries of RV_FK_* */
}; /* size 32 */

extern const RvTypeInfo rv_ti_str;
extern const RvTypeInfo rv_ti_list;
extern const RvTypeInfo rv_ti_cells_scalar;
extern const RvTypeInfo rv_ti_cells_ptr;
extern const RvTypeInfo rv_ti_box_int;
extern const RvTypeInfo rv_ti_box_float;
extern const RvTypeInfo rv_ti_box_bool;

/* ---- heap object layouts ---------------------------------------------- */

/*
 * String literal emitted by the compiler as:
 *   data $rv_s0 = align 8 { l $rv_ti_str, w 1, w 0, l 5, b "hello", b 0 }
 */
typedef struct RvStr {
    RvHeader h;   /* offset 0  */
    int64_t len;  /* offset 16 byte length */
    char bytes[]; /* offset 24 bytes[len] is always 0 */
} RvStr;

typedef struct RvCells {
    RvHeader h;      /* offset 0  */
    int64_t n;       /* offset 16 number of cells */
    int64_t cells[]; /* offset 24 */
} RvCells;

typedef struct RvList {
    RvHeader h;        /* offset 0  */
    int64_t len;       /* offset 16 */
    int64_t cap;       /* offset 24 */
    RvCells *cells;    /* offset 32 storage, len <= cap == cells->n */
    int64_t elem_kind; /* offset 40 RV_FK_* */
} RvList;

typedef struct RvBox {
    RvHeader h;   /* offset 0  */
    int64_t cell; /* offset 16 */
} RvBox;

typedef struct RvObject {
    RvHeader h;      /* offset 0  */
    int64_t cells[]; /* offset 16 + 8*i for field i */
} RvObject;

/*
 * Source location for panics, emitted as:
 *   data $rv_loc3 = align 8 { l $rv_file0, l 12, l 5 }
 */
typedef struct RvLoc {
    const char *file; /* offset 0  */
    int64_t line;     /* offset 8  */
    int64_t col;      /* offset 16 */
} RvLoc;              /* size 24 */

/* ---- lifecycle --------------------------------------------------------- */

/*
 * Called first thing in the generated C-ABI main. stack_bottom is the address
 * of a stack slot in main's frame; the collector scans from the current stack
 * pointer up to it.
 */
void rv_init(int argc, char **argv, void *stack_bottom);

/* Registers pointer-typed globals as roots. slots must stay valid forever. */
void rv_add_roots(void **slots, int64_t count);

/* Flushes buffered stdout. Called before returning from main. */
void rv_flush(void);

_Noreturn void rv_exit(int64_t code);

/* ---- allocation and collection ---------------------------------------- */

/* STRUCT or BOX: allocates 16 + 8*nfields bytes, zero-filled, header set. */
void *rv_alloc(const RvTypeInfo *ti);

void rv_gc_collect(void);
size_t rv_gc_live_bytes(void);
/* Test hook: when nonzero, every allocation runs a collection first. */
void rv_gc_set_stress(int on);

/* ---- panics ----------------------------------------------------------- */

/* Prints "panic: <msg>\n  at file:line:col\n" to stderr, flushes, exits 101. */
_Noreturn void rv_panic(RvStr *msg, const RvLoc *loc);
_Noreturn void rv_panic_cstr(const char *msg, const RvLoc *loc);
void rv_assert(rv_bool cond, const RvLoc *loc);

/* ---- checked integer arithmetic --------------------------------------- */

int64_t rv_add(int64_t a, int64_t b, const RvLoc *loc); /* panics on overflow */
int64_t rv_sub(int64_t a, int64_t b, const RvLoc *loc);
int64_t rv_mul(int64_t a, int64_t b, const RvLoc *loc);
int64_t rv_neg(int64_t a, const RvLoc *loc);
int64_t rv_div(int64_t a, int64_t b, const RvLoc *loc); /* floor; panics on zero and INT64_MIN / -1 */
int64_t rv_mod(int64_t a, int64_t b, const RvLoc *loc); /* floor; result has the divisor's sign */
int64_t rv_shl(int64_t a, int64_t n, const RvLoc *loc); /* panics unless 0 <= n < 64 */
int64_t rv_shr(int64_t a, int64_t n, const RvLoc *loc); /* arithmetic */
int64_t rv_abs(int64_t a, const RvLoc *loc);
int64_t rv_float_to_int(double f, const RvLoc *loc); /* truncates; panics on NaN, inf, out of range */

/* ---- strings ----------------------------------------------------------- */

RvStr *rv_str_new(const char *bytes, int64_t len); /* copies */
RvStr *rv_str_empty(void);
RvStr *rv_str_concat(RvStr *a, RvStr *b);
rv_bool rv_str_eq(RvStr *a, RvStr *b);
int64_t rv_str_cmp(RvStr *a, RvStr *b);                     /* -1, 0, 1 by bytes */
RvStr *rv_str_index(RvStr *s, int64_t i, const RvLoc *loc); /* one-byte string */
RvStr *rv_str_slice(RvStr *s, int64_t lo, int64_t hi, const RvLoc *loc);
rv_bool rv_str_contains(RvStr *s, RvStr *t);
rv_bool rv_str_starts_with(RvStr *s, RvStr *t);
rv_bool rv_str_ends_with(RvStr *s, RvStr *t);
int64_t rv_str_find(RvStr *s, RvStr *t);    /* byte index or -1 */
RvList *rv_str_split(RvStr *s, RvStr *sep); /* list[str]; empty sep splits into bytes */
RvStr *rv_str_join(RvStr *sep, RvList *parts);
RvStr *rv_str_trim(RvStr *s); /* ASCII whitespace */
RvStr *rv_str_upper(RvStr *s);
RvStr *rv_str_lower(RvStr *s);
RvStr *rv_str_replace(RvStr *s, RvStr *from, RvStr *to);
RvStr *rv_str_repeat(RvStr *s, int64_t n, const RvLoc *loc); /* panics if n < 0 */
RvStr *rv_str_from_int(int64_t v);
RvStr *rv_str_from_float(double v); /* shortest round-trip; always has '.' or 'e'; "inf", "-inf", "nan" */
RvStr *rv_str_from_bool(rv_bool v);
RvBox *rv_str_parse_int(RvStr *s);          /* box_int or NULL; accepts optional sign, decimal digits, no whitespace */
RvBox *rv_str_parse_float(RvStr *s);        /* box_float or NULL */
int64_t rv_ord(RvStr *s, const RvLoc *loc); /* panics unless len == 1 */
RvStr *rv_chr(int64_t byte, const RvLoc *loc); /* panics unless 0 <= byte < 256 */

/* ---- lists ------------------------------------------------------------- */
/*
 * _l variants move int, bool, and pointer cells; _d variants move floats.
 * The list remembers its element kind for equality and contains().
 */

RvList *rv_list_new(int64_t elem_kind, int64_t cap);
int64_t rv_list_get_l(RvList *l, int64_t i, const RvLoc *loc);
double rv_list_get_d(RvList *l, int64_t i, const RvLoc *loc);
void rv_list_set_l(RvList *l, int64_t i, int64_t v, const RvLoc *loc);
void rv_list_set_d(RvList *l, int64_t i, double v, const RvLoc *loc);
void rv_list_append_l(RvList *l, int64_t v);
void rv_list_append_d(RvList *l, double v);
int64_t rv_list_pop_l(RvList *l, const RvLoc *loc);
double rv_list_pop_d(RvList *l, const RvLoc *loc);
void rv_list_insert_l(RvList *l, int64_t i, int64_t v, const RvLoc *loc); /* 0 <= i <= len */
void rv_list_insert_d(RvList *l, int64_t i, double v, const RvLoc *loc);
int64_t rv_list_remove_at_l(RvList *l, int64_t i, const RvLoc *loc);
double rv_list_remove_at_d(RvList *l, int64_t i, const RvLoc *loc);
void rv_list_clear(RvList *l);
rv_bool rv_list_contains_l(RvList *l, int64_t v);
rv_bool rv_list_contains_d(RvList *l, double v);
int64_t rv_list_index_of_l(RvList *l, int64_t v);
int64_t rv_list_index_of_d(RvList *l, double v);
RvList *rv_list_slice(RvList *l, int64_t lo, int64_t hi, const RvLoc *loc);
rv_bool rv_list_eq(RvList *a, RvList *b);

/* ---- boxes (optionals of value types) --------------------------------- */

RvBox *rv_box_int(int64_t v);
RvBox *rv_box_float(double v);
RvBox *rv_box_bool(rv_bool v);

/* ---- structural equality for reference values -------------------------- */

/* Deep equality by type descriptor. Either argument may be NULL. */
rv_bool rv_eq(void *a, void *b);

/* ---- io ---------------------------------------------------------------- */

void rv_print(RvStr *s);
void rv_println(RvStr *s);
void rv_eprintln(RvStr *s);
RvList *rv_args(void);            /* list[str], excludes the program name */
RvStr *rv_read_file(RvStr *path); /* NULL on failure */
rv_bool rv_write_file(RvStr *path, RvStr *data);
RvStr *rv_read_line(void); /* NULL at end of input; strips the trailing newline */

/* ---- math -------------------------------------------------------------- */

double rv_sqrt(double x);

#endif
