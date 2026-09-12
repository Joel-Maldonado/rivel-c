#ifndef RIVEL_BACKEND_C_INTERNAL_H
#define RIVEL_BACKEND_C_INTERNAL_H

#include "backend_c.h"
#include "map.h"
#include "vec.h"

typedef struct {
    const char *c_name;
    Type type;
} LocalBinding;

typedef struct {
    StringMap storage;
} BackendBindingTable;

typedef struct {
    BackendBindingTable bindings;
    Vec ordered_bindings;
} BackendScope;

typedef struct {
    Vec storage;
} BackendScopeStack;

typedef struct {
    const Program *program;
    const SemanticResult *semantics;
    Arena *arena;
    StrBuf *output;
    CompileError *error;
    BackendScopeStack scopes;
    int indent;
    size_t next_local_id;
} Backend;

void backend_binding_table_init(BackendBindingTable *table);
void backend_binding_table_free(BackendBindingTable *table);
bool backend_binding_table_set(BackendBindingTable *table, StrSlice name, const LocalBinding *binding, CompileError *error);
const LocalBinding *backend_binding_table_get(const BackendBindingTable *table, StrSlice name);

void backend_scope_stack_init(BackendScopeStack *stack, Arena *arena);
void backend_scope_stack_free(BackendScopeStack *stack);
size_t backend_scope_stack_len(const BackendScopeStack *stack);
BackendScope *backend_scope_stack_push(BackendScopeStack *stack, CompileError *error);
BackendScope *backend_scope_stack_get(BackendScopeStack *stack, size_t index);
const BackendScope *backend_scope_stack_get_const(const BackendScopeStack *stack, size_t index);
void backend_scope_stack_pop(BackendScopeStack *stack);

bool backend_push_scope(Backend *backend);
void backend_pop_scope(Backend *backend);
void backend_clear_scopes(Backend *backend);

bool backend_emit_line(Backend *backend, const char *line);
void backend_indent_push(Backend *backend);
void backend_indent_pop(Backend *backend);

char *backend_c_type(Backend *backend, Type type);
char *backend_double_literal(Backend *backend, double value);
char *backend_function_name(Backend *backend, StrSlice name);
char *backend_global_name(Backend *backend, StrSlice name);
char *backend_param_name(Backend *backend, StrSlice name);
char *backend_local_name(Backend *backend, StrSlice name);
char *backend_temp_name(Backend *backend, const char *base);
char *backend_struct_type_name(Backend *backend, StrSlice name);
char *backend_struct_retain_name(Backend *backend, StrSlice name);
char *backend_struct_release_name(Backend *backend, StrSlice name);
char *backend_struct_take_field_name(Backend *backend, StrSlice struct_name, StrSlice field_name);
const SemanticStructInfo *backend_lookup_struct_checked(Backend *backend, StrSlice name);
const StructFieldDecl *backend_lookup_struct_field(Backend *backend, StrSlice struct_name, StrSlice field_name);
bool backend_type_contains_owned_strings(Backend *backend, Type type);
char *backend_retain_value_expr(Backend *backend, Type type, const char *value);
bool backend_emit_release_value(Backend *backend, Type type, const char *value);
const LocalBinding *backend_resolve_local(const Backend *backend, StrSlice name);
char *backend_resolve_name(Backend *backend, StrSlice name);
bool backend_add_local(Backend *backend, StrSlice name, const char *c_name, Type type);
bool backend_expr_type_checked(Backend *backend, const Expr *expr, Type *out_type);
bool backend_emit_scope_releases(Backend *backend, size_t scope_index);
bool backend_emit_all_scope_releases(Backend *backend);
char *backend_string_literal_value(Backend *backend, StrSlice value);
char *backend_literal(Backend *backend, ConstValue value);

char *backend_emit_expr(Backend *backend, const Expr *expr);
char *backend_condition_expr(Backend *backend, const Expr *expr);

char *backend_function_signature(Backend *backend, const Decl *decl);
bool backend_emit_struct_definitions(Backend *backend);
bool backend_emit_call_stmt(Backend *backend, const Expr *call_expr);
bool backend_emit_block(Backend *backend, const Block *block);
bool backend_emit_stmt(Backend *backend, const Stmt *stmt);
bool backend_emit_function(Backend *backend, const Decl *decl);

bool backend_emit_prelude(Backend *backend);
bool backend_emit_program_entry(Backend *backend);

#endif
