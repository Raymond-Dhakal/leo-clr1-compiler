/*
 * nova_opt.h
 * Part of the Leo compiler extension - PHASE 5: code optimization.
 *
 * Optimizes the IR in place. All passes are conservative: they never
 * remove an instruction that can fail at run time (division by something
 * that may be zero, INPUT, PRINT) and never fold an operation whose
 * result is undefined (x / 0, negative INT exponent, non-finite FLOAT).
 */
#ifndef NOVA_OPT_H
#define NOVA_OPT_H

#include <stdio.h>
#include "nova_ir.h"

typedef struct {
    int rounds;        /* optimization rounds until nothing changed       */
    int propagated;    /* operands replaced by a known constant           */
    int folded;        /* instructions evaluated at compile time          */
    int algebraic;     /* algebraic simplifications (x+0, x*1, ...)       */
    int branches;      /* conditional branches resolved at compile time   */
    int copies;        /* operands replaced by the variable they copy     */
    int unreachable;   /* instructions removed because they cannot run    */
    int labels;        /* unused labels / jumps to the next instruction   */
    int dead;          /* dead or redundant assignments removed           */
    int forwarded;     /* "t = e; x = t" merged into "x = e"              */
    int before, after; /* instruction counts (labels included)            */
} OptStats;

void opt_run(IRProgram *p, OptStats *stats);
void opt_stats_print(const OptStats *s, FILE *out);

#endif /* NOVA_OPT_H */
