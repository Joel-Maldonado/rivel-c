#include <assert.h>
#include <string.h>

#include "base/arena.h"
#include "base/diag.h"
#include "base/source.h"
#include "base/str.h"
#include "base/strbuf.h"
#include "base/strmap.h"
#include "base/vec.h"

typedef Vec(int) IntVec;

static void test_vec(void) {
    IntVec xs = {0};
    for (int i = 0; i < 100; i++) {
        vec_push(&xs, i * 2);
    }
    assert(xs.len == 100);
    assert(xs.data[42] == 84);
    assert(vec_pop(&xs) == 198);
    assert(vec_last(&xs) == 196);
    vec_free(&xs);
    assert(xs.data == NULL && xs.len == 0);
}

static void test_arena_vec(void) {
    Arena a;
    IntVec xs = {0};
    arena_init(&a, 64);
    for (int i = 0; i < 1000; i++) {
        avec_push(&a, &xs, i);
    }
    assert(xs.len == 1000 && xs.data[999] == 999);
    arena_free(&a);
}

static void test_arena_strings(void) {
    Arena a;
    char *s;
    arena_init(&a, 32);
    s = arena_printf(&a, "%s-%d", "value", 42);
    assert(strcmp(s, "value-42") == 0);
    s = arena_strndup(&a, "hello world", 5);
    assert(strcmp(s, "hello") == 0);
    assert(((uintptr_t)arena_alloc(&a, 1) & 15) == 0);
    arena_free(&a);
}

static void test_strmap(void) {
    StrMap m;
    int values[200];
    strmap_init(&m);
    for (int i = 0; i < 200; i++) {
        char *key = xprintf("key%d", i);
        values[i] = i;
        assert(strmap_put(&m, str_from(key), &values[i]) == NULL);
    }
    assert(m.count == 200);
    assert(*(int *)strmap_get(&m, STR("key137")) == 137);
    assert(!strmap_has(&m, STR("missing")));
    assert(strmap_put(&m, STR("key5"), &values[6]) == &values[5]);
    for (size_t i = 0; i < m.cap; i++) {
        if (m.entries[i].used) {
            free((char *)m.entries[i].key.p);
        }
    }
    strmap_free(&m);
}

static void test_strbuf(void) {
    StrBuf sb;
    char *taken;
    sb_init(&sb);
    sb_puts(&sb, "ab");
    sb_putc(&sb, 'c');
    sb_printf(&sb, "%d%s", 1, "z");
    assert(strcmp(sb_cstr(&sb), "abc1z") == 0);
    assert(sb_str(&sb).n == 5);
    taken = sb_take(&sb);
    assert(strcmp(taken, "abc1z") == 0 && sb.len == 0);
    free(taken);
    sb_free(&sb);
}

static void test_source_positions(void) {
    Source *src = source_new("t.rivel", "ab\ncd\n\nefg", 10);
    int line;
    int col;
    source_position(src, 0, &line, &col);
    assert(line == 1 && col == 1);
    source_position(src, 4, &line, &col);
    assert(line == 2 && col == 2);
    source_position(src, 7, &line, &col);
    assert(line == 4 && col == 1);
    assert(str_eq_c(source_line(src, 2), "cd"));
    assert(str_eq_c(source_line(src, 3), ""));
    assert(str_eq_c(source_line(src, 4), "efg"));
    source_free(src);
}

static void test_diags_render(void) {
    Source *src = source_new("t.rivel", "x := 1\ny := 2;\n", 15);
    Diags d;
    char buf[512];
    FILE *f = tmpfile();
    size_t n;

    diags_init(&d);
    diag_error(&d, src, span_make(6, 6), "expected `;`");
    diag_warning(&d, src, span_make(7, 8), "`%s` is never used", "y");
    assert(!diags_ok(&d) && d.errors == 1 && d.warnings == 1);
    diags_print(&d, f, false);
    rewind(f);
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    assert(strstr(buf, "t.rivel:1:7: error: expected `;`") != NULL);
    assert(strstr(buf, "    x := 1\n          ^") != NULL);
    assert(strstr(buf, "t.rivel:2:1: warning: `y` is never used") != NULL);
    fclose(f);
    diags_free(&d);
    source_free(src);
}

int main(void) {
    test_vec();
    test_arena_vec();
    test_arena_strings();
    test_strmap();
    test_strbuf();
    test_source_positions();
    test_diags_render();
    puts("base_test passed");
    return 0;
}
