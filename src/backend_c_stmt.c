#include "backend_c_internal.h"

static char *backend_range_name(Backend *backend, const char *prefix) {
    char *name = arena_printf(backend->arena, backend->error, "rivel_range_%s_%zu", prefix, backend->next_local_id);

    backend->next_local_id += 1U;
    return name;
}

static bool backend_emit_assignment_store(Backend *backend, Type target_type, const char *target_name, const char *value) {
    if (target_type.kind == TYPE_STRING || backend_type_contains_owned_strings(backend, target_type)) {
        char *temp_name = backend_temp_name(backend, "assign_value");

        if (temp_name == NULL) {
            return false;
        }
        if (!backend_emit_line(backend,
                               arena_printf(backend->arena,
                                            backend->error,
                                            "%s %s = %s;",
                                            backend_c_type(backend, target_type),
                                            temp_name,
                                            value))
            || !backend_emit_release_value(backend, target_type, target_name)) {
            return false;
        }
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s = %s;", target_name, temp_name));
    }
    return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s = %s;", target_name, value));
}

static bool backend_emit_return_stmt(Backend *backend, const Expr *value_expr) {
    Type value_type;
    char *value;
    char *temp_name;

    if (!backend_expr_type_checked(backend, value_expr, &value_type)) {
        return false;
    }
    value = backend_emit_expr(backend, value_expr);
    temp_name = backend_temp_name(backend, "return_value");
    if (value == NULL || temp_name == NULL) {
        return false;
    }
    if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s %s = %s;", backend_c_type(backend, value_type), temp_name, value))) {
        return false;
    }
    if (!backend_emit_all_scope_releases(backend)) {
        return false;
    }
    return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "return %s;", temp_name));
}

static bool backend_emit_print_call(Backend *backend, const Expr *arg, bool newline) {
    Type arg_type;
    const char *int_name = newline ? "rivel_println_int" : "rivel_print_int_inline";
    const char *bool_name = newline ? "rivel_println_bool" : "rivel_print_bool_inline";
    const char *double_name = newline ? "rivel_println_double" : "rivel_print_double_inline";
    const char *string_name = newline ? "rivel_println_string_take" : "rivel_print_string_take_inline";
    char *value = backend_emit_expr(backend, arg);

    if (value == NULL) {
        return false;
    }
    if (!expr_resolved_type(arg, &arg_type)) {
        return error_set(backend->error, "Backend", "Internal error: missing semantic type for builtin print argument");
    }
    if (arg_type.kind == TYPE_BOOL) {
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s(%s);", bool_name, value));
    }
    if (arg_type.kind == TYPE_DOUBLE) {
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s(%s);", double_name, value));
    }
    if (arg_type.kind == TYPE_STRING) {
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s(%s);", string_name, value));
    }
    return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s(%s);", int_name, value));
}

char *backend_function_signature(Backend *backend, const Decl *decl) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_fmt(&buf, backend->error, "static %s %s(", backend_c_type(backend, decl->as.function.return_type), backend_function_name(backend, decl->name))) {
        strbuf_free(&buf);
        return NULL;
    }

    if (param_list_len(&decl->as.function.params) == 0U) {
        if (!strbuf_append_cstr(&buf, "void", backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
    } else {
        while (index < param_list_len(&decl->as.function.params)) {
            const Param *param = param_list_get_const(&decl->as.function.params, index);
            if (index > 0U && !strbuf_append_cstr(&buf, ", ", backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
            if (!strbuf_append_fmt(&buf, backend->error, "%s %s", backend_c_type(backend, param->type), backend_param_name(backend, param->name))) {
                strbuf_free(&buf);
                return NULL;
            }
            index += 1U;
        }
    }

    if (!strbuf_append_char(&buf, ')', backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }

    copy = arena_copy_cstr(backend->arena, strbuf_cstr(&buf), backend->error);
    strbuf_free(&buf);
    return copy;
}

bool backend_emit_call_stmt(Backend *backend, const Expr *call_expr) {
    if (slice_equal_cstr(call_expr->as.call.callee, "print")) {
        return backend_emit_print_call(backend, expr_list_get(&call_expr->as.call.args, 0U), false);
    }
    if (slice_equal_cstr(call_expr->as.call.callee, "println")) {
        return backend_emit_print_call(backend, expr_list_get(&call_expr->as.call.args, 0U), true);
    }

    Type call_type;
    char *value;

    if (!backend_expr_type_checked(backend, call_expr, &call_type)) {
        return false;
    }
    value = backend_emit_expr(backend, call_expr);
    if (value == NULL) {
        return false;
    }
    if (call_type.kind == TYPE_STRING || backend_type_contains_owned_strings(backend, call_type)) {
        return backend_emit_release_value(backend, call_type, value);
    }
    return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s;", value));
}

bool backend_emit_block(Backend *backend, const Block *block) {
    size_t index = 0U;

    if (!backend_push_scope(backend)) {
        return false;
    }
    while (index < stmt_list_len(&block->statements)) {
        const Stmt *stmt = stmt_list_get(&block->statements, index);
        if (!backend_emit_stmt(backend, stmt)) {
            return false;
        }
        index += 1U;
    }
    if (!backend_emit_scope_releases(backend, backend_scope_stack_len(&backend->scopes) - 1U)) {
        return false;
    }
    backend_pop_scope(backend);
    return true;
}

bool backend_emit_stmt(Backend *backend, const Stmt *stmt) {
    if (stmt->kind == STMT_BINDING) {
        Type type;
        char *c_name = backend_local_name(backend, stmt->as.binding.name);
        char *value = backend_emit_expr(backend, stmt->as.binding.initializer);

        if (stmt->as.binding.has_annotation) {
            type = stmt->as.binding.annotation;
        } else if (!backend_expr_type_checked(backend, stmt->as.binding.initializer, &type)) {
            return false;
        }
        if (c_name == NULL || value == NULL) {
            return false;
        }
        if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s %s = %s;", backend_c_type(backend, type), c_name, value))) {
            return false;
        }
        return backend_add_local(backend, stmt->as.binding.name, c_name, type);
    }
    if (stmt->kind == STMT_ASSIGN) {
        const LocalBinding *binding;
        char *value = backend_emit_expr(backend, stmt->as.assign.value);
        Type target_type;

        if (!backend_expr_type_checked(backend, stmt->as.assign.target, &target_type)) {
            return false;
        }

        if (stmt->as.assign.target->kind == EXPR_NAME) {
            StrSlice target_name = stmt->as.assign.target->as.name;
            char *name = backend_resolve_name(backend, target_name);

            binding = backend_resolve_local(backend, target_name);
            if (binding == NULL) {
                return error_set(backend->error, "Backend", "Internal error: unresolved assignment target `%.*s` during C emission", (int)target_name.len, target_name.data);
            }
            if (name == NULL || value == NULL) {
                return false;
            }
            return backend_emit_assignment_store(backend, target_type, name, value);
        }

        if (stmt->as.assign.target->kind == EXPR_FIELD) {
            const Expr *base_expr = stmt->as.assign.target->as.field.base;
            char *base_name;
            char *field_lvalue;
            const StructFieldDecl *field_decl;

            if (base_expr->kind != EXPR_NAME) {
                return error_set(backend->error, "Backend", "Internal error: field assignment base must be a local name");
            }
            binding = backend_resolve_local(backend, base_expr->as.name);
            base_name = backend_resolve_name(backend, base_expr->as.name);
            if (binding == NULL) {
                return error_set(backend->error, "Backend", "Internal error: unresolved field assignment base `%.*s` during C emission", (int)base_expr->as.name.len, base_expr->as.name.data);
            }
            field_decl = backend_lookup_struct_field(backend, binding->type.struct_name, stmt->as.assign.target->as.field.name);
            if (field_decl == NULL) {
                return false;
            }
            field_lvalue = arena_printf(backend->arena,
                                        backend->error,
                                        "%s.%.*s",
                                        base_name,
                                        (int)field_decl->name.len,
                                        field_decl->name.data);
            if (base_name == NULL || field_lvalue == NULL || value == NULL) {
                return false;
            }
            return backend_emit_assignment_store(backend, target_type, field_lvalue, value);
        }
        return error_set(backend->error, "Backend", "Internal error: unsupported assignment target during C emission");
    }
    if (stmt->kind == STMT_RETURN) {
        return backend_emit_return_stmt(backend, stmt->as.ret.value);
    }
    if (stmt->kind == STMT_CALL) {
        return backend_emit_call_stmt(backend, stmt->as.call.call);
    }
    if (stmt->kind == STMT_IF) {
        size_t index = 0U;

        if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "if %s {", backend_condition_expr(backend, stmt->as.if_stmt.condition)))) {
            return false;
        }
        backend_indent_push(backend);
        if (!backend_emit_block(backend, stmt->as.if_stmt.then_block)) {
            return false;
        }
        backend_indent_pop(backend);
        if (!backend_emit_line(backend, "}")) {
            return false;
        }
        while (index < if_branch_list_len(&stmt->as.if_stmt.elif_branches)) {
            const IfBranch *branch = if_branch_list_get_const(&stmt->as.if_stmt.elif_branches, index);
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "else if %s {", backend_condition_expr(backend, branch->condition)))) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_block(backend, branch->body)) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
            index += 1U;
        }
        if (stmt->as.if_stmt.else_block != NULL) {
            if (!backend_emit_line(backend, "else {")) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_block(backend, stmt->as.if_stmt.else_block)) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
        }
        return true;
    }
    if (stmt->kind == STMT_WHILE) {
        if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "while %s {", backend_condition_expr(backend, stmt->as.while_stmt.condition)))) {
            return false;
        }
        backend_indent_push(backend);
        if (!backend_emit_block(backend, stmt->as.while_stmt.body)) {
            return false;
        }
        backend_indent_pop(backend);
        return backend_emit_line(backend, "}");
    }
    if (stmt->kind == STMT_FOR_RANGE) {
        Type int_type = type_make_int();
        char *start_value = backend_emit_expr(backend, stmt->as.for_range.start);
        char *end_value = backend_emit_expr(backend, stmt->as.for_range.end);
        char *start_name = backend_range_name(backend, "start");
        char *end_name = backend_range_name(backend, "end");
        char *loop_name = backend_local_name(backend, stmt->as.for_range.name);

        if (start_value == NULL || end_value == NULL || start_name == NULL || end_name == NULL || loop_name == NULL) {
            return false;
        }
        if (!backend_emit_line(backend, "{")) {
            return false;
        }
        backend_indent_push(backend);
        if (!backend_push_scope(backend)) {
            return false;
        }
        if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "int64_t %s = %s;", start_name, start_value))) {
            return false;
        }
        if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "int64_t %s = %s;", end_name, end_value))) {
            return false;
        }
        if (!backend_add_local(backend, stmt->as.for_range.name, loop_name, int_type)) {
            return false;
        }
        if (stmt->as.for_range.is_inclusive) {
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "if (%s <= %s) {", start_name, end_name))) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "int64_t %s = %s;", loop_name, start_name))) {
                return false;
            }
            if (!backend_emit_line(backend, "while (true) {")) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_block(backend, stmt->as.for_range.body)) {
                return false;
            }
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "if (%s == %s) {", loop_name, end_name))) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_line(backend, "break;")) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s += INT64_C(1);", loop_name))) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
        } else {
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "if (%s < %s) {", start_name, end_name))) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "int64_t %s = %s;", loop_name, start_name))) {
                return false;
            }
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "while (%s < %s) {", loop_name, end_name))) {
                return false;
            }
            backend_indent_push(backend);
            if (!backend_emit_block(backend, stmt->as.for_range.body)) {
                return false;
            }
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s += INT64_C(1);", loop_name))) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
        }
        backend_pop_scope(backend);
        backend_indent_pop(backend);
        return backend_emit_line(backend, "}");
    }
    return true;
}

bool backend_emit_function(Backend *backend, const Decl *decl) {
    size_t index = 0U;
    char *signature = backend_function_signature(backend, decl);

    if (signature == NULL) {
        return false;
    }
    if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s {", signature))) {
        return false;
    }

    backend_indent_push(backend);
    backend->next_local_id = 0U;
    backend_clear_scopes(backend);
    if (!backend_push_scope(backend)) {
        return false;
    }
    while (index < param_list_len(&decl->as.function.params)) {
        const Param *param = param_list_get_const(&decl->as.function.params, index);
        char *c_name = backend_param_name(backend, param->name);
        if (c_name == NULL || !backend_add_local(backend, param->name, c_name, param->type)) {
            return false;
        }
        index += 1U;
    }
    if (!backend_emit_block(backend, decl->as.function.body)) {
        return false;
    }
    backend_clear_scopes(backend);
    backend_indent_pop(backend);
    return backend_emit_line(backend, "}");
}
