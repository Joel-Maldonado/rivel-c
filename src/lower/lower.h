#ifndef RV_LOWER_LOWER_H
#define RV_LOWER_LOWER_H

#include "ir/ir.h"
#include "sema/sema.h"

/*
 * Lowers a checked program to IR. Requires that sema_check succeeded: every
 * expression has a type and every name a symbol. Never fails.
 */
void lower_program(Program *prog, Arena *arena, IrModule *out);

#endif
