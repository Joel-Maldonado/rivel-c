#ifndef RV_BASE_STRBUF_H
#define RV_BASE_STRBUF_H

#include <stddef.h>

#include "base/str.h"
#include "base/util.h"

/* Growable byte buffer, always NUL-terminated after any append. */
typedef struct StrBuf {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_clear(StrBuf *sb);
void sb_putc(StrBuf *sb, char c);
void sb_puts(StrBuf *sb, const char *s);
void sb_putn(StrBuf *sb, const char *s, size_t n);
void sb_putstr(StrBuf *sb, Str s);
void sb_printf(StrBuf *sb, const char *fmt, ...) PRINTF_LIKE(2, 3);
const char *sb_cstr(const StrBuf *sb);
Str sb_str(const StrBuf *sb);
/* Detaches the buffer; caller frees. */
char *sb_take(StrBuf *sb);

#endif
