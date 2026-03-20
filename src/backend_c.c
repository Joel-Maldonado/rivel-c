#include "backend_c.h"

#include <stdbool.h>

typedef struct {
    const char *c_name;
    Type type;
} LocalBinding;

typedef struct {
    StringMap names;
} BackendScope;

typedef struct {
    const Program *program;
    const SemanticContext *semantics;
    Arena *arena;
    StrBuf *output;
    CompileError *error;
    Vec scopes;
    int indent;
    size_t next_local_id;
} Backend;

static bool backend_push_scope(Backend *backend) {
    BackendScope *scope = (BackendScope *)vec_push(&backend->scopes, backend->error);
    if (scope == NULL) {
        return false;
    }
    strmap_init(&scope->names);
    return true;
}

static void backend_pop_scope(Backend *backend) {
    BackendScope *scope;
    if (backend->scopes.len == 0U) {
        return;
    }
    scope = (BackendScope *)vec_get(&backend->scopes, backend->scopes.len - 1U);
    strmap_free(&scope->names);
    backend->scopes.len -= 1U;
}

static void backend_clear_scopes(Backend *backend) {
    while (backend->scopes.len > 0U) {
        backend_pop_scope(backend);
    }
}

static bool backend_emit_line(Backend *backend, const char *line) {
    int depth = 0;
    if (line == NULL) {
        return false;
    }
    if (line[0] == '\0') {
        return strbuf_append_char(backend->output, '\n', backend->error);
    }
    while (depth < backend->indent) {
        if (!strbuf_append_cstr(backend->output, "    ", backend->error)) {
            return false;
        }
        depth += 1;
    }
    if (!strbuf_append_cstr(backend->output, line, backend->error)) {
        return false;
    }
    return strbuf_append_char(backend->output, '\n', backend->error);
}

static const char *backend_c_type(Type type) {
    return type.kind == TYPE_INT ? "int64_t" : "bool";
}

static char *backend_function_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_fn_%.*s", (int)name.len, name.data);
}

static char *backend_global_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_global_%.*s", (int)name.len, name.data);
}

static char *backend_param_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_param_%.*s", (int)name.len, name.data);
}

static char *backend_local_name(Backend *backend, StrSlice name) {
    char *out = arena_printf(backend->arena, backend->error, "rivel_local_%.*s_%zu", (int)name.len, name.data, backend->next_local_id);
    backend->next_local_id += 1U;
    return out;
}

static const LocalBinding *backend_resolve_local(const Backend *backend, StrSlice name) {
    size_t index = backend->scopes.len;
    while (index > 0U) {
        BackendScope *scope = (BackendScope *)vec_get(&backend->scopes, index - 1U);
        uintptr_t value;
        if (strmap_get(&scope->names, name, &value)) {
            return (const LocalBinding *)value;
        }
        index -= 1U;
    }
    return NULL;
}

static char *backend_resolve_name(Backend *backend, StrSlice name) {
    const LocalBinding *binding = backend_resolve_local(backend, name);
    if (binding != NULL) {
        return (char *)binding->c_name;
    }
    if (semantic_lookup_global(backend->semantics, name) != NULL) {
        return backend_global_name(backend, name);
    }
    return arena_copy_cstr(backend->arena, "<unknown>", backend->error);
}

static bool backend_add_local(Backend *backend, StrSlice name, const char *c_name, Type type) {
    BackendScope *scope = (BackendScope *)vec_get(&backend->scopes, backend->scopes.len - 1U);
    LocalBinding *binding = (LocalBinding *)arena_alloc_zero(backend->arena, sizeof(*binding), _Alignof(LocalBinding), backend->error);
    if (binding == NULL) {
        return false;
    }
    binding->c_name = c_name;
    binding->type = type;
    return strmap_set(&scope->names, name, (uintptr_t)binding, false, backend->error);
}

static char *backend_literal(Backend *backend, ConstValue value) {
    if (value.type.kind == TYPE_BOOL) {
        return arena_copy_cstr(backend->arena, value.bool_value ? "true" : "false", backend->error);
    }
    return arena_printf(backend->arena, backend->error, "INT64_C(%lld)", (long long)value.int_value);
}

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

static char *backend_emit_expr(Backend *backend, const Expr *expr);

static char *backend_join_call(Backend *backend, const char *callee, const Vec *args) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_cstr(&buf, callee, backend->error) || !strbuf_append_char(&buf, '(', backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }

    while (index < args->len) {
        Expr *arg = *(Expr **)vec_get(args, index);
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

static char *backend_emit_expr(Backend *backend, const Expr *expr) {
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
            return arena_copy_cstr(backend->arena, "<builtin-print>", backend->error);
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
    return arena_copy_cstr(backend->arena, "<expr>", backend->error);
}

static char *backend_condition_expr(Backend *backend, const Expr *expr) {
    char *emitted = backend_emit_expr(backend, expr);
    if (emitted == NULL) {
        return NULL;
    }
    if (backend_is_wrapped_once(emitted)) {
        return emitted;
    }
    return arena_printf(backend->arena, backend->error, "(%s)", emitted);
}

static char *backend_function_signature(Backend *backend, const Decl *decl) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_fmt(&buf, backend->error, "static %s %s(", backend_c_type(decl->as.function.return_type), backend_function_name(backend, decl->name))) {
        strbuf_free(&buf);
        return NULL;
    }

    if (decl->as.function.params.len == 0U) {
        if (!strbuf_append_cstr(&buf, "void", backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
    } else {
        while (index < decl->as.function.params.len) {
            Param *param = (Param *)vec_get(&decl->as.function.params, index);
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

static bool backend_emit_call_stmt(Backend *backend, const Expr *call_expr) {
    if (slice_equal_cstr(call_expr->as.call.callee, "print")) {
        Expr *arg = *(Expr **)vec_get(&call_expr->as.call.args, 0U);
        char *value = backend_emit_expr(backend, arg);
        if (value == NULL) {
            return false;
        }
        if (arg->inferred_type.kind == TYPE_BOOL) {
            return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_print_bool(%s);", value));
        }
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_print_int(%s);", value));
    }
    return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s;", backend_emit_expr(backend, call_expr)));
}

static bool backend_emit_stmt(Backend *backend, const Stmt *stmt);

static bool backend_emit_block(Backend *backend, const Block *block) {
    size_t index = 0U;
    if (!backend_push_scope(backend)) {
        return false;
    }
    while (index < block->statements.len) {
        const Stmt *stmt = *(Stmt *const *)vec_get(&block->statements, index);
        if (!backend_emit_stmt(backend, stmt)) {
            return false;
        }
        index += 1U;
    }
    backend_pop_scope(backend);
    return true;
}

static bool backend_emit_stmt(Backend *backend, const Stmt *stmt) {
    if (stmt->kind == STMT_BINDING) {
        Type type = stmt->as.binding.has_annotation ? stmt->as.binding.annotation : stmt->as.binding.initializer->inferred_type;
        char *c_name = backend_local_name(backend, stmt->as.binding.name);
        char *value = backend_emit_expr(backend, stmt->as.binding.initializer);
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
        backend->indent += 1;
        if (!backend_emit_block(backend, stmt->as.if_stmt.then_block)) {
            return false;
        }
        backend->indent -= 1;
        if (!backend_emit_line(backend, "}")) {
            return false;
        }
        while (index < stmt->as.if_stmt.elif_branches.len) {
            const IfBranch *branch = (const IfBranch *)vec_get(&stmt->as.if_stmt.elif_branches, index);
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "else if %s {", backend_condition_expr(backend, branch->condition)))) {
                return false;
            }
            backend->indent += 1;
            if (!backend_emit_block(backend, branch->body)) {
                return false;
            }
            backend->indent -= 1;
            if (!backend_emit_line(backend, "}")) {
                return false;
            }
            index += 1U;
        }
        if (stmt->as.if_stmt.else_block != NULL) {
            if (!backend_emit_line(backend, "else {")) {
                return false;
            }
            backend->indent += 1;
            if (!backend_emit_block(backend, stmt->as.if_stmt.else_block)) {
                return false;
            }
            backend->indent -= 1;
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
        backend->indent += 1;
        if (!backend_emit_block(backend, stmt->as.while_stmt.body)) {
            return false;
        }
        backend->indent -= 1;
        return backend_emit_line(backend, "}");
    }
    return true;
}

static bool backend_emit_function(Backend *backend, const Decl *decl) {
    size_t index = 0U;
    char *signature = backend_function_signature(backend, decl);

    if (signature == NULL) {
        return false;
    }
    if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s {", signature))) {
        return false;
    }

    backend->indent += 1;
    backend->next_local_id = 0U;
    backend_clear_scopes(backend);
    if (!backend_push_scope(backend)) {
        return false;
    }
    while (index < decl->as.function.params.len) {
        Param *param = (Param *)vec_get(&decl->as.function.params, index);
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
    backend->indent -= 1;
    return backend_emit_line(backend, "}");
}

static bool backend_emit_prelude(Backend *backend) {
    return backend_emit_line(backend, "#include <stdbool.h>")
        && backend_emit_line(backend, "#include <stdint.h>")
        && backend_emit_line(backend, "#include <stdio.h>")
        && backend_emit_line(backend, "#include <stdlib.h>")
        && backend_emit_line(backend, "")
        && backend_emit_line(backend, "static int rivel_exit_code(int64_t value) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "return (int)value;")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && backend_emit_line(backend, "")
        && backend_emit_line(backend, "static void rivel_check_divisor(int64_t rhs) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "if (rhs == INT64_C(0)) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "fputs(\"division by zero\\n\", stderr);")
        && backend_emit_line(backend, "exit(1);")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && backend_emit_line(backend, "")
        && backend_emit_line(backend, "static int64_t rivel_div(int64_t lhs, int64_t rhs) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "rivel_check_divisor(rhs);")
        && backend_emit_line(backend, "return lhs / rhs;")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && backend_emit_line(backend, "")
        && backend_emit_line(backend, "static int64_t rivel_mod(int64_t lhs, int64_t rhs) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "rivel_check_divisor(rhs);")
        && backend_emit_line(backend, "return lhs % rhs;")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && backend_emit_line(backend, "")
        && backend_emit_line(backend, "static void rivel_print_int(int64_t value) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "printf(\"%lld\\n\", (long long)value);")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && backend_emit_line(backend, "")
        && backend_emit_line(backend, "static void rivel_print_bool(bool value) {")
        && (++backend->indent, true)
        && backend_emit_line(backend, "puts(value ? \"true\" : \"false\");")
        && (--backend->indent, true)
        && backend_emit_line(backend, "}")
        && backend_emit_line(backend, "");
}

bool c_backend_generate(const Program *program, const SemanticContext *semantics, Arena *arena, StrBuf *output, CompileError *error) {
    Backend backend;
    size_t index = 0U;

    backend.program = program;
    backend.semantics = semantics;
    backend.arena = arena;
    backend.output = output;
    backend.error = error;
    backend.indent = 0;
    backend.next_local_id = 0U;
    vec_init(&backend.scopes, sizeof(BackendScope), NULL);
    strbuf_clear(output);

    if (!backend_emit_prelude(&backend)) {
        backend_clear_scopes(&backend);
        vec_free(&backend.scopes);
        return false;
    }

    while (index < program->decls.len) {
        const Decl *decl = *(Decl *const *)vec_get(&program->decls, index);
        if (decl->kind == DECL_GLOBAL_CONST) {
            const GlobalConstInfo *info = semantic_lookup_global(semantics, decl->name);
            if (!backend_emit_line(&backend, arena_printf(arena, error, "static const %s %s = %s;",
                                                         backend_c_type(info->type),
                                                         backend_global_name(&backend, decl->name),
                                                         backend_literal(&backend, info->value)))) {
                backend_clear_scopes(&backend);
                vec_free(&backend.scopes);
                return false;
            }
        }
        index += 1U;
    }
    if (program->decls.len > 0U && !backend_emit_line(&backend, "")) {
        backend_clear_scopes(&backend);
        vec_free(&backend.scopes);
        return false;
    }

    index = 0U;
    while (index < program->decls.len) {
        const Decl *decl = *(Decl *const *)vec_get(&program->decls, index);
        if (decl->kind == DECL_FUNCTION && !backend_emit_line(&backend, arena_printf(arena, error, "%s;", backend_function_signature(&backend, decl)))) {
            backend_clear_scopes(&backend);
            vec_free(&backend.scopes);
            return false;
        }
        index += 1U;
    }
    if (!backend_emit_line(&backend, "")) {
        backend_clear_scopes(&backend);
        vec_free(&backend.scopes);
        return false;
    }

    index = 0U;
    while (index < program->decls.len) {
        const Decl *decl = *(Decl *const *)vec_get(&program->decls, index);
        if (decl->kind == DECL_FUNCTION) {
            if (!backend_emit_function(&backend, decl) || !backend_emit_line(&backend, "")) {
                backend_clear_scopes(&backend);
                vec_free(&backend.scopes);
                return false;
            }
        }
        index += 1U;
    }

    if (!backend_emit_line(&backend, "int main(void) {")
        || (++backend.indent, true) == false
        || !backend_emit_line(&backend, "return rivel_exit_code(rivel_fn_main());")
        || (--backend.indent, true) == false
        || !backend_emit_line(&backend, "}")) {
        backend_clear_scopes(&backend);
        vec_free(&backend.scopes);
        return false;
    }

    backend_clear_scopes(&backend);
    vec_free(&backend.scopes);
    return true;
}
