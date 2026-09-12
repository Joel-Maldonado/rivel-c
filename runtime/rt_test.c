/*
 * Standalone tests for the Rivel runtime. Panics are exercised by running the
 * offending call in a forked child and checking its exit status and stderr.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rivel_rt.h"

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                   \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

#define NOINLINE __attribute__((noinline))

static const RvLoc LOC = {"t.rv", 3, 7};

/* ---- helpers ----------------------------------------------------------- */

static RvStr *S(const char *lit) {
    return rv_str_new(lit, (int64_t)strlen(lit));
}

static int str_is(RvStr *s, const char *lit) {
    size_t n = strlen(lit);
    return s->len == (int64_t)n && memcmp(s->bytes, lit, n) == 0 && s->bytes[n] == 0;
}

static int64_t ptr(const void *p) {
    return (int64_t)(uintptr_t)p;
}

static RvStr *elem(RvList *l, int64_t i) {
    return (RvStr *)(uintptr_t)rv_list_get_l(l, i, &LOC);
}

typedef struct Child {
    int status;
    size_t out_len;
    char out[4096];
    char err[4096];
} Child;

static size_t read_all(int fd, char *buf, size_t cap) {
    size_t n = 0;
    for (;;) {
        ssize_t got = read(fd, buf + n, cap - 1 - n);
        if (got <= 0) {
            break;
        }
        n += (size_t)got;
    }
    buf[n] = 0;
    return n;
}

/* Runs fn in a forked child, capturing its exit status, stdout, and stderr. */
static void run_child(void (*fn)(void), Child *c) {
    int outp[2], errp[2];
    CHECK(pipe(outp) == 0 && pipe(errp) == 0);
    rv_flush();
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        dup2(outp[1], 1);
        dup2(errp[1], 2);
        close(outp[0]);
        close(outp[1]);
        close(errp[0]);
        close(errp[1]);
        fn();
        exit(0);
    }
    close(outp[1]);
    close(errp[1]);
    c->out_len = read_all(outp[0], c->out, sizeof c->out);
    read_all(errp[0], c->err, sizeof c->err);
    close(outp[0]);
    close(errp[0]);
    CHECK(waitpid(pid, &c->status, 0) == pid);
}

static void expect_panic_at(void (*fn)(void), const char *msg, int has_loc) {
    Child c;
    char expect[256];
    snprintf(expect, sizeof expect, has_loc ? "panic: %s\n  at t.rv:3:7\n" : "panic: %s\n", msg);
    run_child(fn, &c);
    if (!WIFEXITED(c.status) || WEXITSTATUS(c.status) != 101 || strcmp(c.err, expect) != 0) {
        fprintf(stderr, "expected panic \"%s\", got status %d and stderr:\n%s", msg, c.status, c.err);
        exit(1);
    }
}

#define expect_panic(fn, msg) expect_panic_at(fn, msg, 1)

#define PANIC_CASE(name, expr)                                                                                         \
    static void name(void) {                                                                                           \
        (void)(expr);                                                                                                  \
    }

/*
 * Overwrites the stack region below the caller so stale pointers in dead frames cannot act as roots. Left
 * uninstrumented so that ASan does not put a redzone gap, holding exactly such pointers, above the buffer.
 */
__attribute__((noinline, no_sanitize_address)) static void clobber_stack(void) {
    volatile char buf[1 << 16];
    for (size_t i = 0; i < sizeof buf; i++) {
        buf[i] = 0;
    }
}

/* ---- panics ------------------------------------------------------------ */

static void panic_no_loc(void) {
    rv_panic_cstr("boom", NULL);
}

static void panic_str_after_print(void) {
    rv_print(S("buffered"));
    rv_panic(S("boom"), &LOC);
}

PANIC_CASE(p_assert, rv_assert(0, &LOC))

static void exit_after_print(void) {
    rv_print(S("bye"));
    rv_exit(7);
}

static void test_panics(void) {
    expect_panic_at(panic_no_loc, "boom", 0);
    expect_panic(p_assert, "assertion failed");
    rv_assert(1, &LOC);

    Child c;
    run_child(panic_str_after_print, &c);
    CHECK(WIFEXITED(c.status) && WEXITSTATUS(c.status) == 101);
    CHECK(strcmp(c.out, "buffered") == 0);
    CHECK(strcmp(c.err, "panic: boom\n  at t.rv:3:7\n") == 0);

    run_child(exit_after_print, &c);
    CHECK(WIFEXITED(c.status) && WEXITSTATUS(c.status) == 7);
    CHECK(strcmp(c.out, "bye") == 0);
}

/* ---- arithmetic -------------------------------------------------------- */

PANIC_CASE(p_add, rv_add(INT64_MAX, 1, &LOC))
PANIC_CASE(p_add_neg, rv_add(INT64_MIN, -1, &LOC))
PANIC_CASE(p_sub, rv_sub(INT64_MIN, 1, &LOC))
PANIC_CASE(p_mul, rv_mul(INT64_MAX, 2, &LOC))
PANIC_CASE(p_mul_neg, rv_mul(INT64_MIN, -1, &LOC))
PANIC_CASE(p_neg, rv_neg(INT64_MIN, &LOC))
PANIC_CASE(p_abs, rv_abs(INT64_MIN, &LOC))
PANIC_CASE(p_div_zero, rv_div(1, 0, &LOC))
PANIC_CASE(p_div_min, rv_div(INT64_MIN, -1, &LOC))
PANIC_CASE(p_mod_zero, rv_mod(1, 0, &LOC))
PANIC_CASE(p_shl_64, rv_shl(1, 64, &LOC))
PANIC_CASE(p_shl_neg, rv_shl(1, -1, &LOC))
PANIC_CASE(p_shr_64, rv_shr(1, 64, &LOC))
PANIC_CASE(p_shr_neg, rv_shr(1, -1, &LOC))
PANIC_CASE(p_f2i_nan, rv_float_to_int(NAN, &LOC))
PANIC_CASE(p_f2i_inf, rv_float_to_int(INFINITY, &LOC))
PANIC_CASE(p_f2i_ninf, rv_float_to_int(-INFINITY, &LOC))
PANIC_CASE(p_f2i_big, rv_float_to_int(0x1p63, &LOC))
PANIC_CASE(p_f2i_small, rv_float_to_int(-0x1p63 * 1.0000000000000002, &LOC))

static void test_arithmetic(void) {
    CHECK(rv_add(2, 3, &LOC) == 5);
    CHECK(rv_add(INT64_MAX, 0, &LOC) == INT64_MAX);
    CHECK(rv_sub(2, 3, &LOC) == -1);
    CHECK(rv_sub(INT64_MIN, 0, &LOC) == INT64_MIN);
    CHECK(rv_mul(-4, 5, &LOC) == -20);
    CHECK(rv_mul(INT64_MIN, 1, &LOC) == INT64_MIN);
    CHECK(rv_neg(5, &LOC) == -5);
    CHECK(rv_neg(INT64_MAX, &LOC) == -INT64_MAX);
    CHECK(rv_abs(-5, &LOC) == 5);
    CHECK(rv_abs(7, &LOC) == 7);
    CHECK(rv_abs(INT64_MIN + 1, &LOC) == INT64_MAX);

    CHECK(rv_div(7, 2, &LOC) == 3);
    CHECK(rv_div(-7, 2, &LOC) == -4);
    CHECK(rv_div(7, -2, &LOC) == -4);
    CHECK(rv_div(-7, -2, &LOC) == 3);
    CHECK(rv_div(6, 3, &LOC) == 2);
    CHECK(rv_div(-6, 3, &LOC) == -2);
    CHECK(rv_div(0, -5, &LOC) == 0);
    CHECK(rv_div(INT64_MIN, 1, &LOC) == INT64_MIN);
    CHECK(rv_div(INT64_MAX, -1, &LOC) == -INT64_MAX);
    CHECK(rv_mod(7, 2, &LOC) == 1);
    CHECK(rv_mod(-7, 2, &LOC) == 1);
    CHECK(rv_mod(7, -2, &LOC) == -1);
    CHECK(rv_mod(-7, -2, &LOC) == -1);
    CHECK(rv_mod(6, 3, &LOC) == 0);
    CHECK(rv_mod(-6, 3, &LOC) == 0);
    CHECK(rv_mod(INT64_MIN, -1, &LOC) == 0);
    CHECK(rv_mod(INT64_MIN, 3, &LOC) == 1);
    CHECK(rv_mod(INT64_MAX, -1, &LOC) == 0);

    CHECK(rv_shl(1, 0, &LOC) == 1);
    CHECK(rv_shl(1, 63, &LOC) == INT64_MIN);
    CHECK(rv_shl(-1, 1, &LOC) == -2);
    CHECK(rv_shl(3, 62, &LOC) == INT64_MIN / 2);
    CHECK(rv_shr(-8, 1, &LOC) == -4);
    CHECK(rv_shr(-1, 63, &LOC) == -1);
    CHECK(rv_shr(INT64_MIN, 63, &LOC) == -1);
    CHECK(rv_shr(INT64_MAX, 62, &LOC) == 1);
    CHECK(rv_shr(-7, 1, &LOC) == -4);
    CHECK(rv_shr(5, 0, &LOC) == 5);

    CHECK(rv_float_to_int(2.7, &LOC) == 2);
    CHECK(rv_float_to_int(-2.7, &LOC) == -2);
    CHECK(rv_float_to_int(-0.0, &LOC) == 0);
    CHECK(rv_float_to_int(-0x1p63, &LOC) == INT64_MIN);
    CHECK(rv_float_to_int(nextafter(0x1p63, 0), &LOC) == INT64_C(9223372036854774784));
    CHECK(rv_sqrt(4.0) == 2.0);
    CHECK(isnan(rv_sqrt(-1.0)));

    expect_panic(p_add, "integer overflow");
    expect_panic(p_add_neg, "integer overflow");
    expect_panic(p_sub, "integer overflow");
    expect_panic(p_mul, "integer overflow");
    expect_panic(p_mul_neg, "integer overflow");
    expect_panic(p_neg, "integer overflow");
    expect_panic(p_abs, "integer overflow");
    expect_panic(p_div_zero, "division by zero");
    expect_panic(p_div_min, "integer overflow");
    expect_panic(p_mod_zero, "division by zero");
    expect_panic(p_shl_64, "shift amount out of range");
    expect_panic(p_shl_neg, "shift amount out of range");
    expect_panic(p_shr_64, "shift amount out of range");
    expect_panic(p_shr_neg, "shift amount out of range");
    expect_panic(p_f2i_nan, "float to int conversion out of range");
    expect_panic(p_f2i_inf, "float to int conversion out of range");
    expect_panic(p_f2i_ninf, "float to int conversion out of range");
    expect_panic(p_f2i_big, "float to int conversion out of range");
    expect_panic(p_f2i_small, "float to int conversion out of range");
}

/* ---- strings ----------------------------------------------------------- */

PANIC_CASE(p_index_hi, rv_str_index(S("abc"), 3, &LOC))
PANIC_CASE(p_index_neg, rv_str_index(S("abc"), -1, &LOC))
PANIC_CASE(p_index_empty, rv_str_index(rv_str_empty(), 0, &LOC))
PANIC_CASE(p_slice_rev, rv_str_slice(S("abc"), 2, 1, &LOC))
PANIC_CASE(p_slice_hi, rv_str_slice(S("abc"), 0, 4, &LOC))
PANIC_CASE(p_slice_neg, rv_str_slice(S("abc"), -1, 0, &LOC))
PANIC_CASE(p_repeat_neg, rv_str_repeat(S("ab"), -1, &LOC))
PANIC_CASE(p_repeat_huge, rv_str_repeat(S("ab"), INT64_MAX, &LOC))
PANIC_CASE(p_ord_empty, rv_ord(rv_str_empty(), &LOC))
PANIC_CASE(p_ord_long, rv_ord(S("ab"), &LOC))
PANIC_CASE(p_chr_hi, rv_chr(256, &LOC))
PANIC_CASE(p_chr_neg, rv_chr(-1, &LOC))

static void check_split(const char *s, const char *sep, const char *const *parts, int64_t n) {
    RvList *l = rv_str_split(S(s), S(sep));
    CHECK(l->elem_kind == RV_FK_PTR);
    CHECK(l->len == n);
    for (int64_t i = 0; i < n; i++) {
        CHECK(str_is(elem(l, i), parts[i]));
    }
    if (strlen(sep) > 0) {
        CHECK(str_is(rv_str_join(S(sep), l), s));
    }
}

static void test_str_basics(void) {
    RvStr *e = rv_str_empty();
    CHECK(e->len == 0 && e->bytes[0] == 0 && (e->h.flags & RV_F_STATIC) && e->h.ti == &rv_ti_str);
    CHECK(rv_str_new("", 0) == e);
    CHECK(rv_str_concat(e, e) == e);

    RvStr *s = rv_str_new("abc\0def", 7);
    CHECK(s->len == 7 && s->bytes[3] == 0 && s->bytes[7] == 0 && memcmp(s->bytes, "abc\0def", 7) == 0);
    CHECK(s->h.ti == &rv_ti_str && s->h.flags == 0);

    CHECK(str_is(rv_str_concat(S("ab"), S("cd")), "abcd"));
    CHECK(str_is(rv_str_concat(S("ab"), e), "ab"));
    CHECK(str_is(rv_str_concat(e, S("cd")), "cd"));

    CHECK(rv_str_eq(S("abc"), S("abc")));
    CHECK(!rv_str_eq(S("abc"), S("abd")));
    CHECK(!rv_str_eq(S("abc"), S("ab")));
    CHECK(rv_str_eq(e, S("")));
    CHECK(rv_str_cmp(S("a"), S("b")) == -1);
    CHECK(rv_str_cmp(S("b"), S("a")) == 1);
    CHECK(rv_str_cmp(S("ab"), S("a")) == 1);
    CHECK(rv_str_cmp(S("a"), S("ab")) == -1);
    CHECK(rv_str_cmp(S("abc"), S("abc")) == 0);
    CHECK(rv_str_cmp(e, e) == 0);
    CHECK(rv_str_cmp(S("\xff"), S("a")) == 1);

    RvStr *ab = S("aab");
    RvStr *a0 = rv_str_index(ab, 0, &LOC);
    CHECK(str_is(a0, "a") && (a0->h.flags & RV_F_STATIC));
    CHECK(rv_str_index(ab, 1, &LOC) == a0);
    CHECK(str_is(rv_str_index(ab, 2, &LOC), "b"));
    CHECK(rv_str_index(S("\xff"), 0, &LOC)->bytes[0] == '\xff');
    CHECK(rv_chr(97, &LOC) == a0);
    CHECK(rv_chr(0, &LOC)->len == 1 && rv_chr(0, &LOC)->bytes[0] == 0);
    CHECK(str_is(rv_chr(255, &LOC), "\xff"));
    CHECK(rv_ord(S("A"), &LOC) == 65);
    CHECK(rv_ord(S("\xff"), &LOC) == 255);
    CHECK(rv_ord(rv_chr(0, &LOC), &LOC) == 0);

    CHECK(rv_str_slice(S("abc"), 0, 0, &LOC) == e);
    CHECK(rv_str_slice(S("abc"), 3, 3, &LOC) == e);
    CHECK(str_is(rv_str_slice(S("abc"), 0, 3, &LOC), "abc"));
    CHECK(str_is(rv_str_slice(S("abcde"), 1, 3, &LOC), "bc"));

    expect_panic(p_index_hi, "index out of range");
    expect_panic(p_index_neg, "index out of range");
    expect_panic(p_index_empty, "index out of range");
    expect_panic(p_slice_rev, "slice out of range");
    expect_panic(p_slice_hi, "slice out of range");
    expect_panic(p_slice_neg, "slice out of range");
    expect_panic(p_ord_empty, "ord expects a one-byte string");
    expect_panic(p_ord_long, "ord expects a one-byte string");
    expect_panic(p_chr_hi, "chr out of range");
    expect_panic(p_chr_neg, "chr out of range");
}

static void test_str_search(void) {
    RvStr *e = rv_str_empty();
    CHECK(rv_str_contains(S("ababa"), S("aba")));
    CHECK(rv_str_contains(S("abc"), e));
    CHECK(rv_str_contains(e, e));
    CHECK(!rv_str_contains(e, S("a")));
    CHECK(!rv_str_contains(S("abc"), S("abcd")));
    CHECK(!rv_str_contains(S("abc"), S("ac")));
    CHECK(rv_str_find(S("ababa"), S("aba")) == 0);
    CHECK(rv_str_find(S("ababa"), S("ba")) == 1);
    CHECK(rv_str_find(S("ababa"), S("bab")) == 1);
    CHECK(rv_str_find(S("abcabc"), S("cab")) == 2);
    CHECK(rv_str_find(S("abc"), S("c")) == 2);
    CHECK(rv_str_find(S("abc"), S("d")) == -1);
    CHECK(rv_str_find(S("abc"), e) == 0);
    CHECK(rv_str_find(e, e) == 0);
    CHECK(rv_str_find(S("aaab"), S("aab")) == 1);
    CHECK(rv_str_find(rv_str_new("a\0b", 3), rv_str_new("\0b", 2)) == 1);
    CHECK(rv_str_starts_with(S("abc"), S("ab")));
    CHECK(rv_str_starts_with(S("abc"), e));
    CHECK(!rv_str_starts_with(S("abc"), S("bc")));
    CHECK(!rv_str_starts_with(S("a"), S("ab")));
    CHECK(rv_str_ends_with(S("abc"), S("bc")));
    CHECK(rv_str_ends_with(S("abc"), e));
    CHECK(rv_str_ends_with(S("abc"), S("abc")));
    CHECK(!rv_str_ends_with(S("abc"), S("ab")));
    CHECK(!rv_str_ends_with(S("c"), S("bc")));
}

static void test_str_split_join(void) {
    static const char *const a_b[] = {"a", "", "b"};
    static const char *const one_empty[] = {""};
    static const char *const bytes[] = {"a", "b", "c"};
    static const char *const edges[] = {"", "a", ""};
    static const char *const dashes[] = {"a", "b", "c"};
    static const char *const seps_only[] = {"", "", ""};
    static const char *const overlap[] = {"", "a"};
    check_split("a,,b", ",", a_b, 3);
    check_split("", ",", one_empty, 1);
    check_split("abc", "", bytes, 3);
    check_split("", "", NULL, 0);
    check_split(",a,", ",", edges, 3);
    check_split("a--b--c", "--", dashes, 3);
    check_split(",,", ",", seps_only, 3);
    check_split("aaa", "aa", overlap, 2);

    RvList *parts = rv_str_split(S("xyz"), rv_str_empty());
    CHECK(elem(parts, 0) == rv_str_index(S("x"), 0, &LOC));

    RvList *empty = rv_list_new(RV_FK_PTR, 0);
    CHECK(rv_str_join(S(","), empty) == rv_str_empty());
    rv_list_append_l(empty, ptr(S("solo")));
    CHECK(str_is(rv_str_join(S(", "), empty), "solo"));
    rv_list_append_l(empty, ptr(S("duo")));
    CHECK(str_is(rv_str_join(S(", "), empty), "solo, duo"));
    CHECK(str_is(rv_str_join(rv_str_empty(), empty), "soloduo"));
    rv_list_append_l(empty, ptr(rv_str_empty()));
    CHECK(str_is(rv_str_join(S("-"), empty), "solo-duo-"));
}

static void test_str_transform(void) {
    CHECK(str_is(rv_str_trim(S("  a b \t\r\n\v\f")), "a b"));
    CHECK(rv_str_trim(rv_str_empty()) == rv_str_empty());
    CHECK(rv_str_trim(S("   ")) == rv_str_empty());
    CHECK(str_is(rv_str_trim(S("abc")), "abc"));
    CHECK(str_is(rv_str_upper(S("aBc1!\xe9")), "ABC1!\xe9"));
    CHECK(str_is(rv_str_lower(S("aBc1!\xe9")), "abc1!\xe9"));
    CHECK(rv_str_upper(rv_str_empty()) == rv_str_empty());

    RvStr *abc = S("abc");
    CHECK(rv_str_replace(abc, rv_str_empty(), S("x")) == abc);
    CHECK(rv_str_replace(abc, S("z"), S("x")) == abc);
    CHECK(str_is(rv_str_replace(S("aaa"), S("a"), S("b")), "bbb"));
    CHECK(str_is(rv_str_replace(S("aaa"), S("aa"), S("b")), "ba"));
    CHECK(str_is(rv_str_replace(S("hello world"), S("o"), rv_str_empty()), "hell wrld"));
    CHECK(str_is(rv_str_replace(S("ab"), S("ab"), S("xyz")), "xyz"));
    CHECK(str_is(rv_str_replace(S("a.b.c"), S("."), S("::")), "a::b::c"));
    CHECK(rv_str_replace(S("aa"), S("a"), rv_str_empty()) == rv_str_empty());

    CHECK(str_is(rv_str_repeat(S("ab"), 3, &LOC), "ababab"));
    CHECK(rv_str_repeat(S("ab"), 0, &LOC) == rv_str_empty());
    CHECK(rv_str_repeat(rv_str_empty(), 5, &LOC) == rv_str_empty());
    CHECK(str_is(rv_str_repeat(S("x"), 1, &LOC), "x"));
    expect_panic(p_repeat_neg, "negative repeat count");
    expect_panic(p_repeat_huge, "integer overflow");
}

static void check_float_str(double v, const char *expect) {
    RvStr *s = rv_str_from_float(v);
    CHECK(str_is(s, expect));
    if (!isnan(v)) {
        CHECK(strtod(s->bytes, NULL) == v);
    }
}

static void test_str_conversions(void) {
    CHECK(str_is(rv_str_from_int(0), "0"));
    CHECK(str_is(rv_str_from_int(-1), "-1"));
    CHECK(str_is(rv_str_from_int(INT64_MIN), "-9223372036854775808"));
    CHECK(str_is(rv_str_from_int(INT64_MAX), "9223372036854775807"));

    check_float_str(0.1, "0.1");
    check_float_str(3.0, "3.0");
    check_float_str(1e21, "1e+21");
    check_float_str(1e-7, "1e-07");
    check_float_str(-0.0, "-0.0");
    check_float_str(0.0, "0.0");
    check_float_str(2.5, "2.5");
    check_float_str(-123456.0, "-123456.0");
    check_float_str(1e16, "1e+16");
    check_float_str(0.30000000000000004, "0.30000000000000004");
    check_float_str(1.0 / 3.0, "0.3333333333333333");
    check_float_str(5e-324, "4.94065645841247e-324"); /* %.15g round-trips first; not the true shortest */
    check_float_str(1.7976931348623157e308, "1.7976931348623157e+308");
    check_float_str(NAN, "nan");
    check_float_str(INFINITY, "inf");
    check_float_str(-INFINITY, "-inf");
    for (double v = 1.0; v < 1e30; v *= 3.7) {
        CHECK(strtod(rv_str_from_float(v)->bytes, NULL) == v);
        CHECK(strtod(rv_str_from_float(-1.0 / v)->bytes, NULL) == -1.0 / v);
    }

    CHECK(str_is(rv_str_from_bool(1), "true"));
    CHECK(str_is(rv_str_from_bool(0), "false"));
    CHECK(rv_str_from_bool(1)->h.flags & RV_F_STATIC);

    RvBox *b = rv_str_parse_int(S("+5"));
    CHECK(b && b->h.ti == &rv_ti_box_int && b->cell == 5);
    CHECK(rv_str_parse_int(S("-0"))->cell == 0);
    CHECK(rv_str_parse_int(S("0"))->cell == 0);
    CHECK(rv_str_parse_int(S("007"))->cell == 7);
    CHECK(rv_str_parse_int(S("-42"))->cell == -42);
    CHECK(rv_str_parse_int(S("9223372036854775807"))->cell == INT64_MAX);
    CHECK(rv_str_parse_int(S("-9223372036854775808"))->cell == INT64_MIN);
    CHECK(rv_str_parse_int(S("99999999999999999999")) == NULL);
    CHECK(rv_str_parse_int(S("9223372036854775808")) == NULL);
    CHECK(rv_str_parse_int(S("-9223372036854775809")) == NULL);
    CHECK(rv_str_parse_int(rv_str_empty()) == NULL);
    CHECK(rv_str_parse_int(S("1 ")) == NULL);
    CHECK(rv_str_parse_int(S(" 1")) == NULL);
    CHECK(rv_str_parse_int(S("-")) == NULL);
    CHECK(rv_str_parse_int(S("+")) == NULL);
    CHECK(rv_str_parse_int(S("+-1")) == NULL);
    CHECK(rv_str_parse_int(S("1_000")) == NULL);
    CHECK(rv_str_parse_int(S("0x10")) == NULL);
    CHECK(rv_str_parse_int(S("1.0")) == NULL);
    CHECK(rv_str_parse_int(rv_str_new("1\0", 2)) == NULL);

    RvBox *f = rv_str_parse_float(S("1.5"));
    CHECK(f && f->h.ti == &rv_ti_box_float);
    double d;
    memcpy(&d, &f->cell, sizeof d);
    CHECK(d == 1.5);
    memcpy(&d, &rv_str_parse_float(S("-2e3"))->cell, sizeof d);
    CHECK(d == -2000.0);
    memcpy(&d, &rv_str_parse_float(S(".5"))->cell, sizeof d);
    CHECK(d == 0.5);
    memcpy(&d, &rv_str_parse_float(S("inf"))->cell, sizeof d);
    CHECK(isinf(d) && d > 0);
    memcpy(&d, &rv_str_parse_float(S("nan"))->cell, sizeof d);
    CHECK(isnan(d));
    memcpy(&d, &rv_str_parse_float(S("1e400"))->cell, sizeof d);
    CHECK(isinf(d));
    CHECK(rv_str_parse_float(rv_str_empty()) == NULL);
    CHECK(rv_str_parse_float(S(" 1")) == NULL);
    CHECK(rv_str_parse_float(S("1 ")) == NULL);
    CHECK(rv_str_parse_float(S("\t1")) == NULL);
    CHECK(rv_str_parse_float(S("abc")) == NULL);
    CHECK(rv_str_parse_float(S("1.5x")) == NULL);
    CHECK(rv_str_parse_float(S("-")) == NULL);
    CHECK(rv_str_parse_float(rv_str_new("1\0", 2)) == NULL);
}

/* ---- lists ------------------------------------------------------------- */

static RvList *ints(int64_t n) {
    RvList *l = rv_list_new(RV_FK_INT, 0);
    for (int64_t i = 0; i < n; i++) {
        rv_list_append_l(l, i * 10);
    }
    return l;
}

PANIC_CASE(p_get_hi, rv_list_get_l(ints(3), 3, &LOC))
PANIC_CASE(p_get_neg, rv_list_get_l(ints(3), -1, &LOC))
PANIC_CASE(p_get_d_hi, rv_list_get_d(ints(3), 3, &LOC))
PANIC_CASE(p_set_hi, rv_list_set_l(ints(3), 3, 1, &LOC))
PANIC_CASE(p_set_d_neg, rv_list_set_d(ints(3), -1, 1.0, &LOC))
PANIC_CASE(p_pop_empty, rv_list_pop_l(ints(0), &LOC))
PANIC_CASE(p_pop_d_empty, rv_list_pop_d(ints(0), &LOC))
PANIC_CASE(p_insert_hi, rv_list_insert_l(ints(3), 4, 1, &LOC))
PANIC_CASE(p_insert_neg, rv_list_insert_d(ints(3), -1, 1.0, &LOC))
PANIC_CASE(p_remove_empty, rv_list_remove_at_l(ints(0), 0, &LOC))
PANIC_CASE(p_remove_hi, rv_list_remove_at_d(ints(3), 3, &LOC))
PANIC_CASE(p_lslice_rev, rv_list_slice(ints(3), 2, 1, &LOC))
PANIC_CASE(p_lslice_hi, rv_list_slice(ints(3), 0, 4, &LOC))
PANIC_CASE(p_lslice_neg, rv_list_slice(ints(3), -1, 1, &LOC))

static void test_list_int(void) {
    RvList *l = rv_list_new(RV_FK_INT, 0);
    CHECK(l->h.ti == &rv_ti_list && l->len == 0 && l->cap == 8 && l->elem_kind == RV_FK_INT);
    CHECK(l->cells->h.ti == &rv_ti_cells_scalar && l->cells->n == 8);
    CHECK(rv_list_new(RV_FK_INT, 3)->cap == 8);
    CHECK(rv_list_new(RV_FK_INT, 9)->cap == 9);

    for (int64_t i = 0; i < 100; i++) {
        rv_list_append_l(l, i);
    }
    CHECK(l->len == 100 && l->cap == 128 && l->cells->n == 128);
    CHECK(rv_list_get_l(l, 0, &LOC) == 0 && rv_list_get_l(l, 99, &LOC) == 99);
    rv_list_set_l(l, 5, -5, &LOC);
    CHECK(rv_list_get_l(l, 5, &LOC) == -5);
    CHECK(rv_list_pop_l(l, &LOC) == 99 && l->len == 99);

    rv_list_insert_l(l, 0, 1000, &LOC);
    CHECK(l->len == 100 && rv_list_get_l(l, 0, &LOC) == 1000 && rv_list_get_l(l, 1, &LOC) == 0);
    rv_list_insert_l(l, 100, 2000, &LOC);
    CHECK(l->len == 101 && rv_list_get_l(l, 100, &LOC) == 2000 && rv_list_get_l(l, 99, &LOC) == 98);
    rv_list_insert_l(l, 50, 3000, &LOC);
    CHECK(l->len == 102 && rv_list_get_l(l, 50, &LOC) == 3000 && rv_list_get_l(l, 51, &LOC) == 49);
    CHECK(rv_list_remove_at_l(l, 50, &LOC) == 3000 && rv_list_get_l(l, 50, &LOC) == 49);
    CHECK(rv_list_remove_at_l(l, 0, &LOC) == 1000 && rv_list_get_l(l, 0, &LOC) == 0);
    CHECK(rv_list_remove_at_l(l, 99, &LOC) == 2000 && l->len == 99);
    CHECK(rv_list_get_l(l, 98, &LOC) == 98);

    CHECK(rv_list_contains_l(l, 42) && !rv_list_contains_l(l, 99));
    CHECK(rv_list_index_of_l(l, 42) == 42 && rv_list_index_of_l(l, -5) == 5 && rv_list_index_of_l(l, 99) == -1);

    RvList *s = rv_list_slice(l, 2, 5, &LOC);
    CHECK(s->len == 3 && s->elem_kind == RV_FK_INT && rv_list_get_l(s, 0, &LOC) == 2 && rv_list_get_l(s, 2, &LOC) == 4);
    CHECK(rv_list_slice(l, 0, 0, &LOC)->len == 0);
    CHECK(rv_list_slice(l, 99, 99, &LOC)->len == 0);
    CHECK(rv_list_slice(l, 0, 99, &LOC)->len == 99);
    CHECK(rv_list_eq(rv_list_slice(l, 0, 99, &LOC), l));

    RvList *single = rv_list_new(RV_FK_INT, 0);
    rv_list_insert_l(single, 0, 7, &LOC);
    CHECK(single->len == 1 && rv_list_pop_l(single, &LOC) == 7 && single->len == 0);
    rv_list_append_l(single, 8);
    CHECK(rv_list_remove_at_l(single, 0, &LOC) == 8 && single->len == 0);

    rv_list_clear(l);
    CHECK(l->len == 0 && l->cap == 128);
    CHECK(!rv_list_contains_l(l, 0));

    RvList *bools = rv_list_new(RV_FK_BOOL, 0);
    rv_list_append_l(bools, 0);
    rv_list_append_l(bools, 1);
    CHECK(rv_list_contains_l(bools, 1) && rv_list_index_of_l(bools, 1) == 1);

    expect_panic(p_get_hi, "index out of range");
    expect_panic(p_get_neg, "index out of range");
    expect_panic(p_get_d_hi, "index out of range");
    expect_panic(p_set_hi, "index out of range");
    expect_panic(p_set_d_neg, "index out of range");
    expect_panic(p_pop_empty, "pop from empty list");
    expect_panic(p_pop_d_empty, "pop from empty list");
    expect_panic(p_insert_hi, "index out of range");
    expect_panic(p_insert_neg, "index out of range");
    expect_panic(p_remove_empty, "index out of range");
    expect_panic(p_remove_hi, "index out of range");
    expect_panic(p_lslice_rev, "slice out of range");
    expect_panic(p_lslice_hi, "slice out of range");
    expect_panic(p_lslice_neg, "slice out of range");
}

static void test_list_float(void) {
    RvList *l = rv_list_new(RV_FK_FLOAT, 0);
    CHECK(l->cells->h.ti == &rv_ti_cells_scalar);
    rv_list_append_d(l, 1.5);
    rv_list_append_d(l, NAN);
    rv_list_append_d(l, -0.0);
    CHECK(l->len == 3);
    CHECK(rv_list_get_d(l, 0, &LOC) == 1.5);
    CHECK(isnan(rv_list_get_d(l, 1, &LOC)));
    CHECK(signbit(rv_list_get_d(l, 2, &LOC)));
    CHECK(!rv_list_contains_d(l, NAN));
    CHECK(rv_list_index_of_d(l, NAN) == -1);
    CHECK(rv_list_contains_d(l, 0.0) && rv_list_index_of_d(l, 0.0) == 2);
    CHECK(rv_list_index_of_d(l, 1.5) == 0);
    CHECK(!rv_list_contains_d(l, 2.5));

    rv_list_set_d(l, 1, 2.25, &LOC);
    CHECK(rv_list_get_d(l, 1, &LOC) == 2.25);
    rv_list_insert_d(l, 0, -7.0, &LOC);
    rv_list_insert_d(l, 4, 9.0, &LOC);
    CHECK(l->len == 5 && rv_list_get_d(l, 0, &LOC) == -7.0 && rv_list_get_d(l, 4, &LOC) == 9.0);
    CHECK(rv_list_pop_d(l, &LOC) == 9.0);
    CHECK(rv_list_remove_at_d(l, 0, &LOC) == -7.0);
    CHECK(rv_list_remove_at_d(l, 2, &LOC) == 0.0 && l->len == 2);
    CHECK(rv_list_get_d(l, 0, &LOC) == 1.5 && rv_list_get_d(l, 1, &LOC) == 2.25);

    RvList *m = rv_list_slice(l, 0, 2, &LOC);
    CHECK(m->elem_kind == RV_FK_FLOAT && rv_list_eq(l, m));
    rv_list_append_d(l, NAN);
    rv_list_append_d(m, NAN);
    CHECK(!rv_list_eq(l, m));
}

static void test_list_ptr(void) {
    RvList *l = rv_list_new(RV_FK_PTR, 0);
    CHECK(l->cells->h.ti == &rv_ti_cells_ptr);
    rv_list_append_l(l, ptr(S("a")));
    rv_list_append_l(l, ptr(S("b")));
    rv_list_append_l(l, 0);
    CHECK(rv_list_contains_l(l, ptr(S("b"))));
    CHECK(rv_list_index_of_l(l, ptr(S("b"))) == 1);
    CHECK(rv_list_index_of_l(l, ptr(S("c"))) == -1);
    CHECK(rv_list_index_of_l(l, 0) == 2);
    CHECK(rv_list_contains_l(l, ptr(rv_str_index(S("a"), 0, &LOC))));

    RvList *m = rv_list_new(RV_FK_PTR, 0);
    rv_list_append_l(m, ptr(S("a")));
    rv_list_append_l(m, ptr(S("b")));
    rv_list_append_l(m, 0);
    CHECK(rv_list_eq(l, m));
    rv_list_set_l(m, 2, ptr(S("c")), &LOC);
    CHECK(!rv_list_eq(l, m));
    CHECK(!rv_list_eq(l, ints(3)));
    CHECK(!rv_list_eq(ints(3), ints(4)));
    CHECK(rv_list_eq(ints(4), ints(4)));
    CHECK(rv_list_eq(ints(0), rv_list_new(RV_FK_INT, 0)));

    RvList *outer_a = rv_list_new(RV_FK_PTR, 0);
    RvList *outer_b = rv_list_new(RV_FK_PTR, 0);
    rv_list_append_l(outer_a, ptr(ints(3)));
    rv_list_append_l(outer_b, ptr(ints(3)));
    CHECK(rv_list_eq(outer_a, outer_b));
    CHECK(rv_list_contains_l(outer_a, ptr(ints(3))));
    CHECK(!rv_list_contains_l(outer_a, ptr(ints(2))));
}

/* ---- boxes and structural equality ------------------------------------- */

typedef struct Vec {
    RvHeader h;
    int64_t x;
    int64_t y;
} Vec;

typedef struct Seg {
    RvHeader h;
    RvStr *name;
    Vec *from;
    Vec *to;
    int64_t id;
    int64_t ok;
} Seg;

static const uint8_t vec_kinds[2] = {RV_FK_FLOAT, RV_FK_FLOAT};
static const RvTypeInfo vec_ti = {"Vec", RV_KIND_STRUCT, 2, vec_kinds};
static const RvTypeInfo vec2_ti = {"Vec2", RV_KIND_STRUCT, 2, vec_kinds};
static const uint8_t seg_kinds[5] = {RV_FK_PTR, RV_FK_PTR, RV_FK_PTR, RV_FK_INT, RV_FK_BOOL};
static const RvTypeInfo seg_ti = {"Seg", RV_KIND_STRUCT, 5, seg_kinds};

static int64_t bits(double d) {
    int64_t b;
    memcpy(&b, &d, sizeof b);
    return b;
}

static Vec *vec(const RvTypeInfo *ti, double x, double y) {
    Vec *v = rv_alloc(ti);
    CHECK(v->h.ti == ti && v->h.flags == 0 && v->x == 0 && v->y == 0);
    v->x = bits(x);
    v->y = bits(y);
    return v;
}

static Seg *seg(const char *name, Vec *from, Vec *to, int64_t id, int64_t ok) {
    Seg *s = rv_alloc(&seg_ti);
    s->name = S(name);
    s->from = from;
    s->to = to;
    s->id = id;
    s->ok = ok;
    return s;
}

static void test_boxes(void) {
    RvBox *i = rv_box_int(-3);
    CHECK(i->h.ti == &rv_ti_box_int && i->cell == -3);
    RvBox *f = rv_box_float(2.5);
    CHECK(f->h.ti == &rv_ti_box_float && f->cell == bits(2.5));
    RvBox *b = rv_box_bool(7);
    CHECK(b->h.ti == &rv_ti_box_bool && b->cell == 1);
    CHECK(rv_box_bool(0)->cell == 0);

    CHECK(rv_ti_box_int.kind == RV_KIND_BOX && rv_ti_box_int.nfields == 1 && rv_ti_box_int.field_kinds[0] == RV_FK_INT);
    CHECK(rv_ti_box_float.field_kinds[0] == RV_FK_FLOAT && rv_ti_box_bool.field_kinds[0] == RV_FK_BOOL);
    CHECK(rv_ti_str.kind == RV_KIND_STR && rv_ti_list.kind == RV_KIND_LIST);
    CHECK(rv_ti_cells_scalar.kind == RV_KIND_CELLS && rv_ti_cells_ptr.kind == RV_KIND_CELLS);

    CHECK(rv_eq(i, rv_box_int(-3)));
    CHECK(!rv_eq(i, rv_box_int(3)));
    CHECK(!rv_eq(rv_box_int(3), rv_box_float(3.0)));
    CHECK(!rv_eq(rv_box_int(1), rv_box_bool(1)));
    CHECK(rv_eq(f, rv_box_float(2.5)));
    CHECK(rv_eq(rv_box_float(0.0), rv_box_float(-0.0)));
    RvBox *nan = rv_box_float(NAN);
    CHECK(!rv_eq(nan, nan));
    CHECK(rv_eq(b, rv_box_bool(1)));
    CHECK(!rv_eq(b, rv_box_bool(0)));
}

static void test_eq(void) {
    CHECK(rv_eq(NULL, NULL));
    CHECK(!rv_eq(NULL, S("a")));
    CHECK(!rv_eq(S("a"), NULL));
    CHECK(rv_eq(S("abc"), S("abc")));
    CHECK(!rv_eq(S("abc"), S("abd")));
    CHECK(!rv_eq(S("abc"), ints(3)));
    CHECK(!rv_eq(S("1"), rv_box_int(1)));
    CHECK(rv_eq(ints(5), ints(5)));
    CHECK(!rv_eq(ints(5), ints(6)));

    Vec *v = vec(&vec_ti, 1.0, -0.0);
    CHECK(rv_eq(v, vec(&vec_ti, 1.0, 0.0)));
    CHECK(!rv_eq(v, vec(&vec_ti, 1.0, 1.0)));
    CHECK(!rv_eq(v, vec(&vec2_ti, 1.0, 0.0)));
    Vec *nan = vec(&vec_ti, NAN, 0.0);
    CHECK(!rv_eq(nan, nan));

    Seg *a = seg("s", vec(&vec_ti, 1.0, 2.0), vec(&vec_ti, 3.0, 4.0), 7, 1);
    Seg *b = seg("s", vec(&vec_ti, 1.0, 2.0), vec(&vec_ti, 3.0, 4.0), 7, 1);
    CHECK(rv_eq(a, b));
    b->to = NULL;
    CHECK(!rv_eq(a, b));
    a->to = NULL;
    CHECK(rv_eq(a, b));
    b->from = vec(&vec_ti, 1.0, 2.5);
    CHECK(!rv_eq(a, b));
    b->from = vec(&vec_ti, 1.0, 2.0);
    b->id = 8;
    CHECK(!rv_eq(a, b));
    b->id = 7;
    b->ok = 0;
    CHECK(!rv_eq(a, b));
    b->ok = 1;
    b->name = S("t");
    CHECK(!rv_eq(a, b));
    b->name = S("s");
    CHECK(rv_eq(a, b));

    RvList *la = rv_list_new(RV_FK_PTR, 0);
    RvList *lb = rv_list_new(RV_FK_PTR, 0);
    rv_list_append_l(la, ptr(a));
    rv_list_append_l(lb, ptr(b));
    CHECK(rv_eq(la, lb));
    CHECK(rv_list_contains_l(la, ptr(b)));
    CHECK(rv_list_index_of_l(la, ptr(seg("s", NULL, NULL, 7, 1))) == -1);
}

/* ---- garbage collector ------------------------------------------------- */

typedef struct Node {
    RvHeader h;
    int64_t value;
    struct Node *next;
} Node;

static const uint8_t node_kinds[2] = {RV_FK_INT, RV_FK_PTR};
static const RvTypeInfo node_ti = {"Node", RV_KIND_STRUCT, 2, node_kinds};

enum { CHAIN_LEN = 100000, CHAIN_BYTES = CHAIN_LEN * 32 };

NOINLINE static Node *build_chain(int64_t n) {
    Node *head = rv_alloc(&node_ti);
    Node *cur = head;
    for (int64_t i = 1; i < n; i++) {
        Node *next = rv_alloc(&node_ti);
        next->value = i;
        cur->next = next;
        cur = next;
    }
    return head;
}

NOINLINE static int64_t chain_len(Node *head) {
    int64_t n = 0;
    for (Node *cur = head; cur; cur = cur->next) {
        CHECK(cur->h.ti == &node_ti && cur->value == n);
        n++;
    }
    return n;
}

/*
 * Everything that touches the chain runs in callees so that the only reference left in the caller's frame is the
 * volatile head itself; instrumented builds otherwise spill interior addresses there, which are legitimate roots.
 */
NOINLINE static void check_chain(Node *head) {
    CHECK(chain_len(head) == CHAIN_LEN);
    Node *other = build_chain(CHAIN_LEN);
    CHECK(rv_eq(head, other));
    other->next->value = 5;
    CHECK(!rv_eq(head, other));
}

static void test_gc_chain(void) {
    rv_gc_collect();
    Node *volatile head = build_chain(CHAIN_LEN);
    CHECK(rv_gc_live_bytes() >= CHAIN_BYTES);
    rv_gc_collect();
    size_t live = rv_gc_live_bytes();
    CHECK(live >= CHAIN_BYTES && live < CHAIN_BYTES + (64 << 10));
    check_chain(head);

    head = NULL;
    clobber_stack();
    rv_gc_collect();
    CHECK(rv_gc_live_bytes() < (64 << 10));
}

static void *root_slots[2];

NOINLINE static void fill_roots(void) {
    root_slots[0] = S("rooted string");
    root_slots[1] = ints(50);
}

static void test_gc_roots(void) {
    rv_add_roots(root_slots, 2);
    fill_roots();
    clobber_stack();
    rv_gc_collect();
    rv_gc_collect();
    CHECK(str_is(root_slots[0], "rooted string"));
    CHECK(rv_list_eq(root_slots[1], ints(50)));
    root_slots[0] = NULL;
    root_slots[1] = NULL;
}

static void test_gc_stress(void) {
    rv_gc_set_stress(1);
    char expect[2048];
    int n = 0;
    RvStr *volatile s = rv_str_empty();
    for (int i = 0; i < 300; i++) {
        s = rv_str_concat(s, rv_str_from_int(i));
        n += snprintf(expect + n, sizeof expect - (size_t)n, "%d", i);
    }
    CHECK(str_is(s, expect));

    RvList *volatile strs = rv_list_new(RV_FK_PTR, 0);
    RvList *volatile nums = rv_list_new(RV_FK_INT, 0);
    RvList *volatile floats = rv_list_new(RV_FK_FLOAT, 0);
    for (int i = 0; i < 300; i++) {
        rv_list_append_l(strs, ptr(rv_str_from_int(i)));
        rv_list_append_l(nums, i);
        rv_list_append_d(floats, i + 0.5);
    }
    CHECK(strs->len == 300 && nums->len == 300 && floats->len == 300);
    for (int i = 0; i < 300; i++) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", i);
        CHECK(str_is(elem(strs, i), buf));
        CHECK(rv_list_get_l(nums, i, &LOC) == i);
        CHECK(rv_list_get_d(floats, i, &LOC) == i + 0.5);
    }
    RvStr *volatile joined = rv_str_join(S(","), strs);
    CHECK(joined->len == n + 299);
    CHECK(rv_list_eq(rv_str_split(joined, S(",")), strs));
    CHECK(str_is(rv_str_replace(joined, S(","), S(", ")), rv_str_join(S(", "), strs)->bytes));
    RvList *volatile sliced = rv_list_slice(strs, 100, 200, &LOC);
    CHECK(str_is(elem(sliced, 0), "100") && str_is(elem(sliced, 99), "199"));
    rv_gc_set_stress(0);
}

NOINLINE static char *make_interior(const char *pattern, size_t len) {
    RvStr *s = rv_str_new(pattern, (int64_t)len);
    return s->bytes + len / 2;
}

static void test_gc_interior(void) {
    char pattern[200];
    for (size_t i = 0; i < sizeof pattern; i++) {
        pattern[i] = (char)('a' + i % 26);
    }
    char *volatile mid = make_interior(pattern, sizeof pattern);
    clobber_stack();
    rv_gc_collect();
    for (int i = 0; i < 20000; i++) {
        RvStr *junk = rv_str_repeat(S("x"), (int64_t)sizeof pattern, &LOC);
        CHECK(junk->bytes[0] == 'x');
    }
    rv_gc_collect();
    CHECK(memcmp(mid, pattern + sizeof pattern / 2, sizeof pattern / 2) == 0);
    RvStr *s = (RvStr *)(mid - sizeof pattern / 2 - offsetof(RvStr, bytes));
    CHECK(s->h.ti == &rv_ti_str && s->len == (int64_t)sizeof pattern && s->h.flags == 0);
    CHECK(memcmp(s->bytes, pattern, sizeof pattern) == 0);
}

NOINLINE static RvStr *make_big(size_t len) {
    char *buf = malloc(len);
    CHECK(buf != NULL);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)('A' + i % 26);
    }
    RvStr *s = rv_str_new(buf, (int64_t)len);
    free(buf);
    return s;
}

static int big_ok(RvStr *s, size_t len) {
    if (s->len != (int64_t)len || s->bytes[len] != 0) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (s->bytes[i] != (char)('A' + i % 26)) {
            return 0;
        }
    }
    return 1;
}

enum { BIG = 1 << 20 };

NOINLINE static void check_bigs(RvStr *big, RvStr *big2) {
    CHECK(big_ok(big, BIG) && big_ok(big2, BIG + 1));
    CHECK(rv_str_cmp(big, big2) == -1);
    CHECK(rv_str_starts_with(big2, big));
    CHECK(rv_str_find(big2, rv_str_slice(big, BIG - 30, BIG, &LOC)) == (BIG - 30) % 26); /* the pattern repeats */
}

static void test_gc_large(void) {
    clobber_stack();
    rv_gc_collect();
    size_t before = rv_gc_live_bytes();
    RvStr *volatile big = make_big(BIG);
    rv_gc_collect();
    CHECK(big_ok(big, BIG));
    CHECK(rv_gc_live_bytes() >= (size_t)BIG);
    RvStr *volatile big2 = make_big(BIG + 1);
    rv_gc_collect();
    check_bigs(big, big2);
    big = NULL;
    big2 = NULL;
    clobber_stack();
    rv_gc_collect();
    CHECK(rv_gc_live_bytes() < before + BIG);
}

static void test_gc_threshold(void) {
    clobber_stack();
    rv_gc_collect();
    RvList *volatile keep = rv_list_new(RV_FK_PTR, 0);
    for (int i = 0; i < 1000; i++) {
        rv_list_append_l(keep, ptr(rv_str_from_int(i)));
    }
    size_t peak = 0;
    for (int i = 0; i < 300000; i++) {
        RvStr *junk = rv_str_from_int(i);
        CHECK(junk->len > 0);
        RvList *l = rv_list_new(RV_FK_FLOAT, 4);
        rv_list_append_d(l, 1.0);
        size_t live = rv_gc_live_bytes();
        peak = live > peak ? live : peak;
    }
    CHECK(peak < (10 << 20));
    CHECK(keep->len == 1000);
    for (int i = 0; i < 1000; i++) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", i);
        CHECK(str_is(elem(keep, i), buf));
    }
}

/* ---- io ---------------------------------------------------------------- */

static void print_stuff(void) {
    rv_print(S("a"));
    rv_print(rv_str_empty());
    rv_println(rv_str_new("b\0c", 3));
    rv_println(rv_str_empty());
    rv_eprintln(S("err"));
    rv_eprintln(rv_str_empty());
    rv_print(S("tail"));
    rv_flush();
    rv_flush();
}

static char *temp_path(void) {
    static char path[] = "/tmp/rt_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    return path;
}

static void test_io(void) {
    Child c;
    run_child(print_stuff, &c);
    CHECK(WIFEXITED(c.status) && WEXITSTATUS(c.status) == 0);
    CHECK(c.out_len == 10 && memcmp(c.out, "ab\0c\n\ntail", 10) == 0);
    CHECK(strcmp(c.err, "err\n\n") == 0);

    RvList *args = rv_args();
    CHECK(args->len == 2 && args->elem_kind == RV_FK_PTR);
    CHECK(str_is(elem(args, 0), "alpha") && str_is(elem(args, 1), "beta gamma"));

    char *path = temp_path();
    RvStr *p = S(path);
    RvStr *data = rv_str_new("bin\0ary\r\n\xff", 11);
    CHECK(rv_write_file(p, data) == 1);
    RvStr *back = rv_read_file(p);
    CHECK(back && rv_str_eq(back, data));
    CHECK(rv_write_file(p, rv_str_empty()) == 1);
    CHECK(rv_read_file(p) == rv_str_empty());
    RvStr *big = make_big(300000);
    CHECK(rv_write_file(p, big) == 1);
    CHECK(rv_str_eq(rv_read_file(p), big));
    CHECK(rv_read_file(S("/nonexistent/dir/file")) == NULL);
    CHECK(rv_write_file(S("/nonexistent/dir/file"), data) == 0);

    CHECK(rv_write_file(p, S("line1\r\nline2\n\nlast")) == 1);
    CHECK(freopen(path, "r", stdin) != NULL);
    CHECK(str_is(rv_read_line(), "line1"));
    CHECK(str_is(rv_read_line(), "line2"));
    CHECK(rv_read_line() == rv_str_empty());
    CHECK(str_is(rv_read_line(), "last"));
    CHECK(rv_read_line() == NULL);
    CHECK(rv_read_line() == NULL);
    unlink(path);
}

/* Kept out of main so that every test local lies below the stack slot given to rv_init. */
NOINLINE static void run_tests(void) {
    test_panics();
    test_arithmetic();
    test_str_basics();
    test_str_search();
    test_str_split_join();
    test_str_transform();
    test_str_conversions();
    test_list_int();
    test_list_float();
    test_list_ptr();
    test_boxes();
    test_eq();
    test_gc_chain();
    test_gc_roots();
    test_gc_stress();
    test_gc_interior();
    test_gc_large();
    test_gc_threshold();
    test_io();
}

int main(void) {
    int bottom;
    char *fake_argv[] = {"rt_test", "alpha", "beta gamma", NULL};
    rv_init(3, fake_argv, &bottom);
    run_tests();
    printf("rt_test passed\n");
    rv_flush();
    return 0;
}
