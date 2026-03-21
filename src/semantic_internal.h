#ifndef RIVEL_SEMANTIC_INTERNAL_H
#define RIVEL_SEMANTIC_INTERNAL_H

#include "semantic.h"

typedef enum {
    GLOBAL_UNVISITED = 0,
    GLOBAL_VISITING = 1,
    GLOBAL_DONE = 2
} GlobalVisitState;

typedef struct {
    Type type;
    bool is_mutable;
} BindingInfo;

typedef struct {
    StringMap storage;
} BindingTable;

typedef struct {
    BindingTable bindings;
} Scope;

typedef struct {
    Vec storage;
} ScopeStack;

typedef struct {
    const Expr *expr;
    Type type;
} ExprTypeEntry;

typedef struct {
    const Expr *expr;
    ConstValue value;
} ExprConstEntry;

typedef struct {
    SemanticGlobalInfo info;
    GlobalVisitState visit_state;
} SemanticGlobalRecord;

typedef struct {
    Vec storage;
} SemanticGlobalTable;

typedef struct {
    Vec storage;
} SemanticFunctionTable;

typedef struct {
    Vec storage;
} SemanticBuiltinTable;

typedef struct {
    Vec storage;
} ExprTypeTable;

typedef struct {
    Vec storage;
} ExprConstTable;

typedef struct {
    StringMap storage;
} SemanticSymbolTable;

struct SemanticResult {
    Arena *arena;
    SemanticGlobalTable globals;
    SemanticFunctionTable functions;
    SemanticBuiltinTable builtins;
    SemanticSymbolTable global_names;
    SemanticSymbolTable function_names;
    SemanticSymbolTable builtin_names;
    ExprTypeTable expr_types;
    ExprConstTable expr_consts;
};

typedef struct {
    const Program *program;
    SemanticResult *result;
    CompileError *error;
    ScopeStack scopes;
} Analyzer;

uintptr_t semantic_index_value(size_t index);
size_t semantic_map_index(uintptr_t value);
ConstValue semantic_make_int(int64_t value);
ConstValue semantic_make_double(double value);
ConstValue semantic_make_bool(bool value);
ConstValue semantic_make_string(StrSlice value);

void binding_table_init(BindingTable *table);
void binding_table_free(BindingTable *table);
bool binding_table_contains(const BindingTable *table, StrSlice name);
bool binding_table_set(BindingTable *table, StrSlice name, const BindingInfo *binding, CompileError *error);
const BindingInfo *binding_table_get(const BindingTable *table, StrSlice name);

void scope_stack_init(ScopeStack *stack, Arena *arena);
void scope_stack_free(ScopeStack *stack);
size_t scope_stack_len(const ScopeStack *stack);
Scope *scope_stack_push(ScopeStack *stack, CompileError *error);
Scope *scope_stack_get(ScopeStack *stack, size_t index);
const Scope *scope_stack_get_const(const ScopeStack *stack, size_t index);
void scope_stack_pop(ScopeStack *stack);

void semantic_symbol_table_init(SemanticSymbolTable *table);
void semantic_symbol_table_free(SemanticSymbolTable *table);
bool semantic_symbol_table_contains(const SemanticSymbolTable *table, StrSlice name);
bool semantic_symbol_table_set(SemanticSymbolTable *table, StrSlice name, size_t index, CompileError *error);
bool semantic_symbol_table_get(const SemanticSymbolTable *table, StrSlice name, size_t *out_index);

void semantic_global_table_init(SemanticGlobalTable *table, Arena *arena);
size_t semantic_global_table_len(const SemanticGlobalTable *table);
SemanticGlobalRecord *semantic_global_table_push(SemanticGlobalTable *table, CompileError *error);
SemanticGlobalRecord *semantic_global_table_get(SemanticGlobalTable *table, size_t index);
const SemanticGlobalRecord *semantic_global_table_get_const(const SemanticGlobalTable *table, size_t index);

void semantic_function_table_init(SemanticFunctionTable *table, Arena *arena);
size_t semantic_function_table_len(const SemanticFunctionTable *table);
SemanticFunctionInfo *semantic_function_table_push(SemanticFunctionTable *table, CompileError *error);
SemanticFunctionInfo *semantic_function_table_get(SemanticFunctionTable *table, size_t index);
const SemanticFunctionInfo *semantic_function_table_get_const(const SemanticFunctionTable *table, size_t index);

void semantic_builtin_table_init(SemanticBuiltinTable *table, Arena *arena);
size_t semantic_builtin_table_len(const SemanticBuiltinTable *table);
SemanticBuiltinInfo *semantic_builtin_table_push(SemanticBuiltinTable *table, CompileError *error);
const SemanticBuiltinInfo *semantic_builtin_table_get_const(const SemanticBuiltinTable *table, size_t index);

void expr_type_table_init(ExprTypeTable *table, Arena *arena);
size_t expr_type_table_len(const ExprTypeTable *table);
ExprTypeEntry *expr_type_table_push(ExprTypeTable *table, CompileError *error);
ExprTypeEntry *expr_type_table_get(ExprTypeTable *table, size_t index);
const ExprTypeEntry *expr_type_table_get_const(const ExprTypeTable *table, size_t index);

void expr_const_table_init(ExprConstTable *table, Arena *arena);
size_t expr_const_table_len(const ExprConstTable *table);
ExprConstEntry *expr_const_table_push(ExprConstTable *table, CompileError *error);
ExprConstEntry *expr_const_table_get(ExprConstTable *table, size_t index);
const ExprConstEntry *expr_const_table_get_const(const ExprConstTable *table, size_t index);

bool semantic_record_expr_type(SemanticResult *result, const Expr *expr, Type type, CompileError *error);
bool semantic_record_expr_const(SemanticResult *result, const Expr *expr, ConstValue value, CompileError *error);
bool semantic_lookup_recorded_expr_type(const SemanticResult *result, const Expr *expr, Type *out_type);
bool semantic_lookup_recorded_expr_const(const SemanticResult *result, const Expr *expr, ConstValue *out_value);

bool analyzer_push_scope(Analyzer *analyzer);
void analyzer_pop_scope(Analyzer *analyzer);
void analyzer_clear_scopes(Analyzer *analyzer);
bool analyzer_require_type(Analyzer *analyzer, Token token, Type actual, Type expected, const char *message);
bool analyzer_declare_local(Analyzer *analyzer, Token token, StrSlice name, Type type, bool is_mutable);
const BindingInfo *analyzer_resolve_local(const Analyzer *analyzer, StrSlice name);

SemanticGlobalRecord *analyzer_lookup_global_record_mut(Analyzer *analyzer, StrSlice name);
const SemanticGlobalInfo *analyzer_lookup_global(const Analyzer *analyzer, StrSlice name);
const SemanticFunctionInfo *analyzer_lookup_function(const Analyzer *analyzer, StrSlice name);
const SemanticBuiltinInfo *analyzer_lookup_builtin(const Analyzer *analyzer, StrSlice name);

bool analyzer_register_builtins(Analyzer *analyzer);
bool analyzer_collect_top_level_declarations(Analyzer *analyzer);

bool analyzer_evaluate_const_expr(Analyzer *analyzer, const Expr *expr, ConstValue *out_value);
bool analyzer_evaluate_global_constant(Analyzer *analyzer, StrSlice name, ConstValue *out_value);

bool analyzer_analyze_expr(Analyzer *analyzer, const Expr *expr, Type *out_type);
bool analyzer_analyze_call(Analyzer *analyzer, const Expr *call_expr, bool allow_statement_only_builtins, Type *out_type);

bool analyzer_analyze_block(Analyzer *analyzer, const Block *block, Type function_return_type);
bool analyzer_analyze_stmt(Analyzer *analyzer, const Stmt *stmt, Type function_return_type);
bool analyzer_block_guarantees_return(const Block *block);
bool analyzer_stmt_guarantees_return(const Stmt *stmt);
bool analyzer_analyze_function(Analyzer *analyzer, const Decl *function_decl);
bool analyzer_validate_main_signature(Analyzer *analyzer);

#endif
