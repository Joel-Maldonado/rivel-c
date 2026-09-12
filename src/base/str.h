#ifndef RV_BASE_STR_H
#define RV_BASE_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Borrowed, non-owning view of bytes. Not NUL-terminated. */
typedef struct Str {
    const char *p;
    size_t n;
} Str;

#define STR(lit) ((Str){(lit), sizeof(lit) - 1})
#define STR_FMT "%.*s"
#define STR_ARG(s) (int)(s).n, (s).p

Str str_from(const char *cstr);
Str str_slice(const char *p, size_t n);
bool str_eq(Str a, Str b);
bool str_eq_c(Str a, const char *b);
int str_cmp(Str a, Str b);
uint64_t str_hash(Str s);

#endif
