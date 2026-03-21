#include "semantic_internal.h"

void binding_table_init(BindingTable *table) {
    strmap_init(&table->storage);
}

void binding_table_free(BindingTable *table) {
    strmap_free(&table->storage);
}

bool binding_table_contains(const BindingTable *table, StrSlice name) {
    return strmap_contains(&table->storage, name);
}

bool binding_table_set(BindingTable *table, StrSlice name, const BindingInfo *binding, CompileError *error) {
    return strmap_set(&table->storage, name, (uintptr_t)binding, false, error);
}

const BindingInfo *binding_table_get(const BindingTable *table, StrSlice name) {
    uintptr_t value;

    if (!strmap_get(&table->storage, name, &value)) {
        return NULL;
    }
    return (const BindingInfo *)value;
}

void scope_stack_init(ScopeStack *stack, Arena *arena) {
    vec_init(&stack->storage, sizeof(Scope), arena);
}

void scope_stack_free(ScopeStack *stack) {
    vec_free(&stack->storage);
}

size_t scope_stack_len(const ScopeStack *stack) {
    return stack->storage.len;
}

Scope *scope_stack_push(ScopeStack *stack, CompileError *error) {
    return (Scope *)vec_push(&stack->storage, error);
}

Scope *scope_stack_get(ScopeStack *stack, size_t index) {
    return (Scope *)vec_get(&stack->storage, index);
}

const Scope *scope_stack_get_const(const ScopeStack *stack, size_t index) {
    return (const Scope *)vec_get(&stack->storage, index);
}

void scope_stack_pop(ScopeStack *stack) {
    if (stack->storage.len > 0U) {
        stack->storage.len -= 1U;
    }
}

bool analyzer_push_scope(Analyzer *analyzer) {
    Scope *scope = scope_stack_push(&analyzer->scopes, analyzer->error);

    if (scope == NULL) {
        return false;
    }
    binding_table_init(&scope->bindings);
    return true;
}

void analyzer_pop_scope(Analyzer *analyzer) {
    Scope *scope;

    if (scope_stack_len(&analyzer->scopes) == 0U) {
        return;
    }

    scope = scope_stack_get(&analyzer->scopes, scope_stack_len(&analyzer->scopes) - 1U);
    binding_table_free(&scope->bindings);
    scope_stack_pop(&analyzer->scopes);
}

void analyzer_clear_scopes(Analyzer *analyzer) {
    while (scope_stack_len(&analyzer->scopes) > 0U) {
        analyzer_pop_scope(analyzer);
    }
}

bool analyzer_require_type(Analyzer *analyzer, Token token, Type actual, Type expected, const char *message) {
    if (!type_equal(actual, expected)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "%s", message);
    }
    return true;
}

bool analyzer_declare_local(Analyzer *analyzer, Token token, StrSlice name, Type type, bool is_mutable) {
    Scope *scope;
    BindingInfo *binding;

    scope = scope_stack_get(&analyzer->scopes, scope_stack_len(&analyzer->scopes) - 1U);
    if (binding_table_contains(&scope->bindings, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Binding `%.*s` is already declared in this scope", (int)name.len, name.data);
    }

    binding = (BindingInfo *)arena_alloc_zero(analyzer->result->arena, sizeof(*binding), _Alignof(BindingInfo), analyzer->error);
    if (binding == NULL) {
        return false;
    }

    binding->type = type;
    binding->is_mutable = is_mutable;
    return binding_table_set(&scope->bindings, name, binding, analyzer->error);
}

const BindingInfo *analyzer_resolve_local(const Analyzer *analyzer, StrSlice name) {
    size_t index = scope_stack_len(&analyzer->scopes);

    while (index > 0U) {
        const Scope *scope = scope_stack_get_const(&analyzer->scopes, index - 1U);
        const BindingInfo *binding = binding_table_get(&scope->bindings, name);

        if (binding != NULL) {
            return binding;
        }
        index -= 1U;
    }

    return NULL;
}

SemanticGlobalRecord *analyzer_lookup_global_record_mut(Analyzer *analyzer, StrSlice name) {
    size_t index;

    if (!semantic_symbol_table_get(&analyzer->result->global_names, name, &index)) {
        return NULL;
    }
    return semantic_global_table_get(&analyzer->result->globals, index);
}

const SemanticGlobalInfo *analyzer_lookup_global(const Analyzer *analyzer, StrSlice name) {
    return semantic_lookup_global(analyzer->result, name);
}

const SemanticFunctionInfo *analyzer_lookup_function(const Analyzer *analyzer, StrSlice name) {
    return semantic_lookup_function(analyzer->result, name);
}

const SemanticStructInfo *analyzer_lookup_struct(const Analyzer *analyzer, StrSlice name) {
    return semantic_lookup_struct(analyzer->result, name);
}

const SemanticBuiltinInfo *analyzer_lookup_builtin(const Analyzer *analyzer, StrSlice name) {
    return semantic_lookup_builtin(analyzer->result, name);
}
