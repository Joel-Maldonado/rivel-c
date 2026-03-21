#include "semantic_internal.h"

#define DEFINE_SEMANTIC_TABLE_FUNCTIONS(TableType, EntryType, prefix) \
    void prefix##_init(TableType *table, Arena *arena) { \
        vec_init(&table->storage, sizeof(EntryType), arena); \
    } \
    size_t prefix##_len(const TableType *table) { \
        return table->storage.len; \
    } \
    EntryType *prefix##_push(TableType *table, CompileError *error) { \
        return (EntryType *)vec_push(&table->storage, error); \
    } \
    EntryType *prefix##_get(TableType *table, size_t index) { \
        return (EntryType *)vec_get(&table->storage, index); \
    } \
    const EntryType *prefix##_get_const(const TableType *table, size_t index) { \
        return (const EntryType *)vec_get(&table->storage, index); \
    }

DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticGlobalTable, SemanticGlobalRecord, semantic_global_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticFunctionTable, SemanticFunctionInfo, semantic_function_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticBuiltinTable, SemanticBuiltinInfo, semantic_builtin_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(ExprTypeTable, ExprTypeEntry, expr_type_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(ExprConstTable, ExprConstEntry, expr_const_table)

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

void semantic_symbol_table_init(SemanticSymbolTable *table) {
    strmap_init(&table->storage);
}

void semantic_symbol_table_free(SemanticSymbolTable *table) {
    strmap_free(&table->storage);
}

bool semantic_symbol_table_contains(const SemanticSymbolTable *table, StrSlice name) {
    return strmap_contains(&table->storage, name);
}

bool semantic_symbol_table_set(SemanticSymbolTable *table, StrSlice name, size_t index, CompileError *error) {
    return strmap_set(&table->storage, name, semantic_index_value(index), false, error);
}

bool semantic_symbol_table_get(const SemanticSymbolTable *table, StrSlice name, size_t *out_index) {
    uintptr_t value;

    if (!strmap_get(&table->storage, name, &value)) {
        return false;
    }
    if (out_index != NULL) {
        *out_index = semantic_map_index(value);
    }
    return true;
}

uintptr_t semantic_index_value(size_t index) {
    return (uintptr_t)(index + 1U);
}

size_t semantic_map_index(uintptr_t value) {
    return (size_t)(value - 1U);
}

ConstValue semantic_make_int(int64_t value) {
    ConstValue out;

    out.type.kind = TYPE_INT;
    out.int_value = value;
    out.double_value = 0.0;
    out.bool_value = false;
    return out;
}

ConstValue semantic_make_double(double value) {
    ConstValue out;

    out.type.kind = TYPE_DOUBLE;
    out.int_value = 0;
    out.double_value = value;
    out.bool_value = false;
    return out;
}

ConstValue semantic_make_bool(bool value) {
    ConstValue out;

    out.type.kind = TYPE_BOOL;
    out.int_value = 0;
    out.double_value = 0.0;
    out.bool_value = value;
    return out;
}

bool semantic_record_expr_type(SemanticResult *result, const Expr *expr, Type type, CompileError *error) {
    size_t index = 0U;

    while (index < expr_type_table_len(&result->expr_types)) {
        ExprTypeEntry *entry = expr_type_table_get(&result->expr_types, index);
        if (entry->expr == expr) {
            entry->type = type;
            return true;
        }
        index += 1U;
    }

    ExprTypeEntry *entry = expr_type_table_push(&result->expr_types, error);
    if (entry == NULL) {
        return false;
    }
    entry->expr = expr;
    entry->type = type;
    return true;
}

bool semantic_record_expr_const(SemanticResult *result, const Expr *expr, ConstValue value, CompileError *error) {
    size_t index = 0U;

    while (index < expr_const_table_len(&result->expr_consts)) {
        ExprConstEntry *entry = expr_const_table_get(&result->expr_consts, index);
        if (entry->expr == expr) {
            entry->value = value;
            return true;
        }
        index += 1U;
    }

    ExprConstEntry *entry = expr_const_table_push(&result->expr_consts, error);
    if (entry == NULL) {
        return false;
    }
    entry->expr = expr;
    entry->value = value;
    return true;
}

bool semantic_lookup_recorded_expr_type(const SemanticResult *result, const Expr *expr, Type *out_type) {
    size_t index = 0U;

    while (index < expr_type_table_len(&result->expr_types)) {
        const ExprTypeEntry *entry = expr_type_table_get_const(&result->expr_types, index);
        if (entry->expr == expr) {
            if (out_type != NULL) {
                *out_type = entry->type;
            }
            return true;
        }
        index += 1U;
    }

    return false;
}

bool semantic_lookup_recorded_expr_const(const SemanticResult *result, const Expr *expr, ConstValue *out_value) {
    size_t index = 0U;

    while (index < expr_const_table_len(&result->expr_consts)) {
        const ExprConstEntry *entry = expr_const_table_get_const(&result->expr_consts, index);
        if (entry->expr == expr) {
            if (out_value != NULL) {
                *out_value = entry->value;
            }
            return true;
        }
        index += 1U;
    }

    return false;
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

const SemanticBuiltinInfo *analyzer_lookup_builtin(const Analyzer *analyzer, StrSlice name) {
    return semantic_lookup_builtin(analyzer->result, name);
}

bool analyzer_register_builtins(Analyzer *analyzer) {
    SemanticBuiltinInfo *builtin = semantic_builtin_table_push(&analyzer->result->builtins, analyzer->error);
    if (builtin == NULL) {
        return false;
    }

    builtin->kind = BUILTIN_PRINT;
    return semantic_symbol_table_set(&analyzer->result->builtin_names, slice_from_cstr("print"), 0U, analyzer->error);
}

static bool analyzer_ensure_unique_top_level_name(Analyzer *analyzer, Token token, StrSlice name) {
    if (semantic_symbol_table_contains(&analyzer->result->builtin_names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Top-level name `%.*s` is reserved for a builtin", (int)name.len, name.data);
    }
    if (semantic_symbol_table_contains(&analyzer->result->global_names, name) || semantic_symbol_table_contains(&analyzer->result->function_names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Top-level name `%.*s` is already declared", (int)name.len, name.data);
    }
    return true;
}

bool analyzer_collect_top_level_declarations(Analyzer *analyzer) {
    size_t index = 0U;

    while (index < decl_list_len(&analyzer->program->decls)) {
        Decl *decl = decl_list_get(&analyzer->program->decls, index);

        if (decl->kind == DECL_GLOBAL_CONST) {
            SemanticGlobalRecord *record;

            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }

            record = semantic_global_table_push(&analyzer->result->globals, analyzer->error);
            if (record == NULL) {
                return false;
            }

            record->info.decl = decl;
            record->info.type.kind = TYPE_INT;
            record->info.value = semantic_make_int(0);
            record->visit_state = GLOBAL_UNVISITED;
            if (!semantic_symbol_table_set(&analyzer->result->global_names, decl->name, semantic_global_table_len(&analyzer->result->globals) - 1U, analyzer->error)) {
                return false;
            }
        } else if (decl->kind == DECL_FUNCTION) {
            SemanticFunctionInfo *info;

            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }

            info = semantic_function_table_push(&analyzer->result->functions, analyzer->error);
            if (info == NULL) {
                return false;
            }

            info->decl = decl;
            if (!semantic_symbol_table_set(&analyzer->result->function_names, decl->name, semantic_function_table_len(&analyzer->result->functions) - 1U, analyzer->error)) {
                return false;
            }
        }

        index += 1U;
    }

    return true;
}
