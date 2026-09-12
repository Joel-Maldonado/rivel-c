/*
 * Rivel runtime: allocator and collector, strings, lists, boxes, checked
 * arithmetic, panics, and IO. rivel_rt.h is the ABI contract.
 *
 * Panic messages produced by this file:
 *   "integer overflow"                      rv_add rv_sub rv_mul rv_neg rv_abs rv_div rv_str_repeat
 *   "division by zero"                      rv_div rv_mod
 *   "shift amount out of range"             rv_shl rv_shr
 *   "float to int conversion out of range"  rv_float_to_int
 *   "index out of range"                    rv_str_index rv_list_get_* rv_list_set_* rv_list_insert_*
 *                                           rv_list_remove_at_*
 *   "slice out of range"                    rv_str_slice rv_list_slice
 *   "pop from empty list"                   rv_list_pop_*
 *   "negative repeat count"                 rv_str_repeat
 *   "ord expects a one-byte string"         rv_ord
 *   "chr out of range"                      rv_chr
 *   "assertion failed"                      rv_assert
 *   "out of memory"                         allocator (no location)
 */
#include "rivel_rt.h"

#include <inttypes.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

_Static_assert(sizeof(RvHeader) == 16, "RvHeader layout");
_Static_assert(sizeof(RvTypeInfo) == 32, "RvTypeInfo layout");
_Static_assert(offsetof(RvStr, bytes) == 24, "RvStr layout");
_Static_assert(offsetof(RvCells, cells) == 24, "RvCells layout");
_Static_assert(sizeof(RvList) == 48, "RvList layout");
_Static_assert(sizeof(RvLoc) == 24, "RvLoc layout");

/* The stack scan deliberately reads the redzones ASan places between locals. */
#define NO_ASAN __attribute__((noinline, no_sanitize_address))

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RV_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define RV_ASAN 1
#endif

#ifdef RV_ASAN
/* ASan's use-after-return detection moves locals into heap-allocated fake frames that a stack scan cannot see. */
const char *__asan_default_options(void) {
    return "detect_stack_use_after_return=0";
}
#endif

/* ---- type descriptors -------------------------------------------------- */

static const uint8_t box_int_kinds[1] = {RV_FK_INT};
static const uint8_t box_float_kinds[1] = {RV_FK_FLOAT};
static const uint8_t box_bool_kinds[1] = {RV_FK_BOOL};

const RvTypeInfo rv_ti_str = {"str", RV_KIND_STR, 0, NULL};
const RvTypeInfo rv_ti_list = {"list", RV_KIND_LIST, 0, NULL};
const RvTypeInfo rv_ti_cells_scalar = {"cells", RV_KIND_CELLS, 0, NULL};
const RvTypeInfo rv_ti_cells_ptr = {"cells", RV_KIND_CELLS, 0, NULL};
const RvTypeInfo rv_ti_box_int = {"int?", RV_KIND_BOX, 1, box_int_kinds};
const RvTypeInfo rv_ti_box_float = {"float?", RV_KIND_BOX, 1, box_float_kinds};
const RvTypeInfo rv_ti_box_bool = {"bool?", RV_KIND_BOX, 1, box_bool_kinds};

/* ---- panics ----------------------------------------------------------- */

static _Noreturn void panic_bytes(const char *msg, size_t len, const RvLoc *loc) {
    fflush(stdout);
    fputs("panic: ", stderr);
    fwrite(msg, 1, len, stderr);
    fputc('\n', stderr);
    if (loc) {
        fprintf(stderr, "  at %s:%" PRId64 ":%" PRId64 "\n", loc->file, loc->line, loc->col);
    }
    exit(101);
}

_Noreturn void rv_panic(RvStr *msg, const RvLoc *loc) {
    panic_bytes(msg->bytes, (size_t)msg->len, loc);
}

_Noreturn void rv_panic_cstr(const char *msg, const RvLoc *loc) {
    panic_bytes(msg, strlen(msg), loc);
}

void rv_assert(rv_bool cond, const RvLoc *loc) {
    if (!cond) {
        rv_panic_cstr("assertion failed", loc);
    }
}

/* ---- malloc'd bookkeeping ---------------------------------------------- */

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n);
    if (!p) {
        rv_panic_cstr("out of memory", NULL);
    }
    return p;
}

/* Grows the array at *arr (elements of elem_size bytes) to hold need elements. */
static void reserve_array(void *arr, size_t *cap, size_t need, size_t elem_size) {
    if (need <= *cap) {
        return;
    }
    size_t n = *cap ? *cap : 16;
    while (n < need) {
        n *= 2;
    }
    *(void **)arr = xrealloc(*(void **)arr, n * elem_size);
    *cap = n;
}

/* ---- heap layout ------------------------------------------------------- */
/*
 * Small objects live in 64 KiB pages, each page holding slots of one size
 * class. A free slot has a NULL ti and is threaded on the page's free list.
 * Objects above the largest class are malloc'd individually and kept in an
 * address-sorted array so interior pointers can be resolved by binary search.
 */

enum {
    PAGE_BITS = 16,
    PAGE_BYTES = 1 << PAGE_BITS,
    NCLASSES = 13,
    MAX_SMALL = 4096,
    GC_MIN_THRESHOLD = 4 << 20,
};

static const uint32_t class_sizes[NCLASSES] = {32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048, 4096};

typedef struct FreeSlot {
    const RvTypeInfo *ti; /* always NULL while free */
    struct FreeSlot *next;
} FreeSlot;

_Static_assert(sizeof(FreeSlot) <= 32, "free slot fits the smallest class");

typedef struct Page {
    char *mem; /* PAGE_BYTES bytes, PAGE_BYTES aligned */
    struct Page *next_avail;
    FreeSlot *free;
    uint32_t slot_size;
    uint32_t nslots;
    uint32_t nfree;
    uint32_t cls;
} Page;

typedef struct Large {
    char *mem;
    size_t size;
} Large;

typedef struct Roots {
    void **slots;
    int64_t count;
} Roots;

static Page **pages; /* every page, unordered */
static size_t npages, pages_cap;
static Page **page_table; /* open-addressing set keyed by Page.mem */
static size_t table_cap;
static unsigned table_bits;
static Page *page_avail[NCLASSES]; /* pages with at least one free slot */
static Large *larges;              /* sorted by address */
static size_t nlarge, large_cap;
static uintptr_t heap_lo = UINTPTR_MAX, heap_hi; /* bounds of all heap memory */

static RvHeader **work; /* mark stack */
static size_t nwork, work_cap;
static Roots *roots;
static size_t nroots, roots_cap;
static void *stack_bottom;

static size_t live_bytes; /* survivors of the last collection */
static size_t allocated_since;
static size_t threshold = GC_MIN_THRESHOLD;
static int stress;

static int argc_saved;
static char **argv_saved;

/* ---- page registry ----------------------------------------------------- */

static size_t table_slot(uintptr_t mem) {
    return (size_t)(((mem >> PAGE_BITS) * UINT64_C(0x9E3779B97F4A7C15)) >> (64 - table_bits));
}

static void table_insert(Page *p) {
    size_t i = table_slot((uintptr_t)p->mem);
    while (page_table[i]) {
        i = (i + 1) & (table_cap - 1);
    }
    page_table[i] = p;
}

static void table_rebuild(void) {
    table_bits = 6;
    table_cap = 64;
    while (table_cap < 2 * npages) {
        table_cap *= 2;
        table_bits++;
    }
    page_table = xrealloc(page_table, table_cap * sizeof *page_table);
    memset(page_table, 0, table_cap * sizeof *page_table);
    for (size_t k = 0; k < npages; k++) {
        table_insert(pages[k]);
    }
}

static Page *page_lookup(uintptr_t mem) {
    if (!page_table) {
        return NULL;
    }
    for (size_t i = table_slot(mem);; i = (i + 1) & (table_cap - 1)) {
        Page *p = page_table[i];
        if (!p || (uintptr_t)p->mem == mem) {
            return p;
        }
    }
}

static void note_range(uintptr_t lo, uintptr_t hi) {
    if (lo < heap_lo) {
        heap_lo = lo;
    }
    if (hi > heap_hi) {
        heap_hi = hi;
    }
}

static Page *new_page(unsigned cls) {
    Page *p = xrealloc(NULL, sizeof *p);
    void *mem;
    if (posix_memalign(&mem, PAGE_BYTES, PAGE_BYTES) != 0) {
        rv_panic_cstr("out of memory", NULL);
    }
    p->mem = mem;
    p->slot_size = class_sizes[cls];
    p->nslots = PAGE_BYTES / p->slot_size;
    p->nfree = p->nslots;
    p->cls = cls;
    p->free = NULL;
    for (uint32_t i = p->nslots; i-- > 0;) {
        FreeSlot *s = (FreeSlot *)(p->mem + i * p->slot_size);
        s->ti = NULL;
        s->next = p->free;
        p->free = s;
    }
    p->next_avail = page_avail[cls];
    page_avail[cls] = p;

    reserve_array(&pages, &pages_cap, npages + 1, sizeof *pages);
    pages[npages++] = p;
    if (2 * npages > table_cap) {
        table_rebuild();
    } else {
        table_insert(p);
    }
    note_range((uintptr_t)p->mem, (uintptr_t)p->mem + PAGE_BYTES);
    return p;
}

/* Number of large objects starting at or before address w. */
static size_t large_upper_bound(uintptr_t w) {
    size_t lo = 0, hi = nlarge;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uintptr_t)larges[mid].mem <= w) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static Large *large_lookup(uintptr_t w) {
    size_t n = large_upper_bound(w);
    if (n == 0) {
        return NULL;
    }
    Large *l = &larges[n - 1];
    return w < (uintptr_t)l->mem + l->size ? l : NULL;
}

static void *new_large(size_t size) {
    char *mem = xrealloc(NULL, size);
    size_t at = large_upper_bound((uintptr_t)mem);
    reserve_array(&larges, &large_cap, nlarge + 1, sizeof *larges);
    memmove(&larges[at + 1], &larges[at], (nlarge - at) * sizeof *larges);
    larges[at].mem = mem;
    larges[at].size = size;
    nlarge++;
    note_range((uintptr_t)mem, (uintptr_t)mem + size);
    return mem;
}

/* ---- allocation -------------------------------------------------------- */

static void *alloc_small(size_t size) {
    unsigned cls = 0;
    while (class_sizes[cls] < size) {
        cls++;
    }
    Page *p = page_avail[cls] ? page_avail[cls] : new_page(cls);
    FreeSlot *s = p->free;
    p->free = s->next;
    if (--p->nfree == 0) {
        page_avail[cls] = p->next_avail;
    }
    allocated_since += p->slot_size;
    return s;
}

static void *gc_alloc(size_t size, const RvTypeInfo *ti) {
    if (stress || allocated_since >= threshold) {
        rv_gc_collect();
    }
    void *obj;
    if (size <= MAX_SMALL) {
        obj = alloc_small(size);
    } else {
        obj = new_large(size);
        allocated_since += size;
    }
    memset(obj, 0, size);
    ((RvHeader *)obj)->ti = ti;
    return obj;
}

void *rv_alloc(const RvTypeInfo *ti) {
    return gc_alloc(sizeof(RvHeader) + 8 * (size_t)ti->nfields, ti);
}

/* ---- marking ----------------------------------------------------------- */

/* The allocated object containing address w, or NULL if w is not a heap pointer. */
static RvHeader *find_object(uintptr_t w) {
    if (w < heap_lo || w >= heap_hi) {
        return NULL;
    }
    Page *p = page_lookup(w & ~(uintptr_t)(PAGE_BYTES - 1));
    if (p) {
        size_t i = (w - (uintptr_t)p->mem) / p->slot_size;
        if (i >= p->nslots) {
            return NULL;
        }
        RvHeader *h = (RvHeader *)(p->mem + i * p->slot_size);
        return h->ti ? h : NULL;
    }
    Large *l = large_lookup(w);
    return l ? (RvHeader *)l->mem : NULL;
}

static void mark_word(uintptr_t w) {
    RvHeader *h = find_object(w);
    if (!h || (h->flags & (RV_F_MARK | RV_F_STATIC))) {
        return;
    }
    h->flags |= RV_F_MARK;
    reserve_array(&work, &work_cap, nwork + 1, sizeof *work);
    work[nwork++] = h;
}

static void scan_object(const RvHeader *h) {
    const RvTypeInfo *ti = h->ti;
    switch (ti->kind) {
    case RV_KIND_STRUCT:
    case RV_KIND_BOX: {
        const RvObject *o = (const RvObject *)h;
        for (int64_t i = 0; i < ti->nfields; i++) {
            if (ti->field_kinds[i] == RV_FK_PTR) {
                mark_word((uintptr_t)o->cells[i]);
            }
        }
        break;
    }
    case RV_KIND_LIST:
        mark_word((uintptr_t)((const RvList *)h)->cells);
        break;
    case RV_KIND_CELLS:
        if (ti == &rv_ti_cells_ptr) {
            const RvCells *c = (const RvCells *)h;
            for (int64_t i = 0; i < c->n; i++) {
                mark_word((uintptr_t)c->cells[i]);
            }
        }
        break;
    default:
        break;
    }
}

NO_ASAN static void scan_words(uintptr_t lo, uintptr_t hi) {
    for (lo = (lo + 7) & ~(uintptr_t)7; lo + sizeof(uintptr_t) <= hi; lo += sizeof(uintptr_t)) {
        mark_word(*(const uintptr_t *)lo);
    }
}

/* Spills the callee-saved registers into a local and scans it together with every frame above this one. */
static void scan_stack(void) {
    jmp_buf regs;
    setjmp(regs);
    scan_words((uintptr_t)&regs, (uintptr_t)&regs + sizeof regs);
    scan_words((uintptr_t)__builtin_frame_address(0), (uintptr_t)stack_bottom);
}

/* ---- sweeping ---------------------------------------------------------- */

static void sweep_pages(void) {
    size_t kept = 0;
    memset(page_avail, 0, sizeof page_avail);
    for (size_t k = 0; k < npages; k++) {
        Page *p = pages[k];
        p->free = NULL;
        p->nfree = 0;
        for (uint32_t i = p->nslots; i-- > 0;) {
            RvHeader *h = (RvHeader *)(p->mem + i * p->slot_size);
            if (h->ti && (h->flags & RV_F_MARK)) {
                h->flags &= ~(uint32_t)RV_F_MARK;
                live_bytes += p->slot_size;
                continue;
            }
            FreeSlot *s = (FreeSlot *)h;
            s->ti = NULL;
            s->next = p->free;
            p->free = s;
            p->nfree++;
        }
        if (p->nfree == p->nslots) {
            free(p->mem);
            free(p);
            continue;
        }
        pages[kept++] = p;
        if (p->nfree > 0) {
            p->next_avail = page_avail[p->cls];
            page_avail[p->cls] = p;
        }
        note_range((uintptr_t)p->mem, (uintptr_t)p->mem + PAGE_BYTES);
    }
    npages = kept;
    table_rebuild();
}

static void sweep_larges(void) {
    size_t kept = 0;
    for (size_t k = 0; k < nlarge; k++) {
        Large l = larges[k];
        RvHeader *h = (RvHeader *)l.mem;
        if (!(h->flags & RV_F_MARK)) {
            free(l.mem);
            continue;
        }
        h->flags &= ~(uint32_t)RV_F_MARK;
        live_bytes += l.size;
        note_range((uintptr_t)l.mem, (uintptr_t)l.mem + l.size);
        larges[kept++] = l;
    }
    nlarge = kept;
}

void rv_gc_collect(void) {
    for (size_t k = 0; k < nroots; k++) {
        for (int64_t i = 0; i < roots[k].count; i++) {
            mark_word((uintptr_t)roots[k].slots[i]);
        }
    }
    scan_stack();
    while (nwork > 0) {
        scan_object(work[--nwork]);
    }

    live_bytes = 0;
    heap_lo = UINTPTR_MAX;
    heap_hi = 0;
    sweep_pages();
    sweep_larges();
    allocated_since = 0;
    threshold = 2 * live_bytes > GC_MIN_THRESHOLD ? 2 * live_bytes : GC_MIN_THRESHOLD;
}

size_t rv_gc_live_bytes(void) {
    return live_bytes + allocated_since;
}

void rv_gc_set_stress(int on) {
    stress = on;
}

/* ---- lifecycle --------------------------------------------------------- */

void rv_init(int argc, char **argv, void *bottom) {
    argc_saved = argc;
    argv_saved = argv;
    stack_bottom = bottom;
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
}

void rv_add_roots(void **slots, int64_t count) {
    reserve_array(&roots, &roots_cap, nroots + 1, sizeof *roots);
    roots[nroots].slots = slots;
    roots[nroots].count = count;
    nroots++;
}

void rv_flush(void) {
    fflush(stdout);
}

_Noreturn void rv_exit(int64_t code) {
    fflush(stdout);
    exit((int)code);
}

/* ---- checked integer arithmetic --------------------------------------- */

int64_t rv_add(int64_t a, int64_t b, const RvLoc *loc) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r)) {
        rv_panic_cstr("integer overflow", loc);
    }
    return r;
}

int64_t rv_sub(int64_t a, int64_t b, const RvLoc *loc) {
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r)) {
        rv_panic_cstr("integer overflow", loc);
    }
    return r;
}

int64_t rv_mul(int64_t a, int64_t b, const RvLoc *loc) {
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r)) {
        rv_panic_cstr("integer overflow", loc);
    }
    return r;
}

int64_t rv_neg(int64_t a, const RvLoc *loc) {
    return rv_sub(0, a, loc);
}

int64_t rv_abs(int64_t a, const RvLoc *loc) {
    return a < 0 ? rv_neg(a, loc) : a;
}

int64_t rv_div(int64_t a, int64_t b, const RvLoc *loc) {
    if (b == 0) {
        rv_panic_cstr("division by zero", loc);
    }
    if (a == INT64_MIN && b == -1) {
        rv_panic_cstr("integer overflow", loc);
    }
    int64_t q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0)) {
        q--;
    }
    return q;
}

int64_t rv_mod(int64_t a, int64_t b, const RvLoc *loc) {
    if (b == 0) {
        rv_panic_cstr("division by zero", loc);
    }
    if (b == -1) {
        return 0; /* INT64_MIN % -1 overflows in C; every a mod -1 is 0 */
    }
    int64_t r = a % b;
    if (r != 0 && (r < 0) != (b < 0)) {
        r += b;
    }
    return r;
}

int64_t rv_shl(int64_t a, int64_t n, const RvLoc *loc) {
    if (n < 0 || n >= 64) {
        rv_panic_cstr("shift amount out of range", loc);
    }
    return (int64_t)((uint64_t)a << n);
}

int64_t rv_shr(int64_t a, int64_t n, const RvLoc *loc) {
    if (n < 0 || n >= 64) {
        rv_panic_cstr("shift amount out of range", loc);
    }
    return a < 0 ? ~(~a >> n) : a >> n; /* arithmetic shift without relying on implementation-defined >> */
}

int64_t rv_float_to_int(double f, const RvLoc *loc) {
    if (!(f >= -0x1p63 && f < 0x1p63)) { /* NaN fails both comparisons */
        rv_panic_cstr("float to int conversion out of range", loc);
    }
    return (int64_t)f;
}

/* ---- strings ----------------------------------------------------------- */

#define STATIC_STR(name, lit)                                                                                          \
    static struct {                                                                                                    \
        RvHeader h;                                                                                                    \
        int64_t len;                                                                                                   \
        char bytes[sizeof lit];                                                                                        \
    } name = {{&rv_ti_str, RV_F_STATIC, 0}, sizeof lit - 1, lit}

STATIC_STR(empty_str, "");
STATIC_STR(true_str, "true");
STATIC_STR(false_str, "false");

static struct {
    RvHeader h;
    int64_t len;
    char bytes[8];
} byte_strs[256];

static RvStr *byte_str(unsigned char c) {
    if (!byte_strs[c].h.ti) {
        byte_strs[c].h.ti = &rv_ti_str;
        byte_strs[c].h.flags = RV_F_STATIC;
        byte_strs[c].len = 1;
        byte_strs[c].bytes[0] = (char)c;
    }
    return (RvStr *)&byte_strs[c];
}

/* A fresh zero-filled string of the given length, to be filled in by the caller. */
static RvStr *str_alloc(int64_t len) {
    if (len == 0) {
        return (RvStr *)&empty_str;
    }
    RvStr *s = gc_alloc(offsetof(RvStr, bytes) + (size_t)len + 1, &rv_ti_str);
    s->len = len;
    return s;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* First occurrence of t in s at or after index from, or -1. */
static int64_t find_bytes(const char *s, int64_t slen, const char *t, int64_t tlen, int64_t from) {
    if (tlen > slen - from) {
        return -1;
    }
    if (tlen == 0) {
        return from;
    }
    const char *last = s + slen - tlen;
    for (const char *p = s + from; p <= last; p++) {
        p = memchr(p, t[0], (size_t)(last - p + 1));
        if (!p) {
            return -1;
        }
        if (memcmp(p, t, (size_t)tlen) == 0) {
            return p - s;
        }
    }
    return -1;
}

RvStr *rv_str_new(const char *bytes, int64_t len) {
    RvStr *s = str_alloc(len);
    if (len > 0) {
        memcpy(s->bytes, bytes, (size_t)len);
    }
    return s;
}

RvStr *rv_str_empty(void) {
    return (RvStr *)&empty_str;
}

RvStr *rv_str_concat(RvStr *a, RvStr *b) {
    RvStr *s = str_alloc(a->len + b->len);
    if (s->len > 0) {
        memcpy(s->bytes, a->bytes, (size_t)a->len);
        memcpy(s->bytes + a->len, b->bytes, (size_t)b->len);
    }
    return s;
}

rv_bool rv_str_eq(RvStr *a, RvStr *b) {
    return a->len == b->len && memcmp(a->bytes, b->bytes, (size_t)a->len) == 0;
}

int64_t rv_str_cmp(RvStr *a, RvStr *b) {
    int64_t n = a->len < b->len ? a->len : b->len;
    int r = memcmp(a->bytes, b->bytes, (size_t)n);
    if (r != 0) {
        return r < 0 ? -1 : 1;
    }
    return a->len < b->len ? -1 : a->len > b->len;
}

RvStr *rv_str_index(RvStr *s, int64_t i, const RvLoc *loc) {
    if (i < 0 || i >= s->len) {
        rv_panic_cstr("index out of range", loc);
    }
    return byte_str((unsigned char)s->bytes[i]);
}

RvStr *rv_str_slice(RvStr *s, int64_t lo, int64_t hi, const RvLoc *loc) {
    if (lo < 0 || lo > hi || hi > s->len) {
        rv_panic_cstr("slice out of range", loc);
    }
    return rv_str_new(s->bytes + lo, hi - lo);
}

rv_bool rv_str_contains(RvStr *s, RvStr *t) {
    return rv_str_find(s, t) >= 0;
}

rv_bool rv_str_starts_with(RvStr *s, RvStr *t) {
    return t->len <= s->len && memcmp(s->bytes, t->bytes, (size_t)t->len) == 0;
}

rv_bool rv_str_ends_with(RvStr *s, RvStr *t) {
    return t->len <= s->len && memcmp(s->bytes + s->len - t->len, t->bytes, (size_t)t->len) == 0;
}

int64_t rv_str_find(RvStr *s, RvStr *t) {
    return find_bytes(s->bytes, s->len, t->bytes, t->len, 0);
}

static int64_t ptr_cell(const void *p) {
    return (int64_t)(uintptr_t)p;
}

RvList *rv_str_split(RvStr *s, RvStr *sep) {
    RvList *parts = rv_list_new(RV_FK_PTR, 0);
    if (sep->len == 0) {
        for (int64_t i = 0; i < s->len; i++) {
            rv_list_append_l(parts, ptr_cell(byte_str((unsigned char)s->bytes[i])));
        }
        return parts;
    }
    int64_t start = 0;
    for (;;) {
        int64_t at = find_bytes(s->bytes, s->len, sep->bytes, sep->len, start);
        int64_t end = at < 0 ? s->len : at;
        rv_list_append_l(parts, ptr_cell(rv_str_new(s->bytes + start, end - start)));
        if (at < 0) {
            return parts;
        }
        start = at + sep->len;
    }
}

static RvStr *list_str(const RvList *l, int64_t i) {
    return (RvStr *)(uintptr_t)l->cells->cells[i];
}

RvStr *rv_str_join(RvStr *sep, RvList *parts) {
    int64_t total = 0;
    for (int64_t i = 0; i < parts->len; i++) {
        total += list_str(parts, i)->len + (i > 0 ? sep->len : 0);
    }
    RvStr *r = str_alloc(total);
    char *out = r->bytes;
    for (int64_t i = 0; i < parts->len; i++) {
        if (i > 0 && sep->len > 0) {
            memcpy(out, sep->bytes, (size_t)sep->len);
            out += sep->len;
        }
        RvStr *part = list_str(parts, i);
        if (part->len > 0) {
            memcpy(out, part->bytes, (size_t)part->len);
            out += part->len;
        }
    }
    return r;
}

RvStr *rv_str_trim(RvStr *s) {
    int64_t lo = 0, hi = s->len;
    while (lo < hi && is_space(s->bytes[lo])) {
        lo++;
    }
    while (lo < hi && is_space(s->bytes[hi - 1])) {
        hi--;
    }
    return rv_str_new(s->bytes + lo, hi - lo);
}

RvStr *rv_str_upper(RvStr *s) {
    RvStr *r = str_alloc(s->len);
    for (int64_t i = 0; i < s->len; i++) {
        char c = s->bytes[i];
        r->bytes[i] = c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
    }
    return r;
}

RvStr *rv_str_lower(RvStr *s) {
    RvStr *r = str_alloc(s->len);
    for (int64_t i = 0; i < s->len; i++) {
        char c = s->bytes[i];
        r->bytes[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
    return r;
}

RvStr *rv_str_replace(RvStr *s, RvStr *from, RvStr *to) {
    if (from->len == 0) {
        return s;
    }
    int64_t count = 0;
    for (int64_t at = rv_str_find(s, from); at >= 0;
         at = find_bytes(s->bytes, s->len, from->bytes, from->len, at + from->len)) {
        count++;
    }
    if (count == 0) {
        return s;
    }
    RvStr *r = str_alloc(s->len + count * (to->len - from->len));
    char *out = r->bytes;
    int64_t start = 0;
    for (int64_t at; (at = find_bytes(s->bytes, s->len, from->bytes, from->len, start)) >= 0; start = at + from->len) {
        memcpy(out, s->bytes + start, (size_t)(at - start));
        out += at - start;
        memcpy(out, to->bytes, (size_t)to->len);
        out += to->len;
    }
    memcpy(out, s->bytes + start, (size_t)(s->len - start));
    return r;
}

RvStr *rv_str_repeat(RvStr *s, int64_t n, const RvLoc *loc) {
    if (n < 0) {
        rv_panic_cstr("negative repeat count", loc);
    }
    int64_t total;
    if (__builtin_mul_overflow(s->len, n, &total)) {
        rv_panic_cstr("integer overflow", loc);
    }
    RvStr *r = str_alloc(total);
    for (int64_t off = 0; off < total; off += s->len) {
        memcpy(r->bytes + off, s->bytes, (size_t)s->len);
    }
    return r;
}

RvStr *rv_str_from_int(int64_t v) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%" PRId64, v);
    return rv_str_new(buf, n);
}

RvStr *rv_str_from_float(double v) {
    if (isnan(v)) {
        return rv_str_new("nan", 3);
    }
    if (isinf(v)) {
        return v < 0 ? rv_str_new("-inf", 4) : rv_str_new("inf", 3);
    }
    char buf[32];
    int n = 0;
    for (int prec = 15; prec <= 17; prec++) {
        n = snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (strtod(buf, NULL) == v) {
            break;
        }
    }
    if (!strpbrk(buf, ".e")) {
        buf[n++] = '.';
        buf[n++] = '0';
    }
    return rv_str_new(buf, n);
}

RvStr *rv_str_from_bool(rv_bool v) {
    return v ? (RvStr *)&true_str : (RvStr *)&false_str;
}

RvBox *rv_str_parse_int(RvStr *s) {
    const char *p = s->bytes, *end = s->bytes + s->len;
    int neg = 0;
    if (p < end && (*p == '+' || *p == '-')) {
        neg = *p++ == '-';
    }
    if (p == end) {
        return NULL;
    }
    int64_t v = 0;
    for (; p < end; p++) {
        if (*p < '0' || *p > '9') {
            return NULL;
        }
        int d = *p - '0';
        if (__builtin_mul_overflow(v, 10, &v) || __builtin_add_overflow(v, neg ? -d : d, &v)) {
            return NULL;
        }
    }
    return rv_box_int(v);
}

RvBox *rv_str_parse_float(RvStr *s) {
    if (s->len == 0 || is_space(s->bytes[0])) {
        return NULL;
    }
    char *end;
    double v = strtod(s->bytes, &end);
    if (end != s->bytes + s->len) {
        return NULL;
    }
    return rv_box_float(v);
}

int64_t rv_ord(RvStr *s, const RvLoc *loc) {
    if (s->len != 1) {
        rv_panic_cstr("ord expects a one-byte string", loc);
    }
    return (unsigned char)s->bytes[0];
}

RvStr *rv_chr(int64_t byte, const RvLoc *loc) {
    if (byte < 0 || byte >= 256) {
        rv_panic_cstr("chr out of range", loc);
    }
    return byte_str((unsigned char)byte);
}

/* ---- lists ------------------------------------------------------------- */

enum { LIST_MIN_CAP = 8 };

static int64_t double_cell(double d) {
    int64_t bits;
    memcpy(&bits, &d, sizeof bits);
    return bits;
}

static double cell_double(int64_t bits) {
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static RvCells *new_cells(int64_t elem_kind, int64_t n) {
    const RvTypeInfo *ti = elem_kind == RV_FK_PTR ? &rv_ti_cells_ptr : &rv_ti_cells_scalar;
    RvCells *c = gc_alloc(offsetof(RvCells, cells) + 8 * (size_t)n, ti);
    c->n = n;
    return c;
}

RvList *rv_list_new(int64_t elem_kind, int64_t cap) {
    RvCells *cells = new_cells(elem_kind, cap < LIST_MIN_CAP ? LIST_MIN_CAP : cap);
    RvList *l = gc_alloc(sizeof *l, &rv_ti_list);
    l->cap = cells->n;
    l->cells = cells;
    l->elem_kind = elem_kind;
    return l;
}

static void list_reserve(RvList *l, int64_t need) {
    if (need <= l->cap) {
        return;
    }
    int64_t cap = l->cap;
    while (cap < need) {
        cap *= 2;
    }
    RvCells *c = new_cells(l->elem_kind, cap);
    memcpy(c->cells, l->cells->cells, 8 * (size_t)l->len);
    l->cells = c;
    l->cap = cap;
}

static int64_t *cell_at(RvList *l, int64_t i, const RvLoc *loc) {
    if (i < 0 || i >= l->len) {
        rv_panic_cstr("index out of range", loc);
    }
    return &l->cells->cells[i];
}

static void list_append(RvList *l, int64_t v) {
    list_reserve(l, l->len + 1);
    l->cells->cells[l->len++] = v;
}

/* Vacated cells are zeroed so that dead pointers do not keep objects alive. */
static int64_t list_pop(RvList *l, const RvLoc *loc) {
    if (l->len == 0) {
        rv_panic_cstr("pop from empty list", loc);
    }
    int64_t *cell = &l->cells->cells[--l->len];
    int64_t v = *cell;
    *cell = 0;
    return v;
}

static void list_insert(RvList *l, int64_t i, int64_t v, const RvLoc *loc) {
    if (i < 0 || i > l->len) {
        rv_panic_cstr("index out of range", loc);
    }
    list_reserve(l, l->len + 1);
    int64_t *cells = l->cells->cells;
    memmove(cells + i + 1, cells + i, 8 * (size_t)(l->len - i));
    cells[i] = v;
    l->len++;
}

static int64_t list_remove_at(RvList *l, int64_t i, const RvLoc *loc) {
    int64_t *cell = cell_at(l, i, loc);
    int64_t v = *cell;
    memmove(cell, cell + 1, 8 * (size_t)(l->len - i - 1));
    l->cells->cells[--l->len] = 0;
    return v;
}

static rv_bool cell_eq(int64_t kind, int64_t a, int64_t b) {
    switch (kind) {
    case RV_FK_FLOAT:
        return cell_double(a) == cell_double(b);
    case RV_FK_PTR:
        return rv_eq((void *)(uintptr_t)a, (void *)(uintptr_t)b);
    default:
        return a == b;
    }
}

static int64_t list_index_of(RvList *l, int64_t v) {
    for (int64_t i = 0; i < l->len; i++) {
        if (cell_eq(l->elem_kind, l->cells->cells[i], v)) {
            return i;
        }
    }
    return -1;
}

int64_t rv_list_get_l(RvList *l, int64_t i, const RvLoc *loc) {
    return *cell_at(l, i, loc);
}

double rv_list_get_d(RvList *l, int64_t i, const RvLoc *loc) {
    return cell_double(*cell_at(l, i, loc));
}

void rv_list_set_l(RvList *l, int64_t i, int64_t v, const RvLoc *loc) {
    *cell_at(l, i, loc) = v;
}

void rv_list_set_d(RvList *l, int64_t i, double v, const RvLoc *loc) {
    *cell_at(l, i, loc) = double_cell(v);
}

void rv_list_append_l(RvList *l, int64_t v) {
    list_append(l, v);
}

void rv_list_append_d(RvList *l, double v) {
    list_append(l, double_cell(v));
}

int64_t rv_list_pop_l(RvList *l, const RvLoc *loc) {
    return list_pop(l, loc);
}

double rv_list_pop_d(RvList *l, const RvLoc *loc) {
    return cell_double(list_pop(l, loc));
}

void rv_list_insert_l(RvList *l, int64_t i, int64_t v, const RvLoc *loc) {
    list_insert(l, i, v, loc);
}

void rv_list_insert_d(RvList *l, int64_t i, double v, const RvLoc *loc) {
    list_insert(l, i, double_cell(v), loc);
}

int64_t rv_list_remove_at_l(RvList *l, int64_t i, const RvLoc *loc) {
    return list_remove_at(l, i, loc);
}

double rv_list_remove_at_d(RvList *l, int64_t i, const RvLoc *loc) {
    return cell_double(list_remove_at(l, i, loc));
}

void rv_list_clear(RvList *l) {
    memset(l->cells->cells, 0, 8 * (size_t)l->len);
    l->len = 0;
}

rv_bool rv_list_contains_l(RvList *l, int64_t v) {
    return list_index_of(l, v) >= 0;
}

rv_bool rv_list_contains_d(RvList *l, double v) {
    return list_index_of(l, double_cell(v)) >= 0;
}

int64_t rv_list_index_of_l(RvList *l, int64_t v) {
    return list_index_of(l, v);
}

int64_t rv_list_index_of_d(RvList *l, double v) {
    return list_index_of(l, double_cell(v));
}

RvList *rv_list_slice(RvList *l, int64_t lo, int64_t hi, const RvLoc *loc) {
    if (lo < 0 || lo > hi || hi > l->len) {
        rv_panic_cstr("slice out of range", loc);
    }
    RvList *r = rv_list_new(l->elem_kind, hi - lo);
    memcpy(r->cells->cells, l->cells->cells + lo, 8 * (size_t)(hi - lo));
    r->len = hi - lo;
    return r;
}

rv_bool rv_list_eq(RvList *a, RvList *b) {
    if (a->elem_kind != b->elem_kind || a->len != b->len) {
        return 0;
    }
    for (int64_t i = 0; i < a->len; i++) {
        if (!cell_eq(a->elem_kind, a->cells->cells[i], b->cells->cells[i])) {
            return 0;
        }
    }
    return 1;
}

/* ---- boxes ------------------------------------------------------------- */

RvBox *rv_box_int(int64_t v) {
    RvBox *b = rv_alloc(&rv_ti_box_int);
    b->cell = v;
    return b;
}

RvBox *rv_box_float(double v) {
    RvBox *b = rv_alloc(&rv_ti_box_float);
    b->cell = double_cell(v);
    return b;
}

RvBox *rv_box_bool(rv_bool v) {
    RvBox *b = rv_alloc(&rv_ti_box_bool);
    b->cell = v != 0;
    return b;
}

/* ---- structural equality ----------------------------------------------- */

/* Index of the last pointer field of a struct type, or -1. */
static int64_t last_ptr_field(const RvTypeInfo *ti) {
    for (int64_t i = ti->nfields; i-- > 0;) {
        if (ti->field_kinds[i] == RV_FK_PTR) {
            return i;
        }
    }
    return -1;
}

rv_bool rv_eq(void *a, void *b) {
    /* The last pointer field of a struct is compared iteratively so that long chains do not exhaust the stack. */
    for (;;) {
        if (!a || !b) {
            return a == b;
        }
        const RvTypeInfo *ta = ((RvHeader *)a)->ti, *tb = ((RvHeader *)b)->ti;
        if (ta->kind != tb->kind) {
            return 0;
        }
        switch (ta->kind) {
        case RV_KIND_STR:
            return rv_str_eq(a, b);
        case RV_KIND_LIST:
            return rv_list_eq(a, b);
        case RV_KIND_BOX:
            return ta->field_kinds[0] == tb->field_kinds[0] &&
                   cell_eq(ta->field_kinds[0], ((RvBox *)a)->cell, ((RvBox *)b)->cell);
        case RV_KIND_STRUCT:
            break;
        default:
            return 0;
        }
        if (ta != tb) {
            return 0;
        }
        const int64_t *ca = ((RvObject *)a)->cells, *cb = ((RvObject *)b)->cells;
        int64_t tail = last_ptr_field(ta);
        for (int64_t i = 0; i < ta->nfields; i++) {
            if (i != tail && !cell_eq(ta->field_kinds[i], ca[i], cb[i])) {
                return 0;
            }
        }
        if (tail < 0) {
            return 1;
        }
        a = (void *)(uintptr_t)ca[tail];
        b = (void *)(uintptr_t)cb[tail];
    }
}

/* ---- io ---------------------------------------------------------------- */

void rv_print(RvStr *s) {
    fwrite(s->bytes, 1, (size_t)s->len, stdout);
}

void rv_println(RvStr *s) {
    fwrite(s->bytes, 1, (size_t)s->len, stdout);
    fputc('\n', stdout);
}

void rv_eprintln(RvStr *s) {
    fwrite(s->bytes, 1, (size_t)s->len, stderr);
    fputc('\n', stderr);
}

RvList *rv_args(void) {
    RvList *l = rv_list_new(RV_FK_PTR, argc_saved > 1 ? argc_saved - 1 : 0);
    for (int i = 1; i < argc_saved; i++) {
        rv_list_append_l(l, ptr_cell(rv_str_new(argv_saved[i], (int64_t)strlen(argv_saved[i]))));
    }
    return l;
}

RvStr *rv_read_file(RvStr *path) {
    FILE *f = fopen(path->bytes, "rb");
    if (!f) {
        return NULL;
    }
    size_t cap = 4096, n = 0;
    char *buf = xrealloc(NULL, cap);
    for (;;) {
        if (n == cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) {
            break;
        }
    }
    int failed = ferror(f);
    fclose(f);
    RvStr *s = failed ? NULL : rv_str_new(buf, (int64_t)n);
    free(buf);
    return s;
}

rv_bool rv_write_file(RvStr *path, RvStr *data) {
    FILE *f = fopen(path->bytes, "wb");
    if (!f) {
        return 0;
    }
    int ok = fwrite(data->bytes, 1, (size_t)data->len, f) == (size_t)data->len;
    return fclose(f) == 0 && ok;
}

RvStr *rv_read_line(void) {
    static char *buf;
    static size_t cap;
    fflush(stdout);
    ssize_t n = getline(&buf, &cap, stdin);
    if (n < 0) {
        return NULL;
    }
    if (n > 0 && buf[n - 1] == '\n') {
        n--;
        if (n > 0 && buf[n - 1] == '\r') {
            n--;
        }
    }
    return rv_str_new(buf, n);
}

/* ---- math -------------------------------------------------------------- */

double rv_sqrt(double x) {
    return sqrt(x);
}
