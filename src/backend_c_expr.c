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

static char *backend_emit_struct_literal_expr(Backend *backend, const Expr *expr) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_fmt(&buf,
                           backend->error,
                           "((%s){",
                           backend_c_type(backend, type_make_struct(expr->as.struct_literal.struct_name, NULL)))) {
        strbuf_free(&buf);
        return NULL;
    }
    while (index < struct_literal_field_list_len(&expr->as.struct_literal.fields)) {
        const StructLiteralField *field = struct_literal_field_list_get_const(&expr->as.struct_literal.fields, index);
        char *value = backend_emit_expr(backend, field->value);

        if (value == NULL) {
            strbuf_free(&buf);
            return NULL;
        }
        if (index > 0U && !strbuf_append_cstr(&buf, ", ", backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
        if (!strbuf_append_fmt(&buf, backend->error, ".%.*s = %s", (int)field->name.len, field->name.data, value)) {
            strbuf_free(&buf);
            return NULL;
        }
        index += 1U;
    }
    if (!strbuf_append_cstr(&buf, "})", backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }

    copy = arena_copy_cstr(backend->arena, strbuf_cstr(&buf), backend->error);
    strbuf_free(&buf);
    return copy;
}

static char *backend_emit_field_expr(Backend *backend, const Expr *expr) {
    Type base_type;
    Type field_type;
    const StructFieldDecl *field_decl;
    char *base_value;

    if (!backend_expr_type_checked(backend, expr->as.field.base, &base_type)
        || !backend_expr_type_checked(backend, expr, &field_type)) {
        return NULL;
    }
    field_decl = backend_lookup_struct_field(backend, base_type.struct_name, expr->as.field.name);
    if (field_decl == NULL) {
        return NULL;
    }
    base_value = backend_emit_expr(backend, expr->as.field.base);
    if (base_value == NULL) {
        return NULL;
    }
    if (backend_type_contains_owned_strings(backend, base_type)) {
        return arena_printf(backend->arena,
                            backend->error,
                            "%s(%s)",
                            backend_struct_take_field_name(backend, base_type.struct_name, field_decl->name),
                            base_value);
    }
    return arena_printf(backend->arena, backend->error, "(%s.%.*s)", base_value, (int)expr->as.field.name.len, expr->as.field.name.data);
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

static char *backend_emit_name_expr(Backend *backend, const Expr *expr) {
    Type type;
    char *name;

    if (!backend_expr_type_checked(backend, expr, &type)) {
        return NULL;
    }
    name = backend_resolve_name(backend, expr->as.name);
    if (name == NULL) {
        return NULL;
    }
    return backend_retain_value_expr(backend, type, name);
}

static char *backend_emit_builtin_call_expr(Backend *backend, const Expr *expr) {
    char *arg0;
    char *arg1;
    char *arg2;
    Type arg0_type;

    if (slice_equal_cstr(expr->as.call.callee, "print") || slice_equal_cstr(expr->as.call.callee, "println")) {
        error_set(backend->error, "Backend", "Internal error: statement-only builtin reached expression emitter");
        return NULL;
    }

    arg0 = backend_emit_expr(backend, expr_list_get(&expr->as.call.args, 0U));
    if (arg0 == NULL) {
        return NULL;
    }
    if (slice_equal_cstr(expr->as.call.callee, "__stringify")) {
        if (!backend_expr_type_checked(backend, expr_list_get(&expr->as.call.args, 0U), &arg0_type)) {
            return NULL;
        }
        if (arg0_type.kind == TYPE_STRING) {
            return arg0;
        }
        if (arg0_type.kind == TYPE_BOOL) {
            return arena_printf(backend->arena, backend->error, "rivel_string_from_bool(%s)", arg0);
        }
        if (arg0_type.kind == TYPE_DOUBLE) {
            return arena_printf(backend->arena, backend->error, "rivel_string_from_double(%s)", arg0);
        }
        return arena_printf(backend->arena, backend->error, "rivel_string_from_int(%s)", arg0);
    }
    if (slice_equal_cstr(expr->as.call.callee, "len")) {
        return arena_printf(backend->arena, backend->error, "rivel_string_len_take(%s)", arg0);
    }

    if (slice_equal_cstr(expr->as.call.callee, "substr")) {
        arg1 = backend_emit_expr(backend, expr_list_get(&expr->as.call.args, 1U));
        arg2 = backend_emit_expr(backend, expr_list_get(&expr->as.call.args, 2U));
        if (arg1 == NULL || arg2 == NULL) {
            return NULL;
        }
        return arena_printf(backend->arena, backend->error, "rivel_string_substr_take(%s, %s, %s)", arg0, arg1, arg2);
    }

    if (slice_equal_cstr(expr->as.call.callee, "contains")) {
        arg1 = backend_emit_expr(backend, expr_list_get(&expr->as.call.args, 1U));
        if (arg1 == NULL) {
            return NULL;
        }
        return arena_printf(backend->arena, backend->error, "rivel_string_contains_take(%s, %s)", arg0, arg1);
    }
    if (slice_equal_cstr(expr->as.call.callee, "starts_with")) {
        arg1 = backend_emit_expr(backend, expr_list_get(&expr->as.call.args, 1U));
        if (arg1 == NULL) {
            return NULL;
        }
        return arena_printf(backend->arena, backend->error, "rivel_string_starts_with_take(%s, %s)", arg0, arg1);
    }
    if (slice_equal_cstr(expr->as.call.callee, "ends_with")) {
        arg1 = backend_emit_expr(backend, expr_list_get(&expr->as.call.args, 1U));
        if (arg1 == NULL) {
            return NULL;
        }
        return arena_printf(backend->arena, backend->error, "rivel_string_ends_with_take(%s, %s)", arg0, arg1);
    }

    error_set(backend->error, "Backend", "Internal error: unknown builtin `%.*s` reached expression emitter", (int)expr->as.call.callee.len, expr->as.call.callee.data);
    return NULL;
}

static char *backend_emit_binary_expr(Backend *backend, const Expr *expr) {
    Type lhs_type;
    Type result_type;
    char *lhs = backend_emit_expr(backend, expr->as.binary.lhs);
    char *rhs = backend_emit_expr(backend, expr->as.binary.rhs);

    if (lhs == NULL || rhs == NULL) {
        return NULL;
    }
    if (!backend_expr_type_checked(backend, expr->as.binary.lhs, &lhs_type)
        || !backend_expr_type_checked(backend, expr, &result_type)) {
        return NULL;
    }

    if (expr->as.binary.op == TOKEN_KW_AND) {
        return arena_printf(backend->arena, backend->error, "(%s && %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_KW_OR) {
        return arena_printf(backend->arena, backend->error, "(%s || %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_PLUS) {
        if (lhs_type.kind == TYPE_STRING) {
            return arena_printf(backend->arena, backend->error, "rivel_string_concat_take(%s, %s)", lhs, rhs);
        }
        return arena_printf(backend->arena, backend->error, "(%s + %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_MINUS) {
        return arena_printf(backend->arena, backend->error, "(%s - %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_STAR) {
        return arena_printf(backend->arena, backend->error, "(%s * %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_SLASH) {
        if (result_type.kind == TYPE_INT) {
            return arena_printf(backend->arena, backend->error, "rivel_div(%s, %s)", lhs, rhs);
        }
        return arena_printf(backend->arena, backend->error, "(%s / %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_PERCENT) {
        return arena_printf(backend->arena, backend->error, "rivel_mod(%s, %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_EQ_EQ) {
        if (lhs_type.kind == TYPE_STRING) {
            return arena_printf(backend->arena, backend->error, "rivel_string_eq_take(%s, %s)", lhs, rhs);
        }
        return arena_printf(backend->arena, backend->error, "(%s == %s)", lhs, rhs);
    }
    if (expr->as.binary.op == TOKEN_BANG_EQ) {
        if (lhs_type.kind == TYPE_STRING) {
            return arena_printf(backend->arena, backend->error, "(!rivel_string_eq_take(%s, %s))", lhs, rhs);
        }
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

    error_set(backend->error, "Backend", "Internal error: unsupported binary operator during C emission");
    return NULL;
}

char *backend_emit_expr(Backend *backend, const Expr *expr) {
    if (expr->kind == EXPR_INT) {
        return arena_printf(backend->arena, backend->error, "INT64_C(%lld)", (long long)expr->as.int_value);
    }
    if (expr->kind == EXPR_DOUBLE) {
        return backend_double_literal(backend, expr->as.double_value);
    }
    if (expr->kind == EXPR_BOOL) {
        return arena_copy_cstr(backend->arena, expr->as.bool_value ? "true" : "false", backend->error);
    }
    if (expr->kind == EXPR_STRING) {
        return backend_literal(backend, const_value_make_string(expr->as.string_value));
    }
    if (expr->kind == EXPR_NAME) {
        return backend_emit_name_expr(backend, expr);
    }
    if (expr->kind == EXPR_CALL) {
        if (semantic_lookup_builtin(backend->semantics, expr->as.call.callee) != NULL) {
            return backend_emit_builtin_call_expr(backend, expr);
        }
        return backend_join_call(backend, backend_function_name(backend, expr->as.call.callee), &expr->as.call.args);
    }
    if (expr->kind == EXPR_STRUCT_LITERAL) {
        return backend_emit_struct_literal_expr(backend, expr);
    }
    if (expr->kind == EXPR_FIELD) {
        return backend_emit_field_expr(backend, expr);
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
        return backend_emit_binary_expr(backend, expr);
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
