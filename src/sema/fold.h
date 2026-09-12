#ifndef RV_SEMA_FOLD_H
#define RV_SEMA_FOLD_H

#include "sema/sema.h"

/*
 * Evaluates a global initializer at compile time with run-time semantics.
 * The expression must already be type checked. Returns false after
 * reporting why the expression is not constant, or would panic.
 */
bool fold_const(const Expr *e, TypeTable *tt, Arena *arena, Diags *diags, const Source *src, ConstValue *out);

#endif
