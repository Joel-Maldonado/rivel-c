#ifndef RIVEL_SEMANTIC_INTERNAL_H
#define RIVEL_SEMANTIC_INTERNAL_H

#include "map.h"
#include "semantic.h"
#include "vec.h"

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
    SemanticGlobalInfo info;
    GlobalVisitState visit_state;
} SemanticGlobalRecord;

typedef struct {
    Vec entries;
    StringMap names;
} SemanticTable;

struct SemanticResult {
    Arena *arena;
    SemanticTable globals;
    SemanticTable functions;
    SemanticTable structs;
    SemanticTable builtins;
};

typedef struct {
    const Program *program;
    SemanticResult *result;
    CompileError *error;
    ScopeStack scopes;
} Analyzer;

void semantic_table_init(SemanticTable *table, size_t entry_size, Arena *arena);
void semantic_table_free(SemanticTable *table);
size_t semantic_table_len(const SemanticTable *table);
void *semantic_table_add(SemanticTable *table, StrSlice name, CompileError *error);
void *semantic_table_get(const SemanticTable *table, size_t index);
bool semantic_table_contains(const SemanticTable *table, StrSlice name);
void *semantic_table_lookup(const SemanticTable *table, StrSlice name);

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



bool analyzer_push_scope(Analyzer *analyzer);
void analyzer_pop_scope(Analyzer *analyzer);
void analyzer_clear_scopes(Analyzer *analyzer);
bool analyzer_require_type(Analyzer *analyzer, Token token, Type actual, Type expected, const char *message);
bool analyzer_declare_local(Analyzer *analyzer, Token token, StrSlice name, Type type, bool is_mutable);
const BindingInfo *analyzer_resolve_local(const Analyzer *analyzer, StrSlice name);

SemanticGlobalRecord *analyzer_lookup_global_record_mut(Analyzer *analyzer, StrSlice name);

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
