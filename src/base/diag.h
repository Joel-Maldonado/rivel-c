#ifndef RV_BASE_DIAG_H
#define RV_BASE_DIAG_H

#include <stdbool.h>
#include <stdio.h>

#include "base/source.h"
#include "base/util.h"
#include "base/vec.h"

typedef enum DiagLevel {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
} DiagLevel;

typedef struct Diag {
    DiagLevel level;
    const Source *src; /* NULL for messages without a location */
    Span span;
    char *msg;
} Diag;

typedef Vec(Diag) DiagVec;

/*
 * Collects every diagnostic for one compilation. Passes keep going after an
 * error wherever they can; the driver stops between passes when errors > 0.
 */
typedef struct Diags {
    DiagVec items;
    int errors;
    int warnings;
    int max_errors; /* stop reporting after this many; 0 for unlimited */
} Diags;

void diags_init(Diags *d);
void diags_free(Diags *d);
void diag_error(Diags *d, const Source *src, Span span, const char *fmt, ...) PRINTF_LIKE(4, 5);
void diag_warning(Diags *d, const Source *src, Span span, const char *fmt, ...) PRINTF_LIKE(4, 5);
void diag_note(Diags *d, const Source *src, Span span, const char *fmt, ...) PRINTF_LIKE(4, 5);
void diag_error_nowhere(Diags *d, const char *fmt, ...) PRINTF_LIKE(2, 3);
bool diags_ok(const Diags *d);
/* Renders every diagnostic as file:line:col: level: message plus an excerpt. */
void diags_print(const Diags *d, FILE *out, bool color);

#endif
