#ifndef RV_BASE_UTIL_H
#define RV_BASE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define PRINTF_LIKE(fmt, first) __attribute__((format(printf, fmt, first)))
#define VPRINTF_LIKE(fmt) __attribute__((format(printf, fmt, 0)))

_Noreturn void die(const char *fmt, ...) PRINTF_LIKE(1, 2);

void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xprintf(const char *fmt, ...) PRINTF_LIKE(1, 2);

#endif
