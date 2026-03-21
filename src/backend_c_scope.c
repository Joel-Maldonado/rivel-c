#include "backend_c_internal.h"

#include <math.h>
#include <string.h>

void backend_binding_table_init(BackendBindingTable *table) {
    strmap_init(&table->storage);
}

void backend_binding_table_free(BackendBindingTable *table) {
    strmap_free(&table->storage);
}

bool backend_binding_table_set(BackendBindingTable *table, StrSlice name, const LocalBinding *binding, CompileError *error) {
    return strmap_set(&table->storage, name, (uintptr_t)binding, false, error);
}

const LocalBinding *backend_binding_table_get(const BackendBindingTable *table, StrSlice name) {
    uintptr_t value;

    if (!strmap_get(&table->storage, name, &value)) {
        return NULL;
    }
    return (const LocalBinding *)value;
}

void backend_scope_stack_init(BackendScopeStack *stack, Arena *arena) {
    vec_init(&stack->storage, sizeof(BackendScope), arena);
}

void backend_scope_stack_free(BackendScopeStack *stack) {
    vec_free(&stack->storage);
}

size_t backend_scope_stack_len(const BackendScopeStack *stack) {
    return stack->storage.len;
}

BackendScope *backend_scope_stack_push(BackendScopeStack *stack, CompileError *error) {
    return (BackendScope *)vec_push(&stack->storage, error);
}

BackendScope *backend_scope_stack_get(BackendScopeStack *stack, size_t index) {
    return (BackendScope *)vec_get(&stack->storage, index);
}

const BackendScope *backend_scope_stack_get_const(const BackendScopeStack *stack, size_t index) {
    return (const BackendScope *)vec_get(&stack->storage, index);
}

void backend_scope_stack_pop(BackendScopeStack *stack) {
    if (stack->storage.len > 0U) {
        stack->storage.len -= 1U;
    }
}

bool backend_push_scope(Backend *backend) {
    BackendScope *scope = backend_scope_stack_push(&backend->scopes, backend->error);

    if (scope == NULL) {
        return false;
    }
    backend_binding_table_init(&scope->bindings);
    vec_init(&scope->ordered_bindings, sizeof(const LocalBinding *), NULL);
    return true;
}

void backend_pop_scope(Backend *backend) {
    BackendScope *scope;

    if (backend_scope_stack_len(&backend->scopes) == 0U) {
        return;
    }
    scope = backend_scope_stack_get(&backend->scopes, backend_scope_stack_len(&backend->scopes) - 1U);
    backend_binding_table_free(&scope->bindings);
    vec_free(&scope->ordered_bindings);
    backend_scope_stack_pop(&backend->scopes);
}

void backend_clear_scopes(Backend *backend) {
    while (backend_scope_stack_len(&backend->scopes) > 0U) {
        backend_pop_scope(backend);
    }
}

bool backend_emit_line(Backend *backend, const char *line) {
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

void backend_indent_push(Backend *backend) {
    backend->indent += 1;
}

void backend_indent_pop(Backend *backend) {
    if (backend->indent > 0) {
        backend->indent -= 1;
    }
}

const char *backend_c_type(Type type) {
    switch (type.kind) {
        case TYPE_INT:
            return "int64_t";
        case TYPE_DOUBLE:
            return "double";
        case TYPE_BOOL:
            return "bool";
        case TYPE_STRING:
            return "RivelString";
    }
    return "<type>";
}

char *backend_double_literal(Backend *backend, double value) {
    char *text;

    if (isnan(value)) {
        return arena_copy_cstr(backend->arena, "NAN", backend->error);
    }
    if (isinf(value)) {
        return arena_copy_cstr(backend->arena, signbit(value) ? "-INFINITY" : "INFINITY", backend->error);
    }

    text = arena_printf(backend->arena, backend->error, "%.17g", value);
    if (text == NULL) {
        return NULL;
    }
    if (strchr(text, '.') != NULL || strchr(text, 'e') != NULL || strchr(text, 'E') != NULL) {
        return text;
    }
    return arena_printf(backend->arena, backend->error, "%s.0", text);
}

char *backend_function_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_fn_%.*s", (int)name.len, name.data);
}

char *backend_global_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_global_%.*s", (int)name.len, name.data);
}

char *backend_param_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_param_%.*s", (int)name.len, name.data);
}

char *backend_local_name(Backend *backend, StrSlice name) {
    char *out = arena_printf(backend->arena, backend->error, "rivel_local_%.*s_%zu", (int)name.len, name.data, backend->next_local_id);

    backend->next_local_id += 1U;
    return out;
}

char *backend_temp_name(Backend *backend, const char *base) {
    char *out = arena_printf(backend->arena, backend->error, "rivel_%s_%zu", base, backend->next_local_id);

    backend->next_local_id += 1U;
    return out;
}

const LocalBinding *backend_resolve_local(const Backend *backend, StrSlice name) {
    size_t index = backend_scope_stack_len(&backend->scopes);

    while (index > 0U) {
        const BackendScope *scope = backend_scope_stack_get_const(&backend->scopes, index - 1U);
        const LocalBinding *binding = backend_binding_table_get(&scope->bindings, name);

        if (binding != NULL) {
            return binding;
        }
        index -= 1U;
    }
    return NULL;
}

char *backend_resolve_name(Backend *backend, StrSlice name) {
    const LocalBinding *binding = backend_resolve_local(backend, name);

    if (binding != NULL) {
        return (char *)binding->c_name;
    }
    if (semantic_lookup_global(backend->semantics, name) != NULL) {
        return backend_global_name(backend, name);
    }
    error_set(backend->error, "Backend", "Internal error: unresolved name `%.*s` during C emission", (int)name.len, name.data);
    return NULL;
}

bool backend_add_local(Backend *backend, StrSlice name, const char *c_name, Type type) {
    BackendScope *scope = backend_scope_stack_get(&backend->scopes, backend_scope_stack_len(&backend->scopes) - 1U);
    LocalBinding *binding = (LocalBinding *)arena_alloc_zero(backend->arena, sizeof(*binding), _Alignof(LocalBinding), backend->error);
    const LocalBinding **ordered_binding;

    if (binding == NULL) {
        return false;
    }
    binding->c_name = c_name;
    binding->type = type;
    if (!backend_binding_table_set(&scope->bindings, name, binding, backend->error)) {
        return false;
    }

    ordered_binding = (const LocalBinding **)vec_push(&scope->ordered_bindings, backend->error);
    if (ordered_binding == NULL) {
        return false;
    }
    *ordered_binding = binding;
    return true;
}

bool backend_expr_type_checked(Backend *backend, const Expr *expr, Type *out_type) {
    if (!semantic_expr_type(backend->semantics, expr, out_type)) {
        return error_set(backend->error, "Backend", "Internal error: missing semantic type for emitted expression");
    }
    return true;
}

bool backend_emit_scope_releases(Backend *backend, size_t scope_index) {
    BackendScope *scope = backend_scope_stack_get(&backend->scopes, scope_index);

    while (scope->ordered_bindings.len > 0U) {
        const LocalBinding *binding = *(const LocalBinding **)vec_get(&scope->ordered_bindings, scope->ordered_bindings.len - 1U);

        if (binding->type.kind == TYPE_STRING
            && !backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_string_release(%s);", binding->c_name))) {
            return false;
        }
        scope->ordered_bindings.len -= 1U;
    }
    return true;
}

bool backend_emit_all_scope_releases(Backend *backend) {
    size_t index = backend_scope_stack_len(&backend->scopes);

    while (index > 0U) {
        if (!backend_emit_scope_releases(backend, index - 1U)) {
            return false;
        }
        index -= 1U;
    }
    return true;
}

char *backend_string_literal_value(Backend *backend, StrSlice value) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (value.len == 0U) {
        if (!strbuf_append_cstr(&buf, "\"\"", backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
    }

    while (index < value.len) {
        unsigned char byte = (unsigned char)value.data[index];

        if (index > 0U && !strbuf_append_char(&buf, ' ', backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
        if (!strbuf_append_char(&buf, '"', backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
        if (byte == '\\') {
            if (!strbuf_append_cstr(&buf, "\\\\", backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (byte == '"') {
            if (!strbuf_append_cstr(&buf, "\\\"", backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (byte == '\n') {
            if (!strbuf_append_cstr(&buf, "\\n", backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (byte == '\r') {
            if (!strbuf_append_cstr(&buf, "\\r", backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (byte == '\t') {
            if (!strbuf_append_cstr(&buf, "\\t", backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (byte >= 32U && byte <= 126U) {
            if (!strbuf_append_char(&buf, (char)byte, backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (!strbuf_append_fmt(&buf, backend->error, "\\x%02X", (unsigned int)byte)) {
            strbuf_free(&buf);
            return NULL;
        }
        if (!strbuf_append_char(&buf, '"', backend->error)) {
            strbuf_free(&buf);
            return NULL;
        }
        index += 1U;
    }

    copy = arena_copy_cstr(backend->arena, strbuf_cstr(&buf), backend->error);
    strbuf_free(&buf);
    return copy;
}

char *backend_literal(Backend *backend, ConstValue value) {
    if (value.type.kind == TYPE_BOOL) {
        return arena_copy_cstr(backend->arena, value.bool_value ? "true" : "false", backend->error);
    }
    if (value.type.kind == TYPE_DOUBLE) {
        return backend_double_literal(backend, value.double_value);
    }
    if (value.type.kind == TYPE_STRING) {
        char *literal = backend_string_literal_value(backend, value.string_value);

        if (literal == NULL) {
            return NULL;
        }
        return arena_printf(backend->arena, backend->error, "(RivelString){%s, %zu, NULL}", literal, value.string_value.len);
    }
    return arena_printf(backend->arena, backend->error, "INT64_C(%lld)", (long long)value.int_value);
}
