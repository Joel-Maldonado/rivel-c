#include "backend_c_internal.h"

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

bool backend_emit_scope_releases(Backend *backend, size_t scope_index) {
    BackendScope *scope = backend_scope_stack_get(&backend->scopes, scope_index);

    while (scope->ordered_bindings.len > 0U) {
        const LocalBinding *binding = *(const LocalBinding **)vec_get(&scope->ordered_bindings, scope->ordered_bindings.len - 1U);

        if (!backend_emit_release_value(backend, binding->type, binding->c_name)) {
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
