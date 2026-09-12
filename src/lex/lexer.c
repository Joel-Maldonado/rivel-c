#include "lex/lexer.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>

#include "base/strbuf.h"

typedef enum Mode {
    MODE_NORMAL,
    MODE_FSTR_TEXT, /* inside f"...", scanning literal text */
    MODE_FSTR_EXPR, /* inside f"...{ here }..." */
} Mode;

typedef struct ModeFrame {
    Mode mode;
    int brace_depth; /* MODE_FSTR_EXPR: unmatched `{` inside the expression */
} ModeFrame;

typedef Vec(ModeFrame) ModeStack;

typedef struct Lexer {
    const Source *src;
    const char *text;
    uint32_t len;
    uint32_t pos;
    Arena *arena;
    Diags *diags;
    TokenVec *out;
    ModeStack modes;
    StrBuf scratch;
} Lexer;

static const struct {
    const char *word;
    TokKind kind;
} KEYWORDS[] = {
    {"func", TOK_KW_FUNC},   {"struct", TOK_KW_STRUCT},     {"self", TOK_KW_SELF},     {"if", TOK_KW_IF},
    {"else", TOK_KW_ELSE},   {"while", TOK_KW_WHILE},       {"for", TOK_KW_FOR},       {"in", TOK_KW_IN},
    {"break", TOK_KW_BREAK}, {"continue", TOK_KW_CONTINUE}, {"return", TOK_KW_RETURN}, {"true", TOK_KW_TRUE},
    {"false", TOK_KW_FALSE}, {"null", TOK_KW_NULL},
};

static const char *const RESERVED[] = {"enum", "match", "import", "as", "const", "pub", "type", "is"};

static char peek(const Lexer *lx, uint32_t ahead) {
    return lx->pos + ahead < lx->len ? lx->text[lx->pos + ahead] : '\0';
}

static bool at_end(const Lexer *lx) {
    return lx->pos >= lx->len;
}

static Mode mode(const Lexer *lx) {
    return vec_last(&lx->modes).mode;
}

static void push_mode(Lexer *lx, Mode m) {
    ModeFrame f = {m, 0};
    vec_push(&lx->modes, f);
}

static void pop_mode(Lexer *lx) {
    if (lx->modes.len > 1) {
        lx->modes.len--;
    }
}

static void error_at(Lexer *lx, uint32_t lo, uint32_t hi, const char *fmt, ...) PRINTF_LIKE(4, 5);
static void error_at(Lexer *lx, uint32_t lo, uint32_t hi, const char *fmt, ...) {
    va_list args;
    char *msg;

    va_start(args, fmt);
    msg = arena_vprintf(lx->arena, fmt, args);
    va_end(args);
    diag_error(lx->diags, lx->src, span_make(lo, hi), "%s", msg);
}

static Token *emit(Lexer *lx, TokKind kind, uint32_t lo, uint32_t hi) {
    Token t;
    memset(&t, 0, sizeof t);
    t.kind = kind;
    t.span = span_make(lo, hi);
    t.text = str_slice(lx->text + lo, hi - lo);
    vec_push(lx->out, t);
    return &vec_last(lx->out);
}

/* ---- whitespace and comments ------------------------------------------ */

static void skip_block_comment(Lexer *lx) {
    uint32_t start = lx->pos;
    int depth = 1;

    lx->pos += 2;
    while (!at_end(lx)) {
        if (peek(lx, 0) == '/' && peek(lx, 1) == '*') {
            depth++;
            lx->pos += 2;
        } else if (peek(lx, 0) == '*' && peek(lx, 1) == '/') {
            lx->pos += 2;
            if (--depth == 0) {
                return;
            }
        } else {
            lx->pos++;
        }
    }
    error_at(lx, start, start + 2, "unterminated block comment");
}

static void skip_trivia(Lexer *lx) {
    for (;;) {
        char c = peek(lx, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lx->pos++;
        } else if (c == '/' && peek(lx, 1) == '/') {
            while (!at_end(lx) && peek(lx, 0) != '\n') {
                lx->pos++;
            }
        } else if (c == '/' && peek(lx, 1) == '*') {
            skip_block_comment(lx);
        } else {
            return;
        }
    }
}

/* ---- identifiers and numbers ------------------------------------------- */

static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void lex_ident(Lexer *lx) {
    uint32_t lo = lx->pos;
    Str word;
    Token *t;

    while (is_ident_char(peek(lx, 0))) {
        lx->pos++;
    }
    word = str_slice(lx->text + lo, lx->pos - lo);
    for (size_t i = 0; i < ARRAY_LEN(KEYWORDS); i++) {
        if (str_eq_c(word, KEYWORDS[i].word)) {
            emit(lx, KEYWORDS[i].kind, lo, lx->pos);
            return;
        }
    }
    t = emit(lx, TOK_IDENT, lo, lx->pos);
    for (size_t i = 0; i < ARRAY_LEN(RESERVED); i++) {
        if (str_eq_c(word, RESERVED[i])) {
            error_at(lx, t->span.lo, t->span.hi, "`%s` is reserved and cannot be used as a name", RESERVED[i]);
        }
    }
}

static int digit_value(char c, int base) {
    int v;
    if (c >= '0' && c <= '9') {
        v = c - '0';
    } else if (c >= 'a' && c <= 'f') {
        v = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        v = c - 'A' + 10;
    } else {
        return -1;
    }
    return v < base ? v : -1;
}

static void lex_number(Lexer *lx) {
    uint32_t lo = lx->pos;
    int base = 10;
    uint64_t value = 0;
    bool overflow = false;
    bool any_digit = false;
    bool is_float = false;
    Token *t;

    if (peek(lx, 0) == '0' && (peek(lx, 1) == 'x' || peek(lx, 1) == 'o' || peek(lx, 1) == 'b')) {
        base = peek(lx, 1) == 'x' ? 16 : peek(lx, 1) == 'o' ? 8 : 2;
        lx->pos += 2;
    }

    for (;;) {
        char c = peek(lx, 0);
        int d;
        if (c == '_') {
            lx->pos++;
            continue;
        }
        d = digit_value(c, base);
        if (d < 0) {
            break;
        }
        any_digit = true;
        if (value > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) {
            overflow = true;
        } else {
            value = value * (uint64_t)base + (uint64_t)d;
        }
        lx->pos++;
    }

    if (base == 10) {
        if (peek(lx, 0) == '.' && isdigit((unsigned char)peek(lx, 1))) {
            is_float = true;
            lx->pos++;
            while (isdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') {
                lx->pos++;
            }
        }
        if ((peek(lx, 0) == 'e' || peek(lx, 0) == 'E') &&
            (isdigit((unsigned char)peek(lx, 1)) ||
             ((peek(lx, 1) == '+' || peek(lx, 1) == '-') && isdigit((unsigned char)peek(lx, 2))))) {
            is_float = true;
            lx->pos += 2;
            while (isdigit((unsigned char)peek(lx, 0))) {
                lx->pos++;
            }
        }
    }

    if (is_ident_char(peek(lx, 0))) {
        uint32_t bad = lx->pos;
        while (is_ident_char(peek(lx, 0))) {
            lx->pos++;
        }
        error_at(lx, bad, lx->pos, "invalid suffix on number literal");
    }

    if (is_float) {
        sb_clear(&lx->scratch);
        for (uint32_t i = lo; i < lx->pos; i++) {
            if (lx->text[i] != '_') {
                sb_putc(&lx->scratch, lx->text[i]);
            }
        }
        t = emit(lx, TOK_FLOAT, lo, lx->pos);
        errno = 0;
        t->as.float_val = strtod(sb_cstr(&lx->scratch), NULL);
        if (errno == ERANGE && isinf(t->as.float_val)) {
            error_at(lx, lo, lx->pos, "float literal is out of range");
        }
        return;
    }

    if (!any_digit) {
        error_at(lx, lo, lx->pos, "number literal has no digits");
    }
    if (overflow || value > (uint64_t)1 << 63) {
        error_at(lx, lo, lx->pos, "integer literal is too large for int");
        value = 0;
    }
    t = emit(lx, TOK_INT, lo, lx->pos);
    t->as.int_val = value;
}

/* ---- strings ------------------------------------------------------------ */

static void put_utf8(StrBuf *sb, uint32_t cp) {
    if (cp < 0x80) {
        sb_putc(sb, (char)cp);
    } else if (cp < 0x800) {
        sb_putc(sb, (char)(0xC0 | (cp >> 6)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sb_putc(sb, (char)(0xE0 | (cp >> 12)));
        sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_putc(sb, (char)(0xF0 | (cp >> 18)));
        sb_putc(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

/* Decodes one escape starting at the backslash; appends to scratch. */
static void lex_escape(Lexer *lx) {
    uint32_t lo = lx->pos;
    char c = peek(lx, 1);

    lx->pos += 2;
    switch (c) {
    case '\\':
        sb_putc(&lx->scratch, '\\');
        return;
    case '"':
        sb_putc(&lx->scratch, '"');
        return;
    case 'n':
        sb_putc(&lx->scratch, '\n');
        return;
    case 'r':
        sb_putc(&lx->scratch, '\r');
        return;
    case 't':
        sb_putc(&lx->scratch, '\t');
        return;
    case '0':
        sb_putc(&lx->scratch, '\0');
        return;
    case 'x': {
        int hi = digit_value(peek(lx, 0), 16);
        int lo_d = digit_value(peek(lx, 1), 16);
        if (hi < 0 || lo_d < 0) {
            error_at(lx, lo, lx->pos, "`\\x` escape needs two hex digits");
            return;
        }
        lx->pos += 2;
        sb_putc(&lx->scratch, (char)(hi * 16 + lo_d));
        return;
    }
    case 'u': {
        uint32_t cp = 0;
        int ndigits = 0;
        if (peek(lx, 0) != '{') {
            error_at(lx, lo, lx->pos, "`\\u` escape must be written as `\\u{...}`");
            return;
        }
        lx->pos++;
        while (digit_value(peek(lx, 0), 16) >= 0) {
            cp = cp * 16 + (uint32_t)digit_value(peek(lx, 0), 16);
            ndigits++;
            lx->pos++;
            if (cp > 0x10FFFF) {
                break;
            }
        }
        if (peek(lx, 0) != '}' || ndigits == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            error_at(lx, lo, lx->pos, "invalid `\\u{...}` escape");
            return;
        }
        lx->pos++;
        put_utf8(&lx->scratch, cp);
        return;
    }
    default:
        if (c == '\n' || c == '\0') {
            lx->pos = lo + 1;
            error_at(lx, lo, lo + 1, "unterminated string literal");
        } else {
            error_at(lx, lo, lx->pos, "unknown escape `\\%c`", c);
        }
        return;
    }
}

static Str take_scratch(Lexer *lx) {
    char *bytes = arena_strndup(lx->arena, sb_cstr(&lx->scratch), lx->scratch.len);
    return str_slice(bytes, lx->scratch.len);
}

static void lex_string(Lexer *lx) {
    uint32_t lo = lx->pos;
    Token *t;

    lx->pos++;
    sb_clear(&lx->scratch);
    for (;;) {
        char c = peek(lx, 0);
        if (c == '"') {
            lx->pos++;
            break;
        }
        if (c == '\n' || at_end(lx)) {
            error_at(lx, lo, lo + 1, "unterminated string literal");
            break;
        }
        if (c == '\\') {
            lex_escape(lx);
        } else {
            sb_putc(&lx->scratch, c);
            lx->pos++;
        }
    }
    t = emit(lx, TOK_STRING, lo, lx->pos);
    t->as.str_val = take_scratch(lx);
}

/* Scans literal text inside an f-string up to `{`, `"`, or an error. */
static void lex_fstring_text(Lexer *lx) {
    uint32_t lo = lx->pos;
    Token *t;

    sb_clear(&lx->scratch);
    for (;;) {
        char c = peek(lx, 0);
        if (c == '"') {
            if (lx->pos > lo) {
                t = emit(lx, TOK_FSTR_TEXT, lo, lx->pos);
                t->as.str_val = take_scratch(lx);
            }
            emit(lx, TOK_FSTR_END, lx->pos, lx->pos + 1);
            lx->pos++;
            pop_mode(lx);
            return;
        }
        if (c == '\n' || at_end(lx)) {
            error_at(lx, lo, lo + 1, "unterminated f-string");
            if (lx->pos > lo) {
                t = emit(lx, TOK_FSTR_TEXT, lo, lx->pos);
                t->as.str_val = take_scratch(lx);
            }
            emit(lx, TOK_FSTR_END, lx->pos, lx->pos);
            pop_mode(lx);
            return;
        }
        if (c == '{') {
            if (peek(lx, 1) == '{') {
                sb_putc(&lx->scratch, '{');
                lx->pos += 2;
                continue;
            }
            if (lx->pos > lo) {
                t = emit(lx, TOK_FSTR_TEXT, lo, lx->pos);
                t->as.str_val = take_scratch(lx);
            }
            emit(lx, TOK_FSTR_EXPR_START, lx->pos, lx->pos + 1);
            lx->pos++;
            push_mode(lx, MODE_FSTR_EXPR);
            return;
        }
        if (c == '}') {
            if (peek(lx, 1) == '}') {
                sb_putc(&lx->scratch, '}');
                lx->pos += 2;
                continue;
            }
            error_at(lx, lx->pos, lx->pos + 1, "single `}` in f-string; write `}}` for a literal brace");
            lx->pos++;
            continue;
        }
        if (c == '\\') {
            lex_escape(lx);
        } else {
            sb_putc(&lx->scratch, c);
            lx->pos++;
        }
    }
}

/* ---- operators ---------------------------------------------------------- */

static bool lex_operator(Lexer *lx) {
    static const struct {
        const char *text;
        TokKind kind;
    } OPS[] = {
        /* longest first so that prefixes don't win */
        {"..=", TOK_DOTDOTEQ},
        {"<<", TOK_SHL},
        {">>", TOK_SHR},
        {"==", TOK_EQ},
        {"!=", TOK_NE},
        {"<=", TOK_LE},
        {">=", TOK_GE},
        {"&&", TOK_ANDAND},
        {"||", TOK_OROR},
        {"..", TOK_DOTDOT},
        {"->", TOK_ARROW},
        {"??", TOK_QQ},
        {":=", TOK_DEFINE},
        {"+=", TOK_PLUS_ASSIGN},
        {"-=", TOK_MINUS_ASSIGN},
        {"*=", TOK_STAR_ASSIGN},
        {"/=", TOK_SLASH_ASSIGN},
        {"%=", TOK_PERCENT_ASSIGN},
        {"(", TOK_LPAREN},
        {")", TOK_RPAREN},
        {"{", TOK_LBRACE},
        {"}", TOK_RBRACE},
        {"[", TOK_LBRACKET},
        {"]", TOK_RBRACKET},
        {",", TOK_COMMA},
        {".", TOK_DOT},
        {":", TOK_COLON},
        {";", TOK_SEMI},
        {"?", TOK_QUESTION},
        {"=", TOK_ASSIGN},
        {"+", TOK_PLUS},
        {"-", TOK_MINUS},
        {"*", TOK_STAR},
        {"/", TOK_SLASH},
        {"%", TOK_PERCENT},
        {"&", TOK_AMP},
        {"|", TOK_PIPE},
        {"^", TOK_CARET},
        {"~", TOK_TILDE},
        {"<", TOK_LT},
        {">", TOK_GT},
        {"!", TOK_BANG},
    };

    for (size_t i = 0; i < ARRAY_LEN(OPS); i++) {
        size_t n = strlen(OPS[i].text);
        if (lx->pos + n <= lx->len && memcmp(lx->text + lx->pos, OPS[i].text, n) == 0) {
            emit(lx, OPS[i].kind, lx->pos, lx->pos + (uint32_t)n);
            lx->pos += (uint32_t)n;
            return true;
        }
    }
    return false;
}

/* ---- main loop ---------------------------------------------------------- */

static void lex_normal(Lexer *lx) {
    char c;
    uint32_t lo;

    skip_trivia(lx);
    if (at_end(lx)) {
        return;
    }
    c = peek(lx, 0);
    lo = lx->pos;

    if (c == 'f' && peek(lx, 1) == '"') {
        emit(lx, TOK_FSTR_START, lo, lo + 2);
        lx->pos += 2;
        push_mode(lx, MODE_FSTR_TEXT);
        return;
    }
    if (is_ident_start(c)) {
        lex_ident(lx);
        return;
    }
    if (isdigit((unsigned char)c)) {
        lex_number(lx);
        return;
    }
    if (c == '"') {
        lex_string(lx);
        return;
    }
    if (mode(lx) == MODE_FSTR_EXPR) {
        ModeFrame *frame = &vec_last(&lx->modes);
        if (c == '{') {
            frame->brace_depth++;
        } else if (c == '}') {
            if (frame->brace_depth == 0) {
                emit(lx, TOK_FSTR_EXPR_END, lo, lo + 1);
                lx->pos++;
                pop_mode(lx);
                return;
            }
            frame->brace_depth--;
        }
    }
    if (lex_operator(lx)) {
        return;
    }
    lx->pos++;
    if (isprint((unsigned char)c)) {
        error_at(lx, lo, lx->pos, "unexpected character `%c`", c);
    } else {
        error_at(lx, lo, lx->pos, "unexpected byte 0x%02X", (unsigned char)c);
    }
}

void lex_source(const Source *src, Arena *arena, Diags *diags, TokenVec *out) {
    Lexer lx;

    memset(&lx, 0, sizeof lx);
    lx.src = src;
    lx.text = src->text;
    lx.len = (uint32_t)src->len;
    lx.arena = arena;
    lx.diags = diags;
    lx.out = out;
    sb_init(&lx.scratch);
    push_mode(&lx, MODE_NORMAL);

    while (!at_end(&lx)) {
        if (mode(&lx) == MODE_FSTR_TEXT) {
            lex_fstring_text(&lx);
        } else {
            lex_normal(&lx);
        }
    }
    while (lx.modes.len > 1) {
        if (mode(&lx) == MODE_FSTR_EXPR) {
            error_at(&lx, lx.len, lx.len, "unterminated expression in f-string");
            emit(&lx, TOK_FSTR_EXPR_END, lx.len, lx.len);
            pop_mode(&lx);
        } else {
            error_at(&lx, lx.len, lx.len, "unterminated f-string");
            emit(&lx, TOK_FSTR_END, lx.len, lx.len);
            pop_mode(&lx);
        }
    }
    emit(&lx, TOK_EOF, lx.len, lx.len);

    sb_free(&lx.scratch);
    vec_free(&lx.modes);
}

/* ---- names and dumping -------------------------------------------------- */

const char *tok_kind_name(TokKind kind) {
    static const char *const NAMES[TOK_KIND_COUNT] = {
        [TOK_EOF] = "end of file",
        [TOK_IDENT] = "identifier",
        [TOK_INT] = "integer literal",
        [TOK_FLOAT] = "float literal",
        [TOK_STRING] = "string literal",
        [TOK_FSTR_START] = "f-string",
        [TOK_FSTR_TEXT] = "f-string text",
        [TOK_FSTR_EXPR_START] = "`{`",
        [TOK_FSTR_EXPR_END] = "`}`",
        [TOK_FSTR_END] = "end of f-string",
        [TOK_KW_FUNC] = "`func`",
        [TOK_KW_STRUCT] = "`struct`",
        [TOK_KW_SELF] = "`self`",
        [TOK_KW_IF] = "`if`",
        [TOK_KW_ELSE] = "`else`",
        [TOK_KW_WHILE] = "`while`",
        [TOK_KW_FOR] = "`for`",
        [TOK_KW_IN] = "`in`",
        [TOK_KW_BREAK] = "`break`",
        [TOK_KW_CONTINUE] = "`continue`",
        [TOK_KW_RETURN] = "`return`",
        [TOK_KW_TRUE] = "`true`",
        [TOK_KW_FALSE] = "`false`",
        [TOK_KW_NULL] = "`null`",
        [TOK_LPAREN] = "`(`",
        [TOK_RPAREN] = "`)`",
        [TOK_LBRACE] = "`{`",
        [TOK_RBRACE] = "`}`",
        [TOK_LBRACKET] = "`[`",
        [TOK_RBRACKET] = "`]`",
        [TOK_COMMA] = "`,`",
        [TOK_DOT] = "`.`",
        [TOK_DOTDOT] = "`..`",
        [TOK_DOTDOTEQ] = "`..=`",
        [TOK_COLON] = "`:`",
        [TOK_SEMI] = "`;`",
        [TOK_ARROW] = "`->`",
        [TOK_QUESTION] = "`?`",
        [TOK_QQ] = "`??`",
        [TOK_ASSIGN] = "`=`",
        [TOK_DEFINE] = "`:=`",
        [TOK_PLUS_ASSIGN] = "`+=`",
        [TOK_MINUS_ASSIGN] = "`-=`",
        [TOK_STAR_ASSIGN] = "`*=`",
        [TOK_SLASH_ASSIGN] = "`/=`",
        [TOK_PERCENT_ASSIGN] = "`%=`",
        [TOK_PLUS] = "`+`",
        [TOK_MINUS] = "`-`",
        [TOK_STAR] = "`*`",
        [TOK_SLASH] = "`/`",
        [TOK_PERCENT] = "`%`",
        [TOK_AMP] = "`&`",
        [TOK_PIPE] = "`|`",
        [TOK_CARET] = "`^`",
        [TOK_TILDE] = "`~`",
        [TOK_SHL] = "`<<`",
        [TOK_SHR] = "`>>`",
        [TOK_EQ] = "`==`",
        [TOK_NE] = "`!=`",
        [TOK_LT] = "`<`",
        [TOK_LE] = "`<=`",
        [TOK_GT] = "`>`",
        [TOK_GE] = "`>=`",
        [TOK_ANDAND] = "`&&`",
        [TOK_OROR] = "`||`",
        [TOK_BANG] = "`!`",
    };
    return kind < TOK_KIND_COUNT && NAMES[kind] ? NAMES[kind] : "token";
}

static void dump_bytes(FILE *out, Str s) {
    fputc('"', out);
    for (size_t i = 0; i < s.n; i++) {
        unsigned char c = (unsigned char)s.p[i];
        if (c == '"' || c == '\\') {
            fprintf(out, "\\%c", c);
        } else if (c >= 32 && c < 127) {
            fputc(c, out);
        } else {
            fprintf(out, "\\x%02X", c);
        }
    }
    fputc('"', out);
}

void tokens_dump(const TokenVec *tokens, const Source *src, FILE *out) {
    for (size_t i = 0; i < tokens->len; i++) {
        const Token *t = &tokens->data[i];
        int line;
        int col;

        source_position(src, t->span.lo, &line, &col);
        fprintf(out, "%4d:%-3d %-16s", line, col, tok_kind_name(t->kind));
        switch (t->kind) {
        case TOK_INT:
            fprintf(out, " %llu", (unsigned long long)t->as.int_val);
            break;
        case TOK_FLOAT:
            fprintf(out, " %.17g", t->as.float_val);
            break;
        case TOK_STRING:
        case TOK_FSTR_TEXT:
            fputc(' ', out);
            dump_bytes(out, t->as.str_val);
            break;
        case TOK_IDENT:
            fprintf(out, " " STR_FMT, STR_ARG(t->text));
            break;
        default:
            break;
        }
        fputc('\n', out);
    }
}
