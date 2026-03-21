#include "backend_c_internal.h"

char *backend_function_signature(Backend *backend, const Decl *decl) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_fmt(&buf, backend->error, "static %s %s(", backend_c_type(decl->as.function.return_type), backend_function_name(backend, decl->name))) {
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
            if (!strbuf_append_fmt(&buf, backend->error, "%s %s", backend_c_type(param->type), backend_param_name(backend, param->name))) {
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
        Expr *arg = expr_list_get(&call_expr->as.call.args, 0U);
        Type arg_type;
        char *value = backend_emit_expr(backend, arg);

        if (value == NULL) {
            return false;
        }
        if (!semantic_expr_type(backend->semantics, arg, &arg_type)) {
            return error_set(backend->error, "Backend", "Internal error: missing semantic type for builtin print argument");
        }
        if (arg_type.kind == TYPE_BOOL) {
            return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_print_bool(%s);", value));
        }
        if (arg_type.kind == TYPE_DOUBLE) {
            return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_print_double(%s);", value));
        }
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_print_int(%s);", value));
    }
    return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s;", backend_emit_expr(backend, call_expr)));
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
        } else if (!semantic_expr_type(backend->semantics, stmt->as.binding.initializer, &type)) {
            return error_set(backend->error, "Backend", "Internal error: missing semantic type for binding initializer");
        }
        if (c_name == NULL || value == NULL) {
            return false;
        }
        if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s %s = %s;", backend_c_type(type), c_name, value))) {
            return false;
        }
        return backend_add_local(backend, stmt->as.binding.name, c_name, type);
    }
    if (stmt->kind == STMT_ASSIGN) {
        char *name = backend_resolve_name(backend, stmt->as.assign.name);
        char *value = backend_emit_expr(backend, stmt->as.assign.value);

        if (name == NULL || value == NULL) {
            return false;
        }
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s = %s;", name, value));
    }
    if (stmt->kind == STMT_RETURN) {
        char *value = backend_emit_expr(backend, stmt->as.ret.value);
        if (value == NULL) {
            return false;
        }
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "return %s;", value));
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
