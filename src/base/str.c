#include "base/str.h"

#include <string.h>

Str str_from(const char *cstr) {
    return (Str){cstr, strlen(cstr)};
}

Str str_slice(const char *p, size_t n) {
    return (Str){p, n};
}

bool str_eq(Str a, Str b) {
    return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, a.n) == 0);
}

bool str_eq_c(Str a, const char *b) {
    return str_eq(a, str_from(b));
}

int str_cmp(Str a, Str b) {
    size_t n = a.n < b.n ? a.n : b.n;
    int c = n ? memcmp(a.p, b.p, n) : 0;
    if (c != 0) {
        return c < 0 ? -1 : 1;
    }
    return a.n < b.n ? -1 : a.n > b.n ? 1 : 0;
}

uint64_t str_hash(Str s) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < s.n; i++) {
        h ^= (unsigned char)s.p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
