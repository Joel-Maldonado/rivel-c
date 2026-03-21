#include "semantic_internal.h"

SemanticResult *semantic_result_create(Arena *arena, CompileError *error) {
    SemanticResult *result = (SemanticResult *)arena_alloc_zero(arena, sizeof(*result), _Alignof(SemanticResult), error);

    if (result == NULL) {
        return NULL;
    }

    result->arena = arena;
    semantic_global_table_init(&result->globals, arena);
    semantic_function_table_init(&result->functions, arena);
    semantic_struct_table_init(&result->structs, arena);
    semantic_builtin_table_init(&result->builtins, arena);
    semantic_symbol_table_init(&result->global_names);
    semantic_symbol_table_init(&result->function_names);
    semantic_symbol_table_init(&result->struct_names);
    semantic_symbol_table_init(&result->builtin_names);
    return result;
}

void semantic_result_dispose(SemanticResult *result) {
    if (result == NULL) {
        return;
    }

    semantic_symbol_table_free(&result->global_names);
    semantic_symbol_table_free(&result->function_names);
    semantic_symbol_table_free(&result->struct_names);
    semantic_symbol_table_free(&result->builtin_names);
}

bool semantic_analyze(const Program *program, SemanticResult *result, CompileError *error) {
    Analyzer analyzer;
    size_t index = 0U;

    analyzer.program = program;
    analyzer.result = result;
    analyzer.error = error;
    scope_stack_init(&analyzer.scopes, NULL);

    if (!analyzer_register_builtins(&analyzer) || !analyzer_collect_top_level_declarations(&analyzer)) {
        scope_stack_free(&analyzer.scopes);
        return false;
    }

    while (index < semantic_global_table_len(&result->globals)) {
        SemanticGlobalRecord *record = semantic_global_table_get(&result->globals, index);
        ConstValue value;

        if (!analyzer_evaluate_global_constant(&analyzer, record->info.decl->name, &value)) {
            analyzer_clear_scopes(&analyzer);
            scope_stack_free(&analyzer.scopes);
            return false;
        }
        index += 1U;
    }

    index = 0U;
    while (index < semantic_function_table_len(&result->functions)) {
        SemanticFunctionInfo *info = semantic_function_table_get(&result->functions, index);
        if (!analyzer_analyze_function(&analyzer, info->decl)) {
            analyzer_clear_scopes(&analyzer);
            scope_stack_free(&analyzer.scopes);
            return false;
        }
        index += 1U;
    }

    if (!analyzer_validate_main_signature(&analyzer)) {
        analyzer_clear_scopes(&analyzer);
        scope_stack_free(&analyzer.scopes);
        return false;
    }

    analyzer_clear_scopes(&analyzer);
    scope_stack_free(&analyzer.scopes);
    return true;
}

const SemanticGlobalInfo *semantic_lookup_global(const SemanticResult *result, StrSlice name) {
    size_t index;

    if (!semantic_symbol_table_get(&result->global_names, name, &index)) {
        return NULL;
    }
    return &semantic_global_table_get_const(&result->globals, index)->info;
}

const SemanticFunctionInfo *semantic_lookup_function(const SemanticResult *result, StrSlice name) {
    size_t index;

    if (!semantic_symbol_table_get(&result->function_names, name, &index)) {
        return NULL;
    }
    return semantic_function_table_get_const(&result->functions, index);
}

const SemanticStructInfo *semantic_lookup_struct(const SemanticResult *result, StrSlice name) {
    size_t index;

    if (!semantic_symbol_table_get(&result->struct_names, name, &index)) {
        return NULL;
    }
    return semantic_struct_table_get_const(&result->structs, index);
}

const SemanticBuiltinInfo *semantic_lookup_builtin(const SemanticResult *result, StrSlice name) {
    size_t index;

    if (!semantic_symbol_table_get(&result->builtin_names, name, &index)) {
        return NULL;
    }
    return semantic_builtin_table_get_const(&result->builtins, index);
}

bool semantic_global_const_value(const SemanticResult *result, StrSlice name, ConstValue *out_value) {
    const SemanticGlobalInfo *info = semantic_lookup_global(result, name);

    if (info == NULL) {
        return false;
    }
    if (out_value != NULL) {
        *out_value = info->value;
    }
    return true;
}

bool semantic_expr_type(const SemanticResult *result, const Expr *expr, Type *out_type) {
    (void)result;
    return expr_resolved_type(expr, out_type);
}

bool semantic_expr_const_value(const SemanticResult *result, const Expr *expr, ConstValue *out_value) {
    (void)result;
    return expr_const_value(expr, out_value);
}
