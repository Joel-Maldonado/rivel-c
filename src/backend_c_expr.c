#include "backend_c_internal.h"

static bool backend_is_wrapped_once(const char *text) {
    int depth = 0;
    size_t index = 0U;

    if (text[0] != '(') {
        return false;
    }
    while (text[index] != '\0') {
        if (text[index] == '(') {
            depth += 1;
        } else if (text[index] == ')') {
            depth -= 1;
            if (depth == 0 && text[index + 1U] != '\0') {
                return false;
            }
        }
        index += 1U;
    }
    return depth == 0;
}

static char *backend_join_call(Backend *backend, const char *callee, const ExprList *args) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_cstr(&buf, callee, backend->error) || !strbuf_append_char(&buf, '(', backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }

    while (index < expr_list_len(args)) {
        Expr *arg = expr_list_get(args, index);
        char *emitted = backend_emit_expr(backend, arg);
        if (emitted == NULL) {
            strbuf_free(&buf);
            return NULL;
        }
        if (index > 0U && !strbuf_append_cstr(&buf, ", ", backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
        if (!strbuf_append_cstr(&buf, emitted, backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
        index += 1U;
    }

    if (!strbuf_append_char(&buf, ')', backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }

    copy = arena_copy_cstr(backend->arena, strbuf_cstr(&buf), backend->error);
    strbuf_free(&buf);
    return copy;
}

char *backend_emit_expr(Backend *backend, const Expr *expr) {
    if (expr->kind == EXPR_INT) {
        return arena_printf(backend->arena, backend->error, "INT64_C(%lld)", (long long)expr->as.int_value);
    }
    if (expr->kind == EXPR_BOOL) {
        return arena_copy_cstr(backend->arena, expr->as.bool_value ? "true" : "false", backend->error);
    }
    if (expr->kind == EXPR_NAME) {
        return backend_resolve_name(backend, expr->as.name);
    }
    if (expr->kind == EXPR_CALL) {
        if (slice_equal_cstr(expr->as.call.callee, "print")) {
            error_set(backend->error, "Backend", "Internal error: builtin `print` reached expression emitter");
            return NULL;
        }
        return backend_join_call(backend, backend_function_name(backend, expr->as.call.callee), &expr->as.call.args);
    }
    if (expr->kind == EXPR_UNARY) {
        char *operand = backend_emit_expr(backend, expr->as.unary.operand);
        if (operand == NULL) {
            return NULL;
        }
        if (expr->as.unary.op == TOKEN_MINUS) {
            return arena_printf(backend->arena, backend->error, "(-%s)", operand);
        }
        return arena_printf(backend->arena, backend->error, "(!%s)", operand);
    }
    if (expr->kind == EXPR_BINARY) {
        char *lhs = backend_emit_expr(backend, expr->as.binary.lhs);
        char *rhs = backend_emit_expr(backend, expr->as.binary.rhs);
        if (lhs == NULL || rhs == NULL) {
            return NULL;
        }
        if (expr->as.binary.op == TOKEN_KW_AND) {
            return arena_printf(backend->arena, backend->error, "(%s && %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_KW_OR) {
            return arena_printf(backend->arena, backend->error, "(%s || %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_PLUS) {
            return arena_printf(backend->arena, backend->error, "(%s + %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_MINUS) {
            return arena_printf(backend->arena, backend->error, "(%s - %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_STAR) {
            return arena_printf(backend->arena, backend->error, "(%s * %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_SLASH) {
            return arena_printf(backend->arena, backend->error, "rivel_div(%s, %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_PERCENT) {
            return arena_printf(backend->arena, backend->error, "rivel_mod(%s, %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_EQ_EQ) {
            return arena_printf(backend->arena, backend->error, "(%s == %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_BANG_EQ) {
            return arena_printf(backend->arena, backend->error, "(%s != %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_LESS) {
            return arena_printf(backend->arena, backend->error, "(%s < %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_LESS_EQ) {
            return arena_printf(backend->arena, backend->error, "(%s <= %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_GREATER) {
            return arena_printf(backend->arena, backend->error, "(%s > %s)", lhs, rhs);
        }
        if (expr->as.binary.op == TOKEN_GREATER_EQ) {
            return arena_printf(backend->arena, backend->error, "(%s >= %s)", lhs, rhs);
        }
    }

    error_set(backend->error, "Backend", "Internal error: unsupported expression kind during C emission");
    return NULL;
}

char *backend_condition_expr(Backend *backend, const Expr *expr) {
    char *emitted = backend_emit_expr(backend, expr);

    if (emitted == NULL) {
        return NULL;
    }
    if (backend_is_wrapped_once(emitted)) {
        return emitted;
    }
    return arena_printf(backend->arena, backend->error, "(%s)", emitted);
}
