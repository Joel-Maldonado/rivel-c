#ifndef RIVEL_SEMANTIC_H
#define RIVEL_SEMANTIC_H

#include "arena.h"
#include "ast.h"
#include "error.h"

typedef struct SemanticResult SemanticResult;

typedef struct {
    const Decl *decl;
    Type type;
    ConstValue value;
} SemanticGlobalInfo;

typedef struct {
    const Decl *decl;
} SemanticFunctionInfo;

typedef struct {
    const Decl *decl;
} SemanticStructInfo;

typedef enum {
    BUILTIN_PRINT,
    BUILTIN_PRINTLN,
    BUILTIN_STRINGIFY,
    BUILTIN_LEN,
    BUILTIN_SUBSTR,
    BUILTIN_CONTAINS,
    BUILTIN_STARTS_WITH,
    BUILTIN_ENDS_WITH
} BuiltinKind;

typedef struct {
    BuiltinKind kind;
} SemanticBuiltinInfo;

SemanticResult *semantic_result_create(Arena *arena, CompileError *error);
void semantic_result_dispose(SemanticResult *result);
bool semantic_analyze(const Program *program, SemanticResult *result, CompileError *error);
const SemanticGlobalInfo *semantic_lookup_global(const SemanticResult *result, StrSlice name);
const SemanticFunctionInfo *semantic_lookup_function(const SemanticResult *result, StrSlice name);
const SemanticStructInfo *semantic_lookup_struct(const SemanticResult *result, StrSlice name);
const SemanticBuiltinInfo *semantic_lookup_builtin(const SemanticResult *result, StrSlice name);
bool semantic_global_const_value(const SemanticResult *result, StrSlice name, ConstValue *out_value);
bool semantic_expr_type(const SemanticResult *result, const Expr *expr, Type *out_type);
bool semantic_expr_const_value(const SemanticResult *result, const Expr *expr, ConstValue *out_value);

#endif
