#ifndef RV_BACKEND_QBE_H
#define RV_BACKEND_QBE_H

#include <stdio.h>

#include "ir/ir.h"

/* Writes the module as QBE intermediate language. The output is target independent. */
void qbe_emit(const IrModule *m, FILE *out);

#endif
