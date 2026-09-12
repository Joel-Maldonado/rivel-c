#include "semantic_internal.h"

SemanticResult *semantic_result_create(Arena *arena, CompileError *error) {
    SemanticResult *result = (SemanticResult *)arena_alloc_zero(arena, sizeof(*result), _Alignof(SemanticResult), error);

    if (result == NULL) {
        return NULL;
    }

    result->arena = arena;
    semantic_table_init(&result->globals, sizeof(SemanticGlobalRecord), arena);
    semantic_table_init(&result->functions, sizeof(SemanticFunctionInfo), arena);
    semantic_table_init(&result->structs, sizeof(SemanticStructInfo), arena);
    semantic_table_init(&result->builtins, sizeof(SemanticBuiltinInfo), arena);
    return result;
}

void semantic_result_dispose(SemanticResult *result) {
    if (result == NULL) {
        return;
    }

    semantic_table_free(&result->globals);
    semantic_table_free(&result->functions);
    semantic_table_free(&result->structs);
    semantic_table_free(&result->builtins);
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

    while (index < semantic_table_len(&result->globals)) {
        SemanticGlobalRecord *record = (SemanticGlobalRecord *)semantic_table_get(&result->globals, index);
        ConstValue value;

        if (!analyzer_evaluate_global_constant(&analyzer, record->info.decl->name, &value)) {
            analyzer_clear_scopes(&analyzer);
            scope_stack_free(&analyzer.scopes);
            return false;
        }
        index += 1U;
    }

    index = 0U;
    while (index < semantic_table_len(&result->functions)) {
        SemanticFunctionInfo *info = (SemanticFunctionInfo *)semantic_table_get(&result->functions, index);
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
    SemanticGlobalRecord *record = (SemanticGlobalRecord *)semantic_table_lookup(&result->globals, name);

    if (record == NULL) {
        return NULL;
    }
    return &record->info;
}

const SemanticFunctionInfo *semantic_lookup_function(const SemanticResult *result, StrSlice name) {
    return (const SemanticFunctionInfo *)semantic_table_lookup(&result->functions, name);
}

const SemanticStructInfo *semantic_lookup_struct(const SemanticResult *result, StrSlice name) {
    return (const SemanticStructInfo *)semantic_table_lookup(&result->structs, name);
}

const SemanticBuiltinInfo *semantic_lookup_builtin(const SemanticResult *result, StrSlice name) {
    return (const SemanticBuiltinInfo *)semantic_table_lookup(&result->builtins, name);
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

