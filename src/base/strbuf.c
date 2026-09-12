#include "base/strbuf.h"

#include <stdarg.h>
#include <stdio.h>

static void sb_reserve(StrBuf *sb, size_t extra) {
    size_t want = sb->len + extra + 1;
    size_t cap;

    if (want <= sb->cap) {
        return;
    }
    cap = sb->cap ? sb->cap : 64;
    while (cap < want) {
        cap *= 2;
    }
    sb->data = xrealloc(sb->data, cap);
    sb->cap = cap;
}

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb_init(sb);
}

void sb_clear(StrBuf *sb) {
    sb->len = 0;
    if (sb->data != NULL) {
        sb->data[0] = '\0';
    }
}

void sb_putn(StrBuf *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_putc(StrBuf *sb, char c) {
    sb_putn(sb, &c, 1);
}

void sb_puts(StrBuf *sb, const char *s) {
    sb_putn(sb, s, strlen(s));
}

void sb_putstr(StrBuf *sb, Str s) {
    sb_putn(sb, s.p, s.n);
}

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    va_list args;
    va_list copy;
    int n;

    va_start(args, fmt);
    va_copy(copy, args);
    n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        die("vsnprintf failed");
    }
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, args);
    va_end(args);
    sb->len += (size_t)n;
}

const char *sb_cstr(const StrBuf *sb) {
    return sb->data ? sb->data : "";
}

Str sb_str(const StrBuf *sb) {
    return (Str){sb_cstr(sb), sb->len};
}

char *sb_take(StrBuf *sb) {
    char *data = sb->data ? sb->data : xstrdup("");
    sb_init(sb);
    return data;
}
