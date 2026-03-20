#ifndef RIVEL_SEMANTIC_H
#define RIVEL_SEMANTIC_H

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "map.h"
#include "vec.h"

typedef struct {
    Type type;
    int64_t int_value;
    bool bool_value;
} ConstValue;

typedef struct {
    Decl *decl;
    Type type;
    ConstValue value;
    int visit_state;
} GlobalConstInfo;

typedef struct {
    Decl *decl;
} FunctionInfo;

typedef enum {
    BUILTIN_PRINT
} BuiltinKind;

typedef struct {
    BuiltinKind kind;
} BuiltinInfo;

typedef struct {
    Arena *arena;
    Vec globals;
    Vec functions;
    Vec builtins;
    StringMap global_names;
    StringMap function_names;
    StringMap builtin_names;
} SemanticContext;

void semantic_context_init(SemanticContext *context, Arena *arena);
void semantic_context_free(SemanticContext *context);
bool semantic_analyze(Program *program, SemanticContext *context, CompileError *error);
const GlobalConstInfo *semantic_lookup_global(const SemanticContext *context, StrSlice name);
const FunctionInfo *semantic_lookup_function(const SemanticContext *context, StrSlice name);
const BuiltinInfo *semantic_lookup_builtin(const SemanticContext *context, StrSlice name);

#endif
