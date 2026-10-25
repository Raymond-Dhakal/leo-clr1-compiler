/*
 * nova_interp.h
 * Part of the Leo compiler extension: an interpreter for the three address
 * code. It is the execution engine of the browser version (where GCC is not
 * available) and of `leoc --interp`. It follows exactly the same run time
 * rules as the generated C program: 64 bit wrapping INT, checked division,
 * the same error messages, the same output formats.
 */
#ifndef NOVA_INTERP_H
#define NOVA_INTERP_H

#include <stdio.h>
#include "nova_ir.h"

/* Runs `p`, reading INPUT values from `in` and writing PRINT output to `out`.
   Run time errors are reported on stderr as "runtime error (line N): ...".
   Returns 0 on success, 1 after a run time error (like the compiled program). */
/* Returns 2 when interactive mode reaches end-of-input and needs another value. */
int nova_interpret(const IRProgram *p, FILE *in, FILE *out, int interactive);

#endif /* NOVA_INTERP_H */
