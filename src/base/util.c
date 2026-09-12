#include "base/util.h"

#include <stdarg.h>

_Noreturn void die(const char *fmt, ...) {
    va_list args;

    fputs("rivelc: fatal: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(2);
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (p == NULL) {
        die("out of memory");
    }
    return p;
}

void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (p == NULL) {
        die("out of memory");
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (p == NULL) {
        die("out of memory");
    }
    return p;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = xmalloc(n);
    memcpy(copy, s, n);
    return copy;
}

char *xprintf(const char *fmt, ...) {
    va_list args;
    va_list copy;
    int n;
    char *buf;

    va_start(args, fmt);
    va_copy(copy, args);
    n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        die("vsnprintf failed");
    }
    buf = xmalloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, args);
    va_end(args);
    return buf;
}
