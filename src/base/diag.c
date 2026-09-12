#include "base/diag.h"

#include <stdarg.h>

void diags_init(Diags *d) {
    memset(d, 0, sizeof *d);
    d->max_errors = 50;
}

void diags_free(Diags *d) {
    for (size_t i = 0; i < d->items.len; i++) {
        free(d->items.data[i].msg);
    }
    vec_free(&d->items);
}

static void diag_add(Diags *d, DiagLevel level, const Source *src, Span span, const char *fmt, va_list args)
    VPRINTF_LIKE(5);
static void diag_add(Diags *d, DiagLevel level, const Source *src, Span span, const char *fmt, va_list args) {
    Diag diag;
    va_list copy;
    int n;

    if (level == DIAG_ERROR) {
        d->errors++;
        if (d->max_errors && d->errors > d->max_errors) {
            return;
        }
    } else if (level == DIAG_WARNING) {
        d->warnings++;
    }
    va_copy(copy, args);
    n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        die("vsnprintf failed");
    }
    diag.level = level;
    diag.src = src;
    diag.span = span;
    diag.msg = xmalloc((size_t)n + 1);
    vsnprintf(diag.msg, (size_t)n + 1, fmt, args);
    vec_push(&d->items, diag);
}

#define DIAG_FN(name, level)                                                                                           \
    void name(Diags *d, const Source *src, Span span, const char *fmt, ...) {                                          \
        va_list args;                                                                                                  \
        va_start(args, fmt);                                                                                           \
        diag_add(d, level, src, span, fmt, args);                                                                      \
        va_end(args);                                                                                                  \
    }

DIAG_FN(diag_error, DIAG_ERROR)
DIAG_FN(diag_warning, DIAG_WARNING)
DIAG_FN(diag_note, DIAG_NOTE)

void diag_error_nowhere(Diags *d, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    diag_add(d, DIAG_ERROR, NULL, span_make(0, 0), fmt, args);
    va_end(args);
}

bool diags_ok(const Diags *d) {
    return d->errors == 0;
}

static const char *level_name(DiagLevel level) {
    switch (level) {
    case DIAG_ERROR:
        return "error";
    case DIAG_WARNING:
        return "warning";
    case DIAG_NOTE:
        return "note";
    }
    return "?";
}

static const char *level_color(DiagLevel level) {
    switch (level) {
    case DIAG_ERROR:
        return "\x1b[1;31m";
    case DIAG_WARNING:
        return "\x1b[1;33m";
    case DIAG_NOTE:
        return "\x1b[1;36m";
    }
    return "";
}

/* Width of the line prefix up to byte `upto`, expanding tabs to 4 columns. */
static int display_width(Str line, size_t upto) {
    int w = 0;
    for (size_t i = 0; i < upto && i < line.n; i++) {
        w += line.p[i] == '\t' ? 4 - (w % 4) : 1;
    }
    return w;
}

static void print_excerpt(const Diag *diag, FILE *out, bool color) {
    int line;
    int col;
    int end_line;
    int end_col;
    Str text;
    int start_w;
    int end_w;

    source_position(diag->src, diag->span.lo, &line, &col);
    source_position(diag->src, diag->span.hi, &end_line, &end_col);
    text = source_line(diag->src, line);

    fputs("    ", out);
    for (size_t i = 0; i < text.n; i++) {
        if (text.p[i] == '\t') {
            fputs("    ", out);
        } else {
            fputc(text.p[i], out);
        }
    }
    fputc('\n', out);

    start_w = display_width(text, (size_t)col - 1);
    end_w = end_line == line ? display_width(text, (size_t)end_col - 1) : (int)text.n;
    if (end_w <= start_w) {
        end_w = start_w + 1;
    }
    fputs("    ", out);
    for (int i = 0; i < start_w; i++) {
        fputc(' ', out);
    }
    if (color) {
        fputs(level_color(diag->level), out);
    }
    fputc('^', out);
    for (int i = start_w + 1; i < end_w; i++) {
        fputc('~', out);
    }
    if (color) {
        fputs("\x1b[0m", out);
    }
    fputc('\n', out);
}

void diags_print(const Diags *d, FILE *out, bool color) {
    for (size_t i = 0; i < d->items.len; i++) {
        const Diag *diag = &d->items.data[i];

        if (diag->src != NULL) {
            int line;
            int col;
            source_position(diag->src, diag->span.lo, &line, &col);
            fprintf(out, "%s:%d:%d: ", diag->src->name, line, col);
        }
        if (color) {
            fprintf(out, "%s%s:\x1b[0m %s\n", level_color(diag->level), level_name(diag->level), diag->msg);
        } else {
            fprintf(out, "%s: %s\n", level_name(diag->level), diag->msg);
        }
        if (diag->src != NULL) {
            print_excerpt(diag, out, color);
        }
    }
    if (d->max_errors && d->errors > d->max_errors) {
        fprintf(out, "error: too many errors, stopping after %d\n", d->max_errors);
    }
}
