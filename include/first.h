/*
 * first.h
 * Owner: Member 2 - Ephraim Immanuel (24BEL0091)
 *
 * FIRST sets, nullability, FIRST-of-a-sequence, and the lookahead
 * computation that CLOSURE (Member 3) depends on. All other modules
 * that need FIRST(...) MUST call into this module rather than
 * re-deriving it (see closure.c, which only calls first_of_sequence).
 */
#ifndef CLR_FIRST_H
#define CLR_FIRST_H

#include "grammar.h"

/* A small dynamic set of symbol ids (no duplicates). */
typedef struct {
    int *symbols;
    int count;
    int capacity;
} SymbolSet;

typedef struct {
    Grammar *grammar;
    SymbolSet *first_sets;  /* indexed by symbol id, size symbol_count */
    int *nullable;          /* indexed by symbol id, 1 = nullable      */
} FirstSetsTable;

/* SymbolSet helpers, reused by closure/goto/etc. */
void symbolset_init(SymbolSet *s);
void symbolset_free(SymbolSet *s);
int symbolset_add(SymbolSet *s, int sym);      /* returns 1 if newly added */
int symbolset_contains(const SymbolSet *s, int sym);
void symbolset_union(SymbolSet *dst, const SymbolSet *src); /* returns via dst */

/* Core FIRST-set computation over the whole grammar (fixed-point). */
FirstSetsTable *first_sets_compute(Grammar *g);
void first_sets_destroy(FirstSetsTable *t);

/*
 * FIRST of a symbol string seq[0..len-1] followed by a single lookahead
 * symbol `trailing_lookahead` (this is exactly FIRST(beta a) used when
 * building LR(1) items in CLOSURE). Caller owns the returned SymbolSet
 * and must call symbolset_free on it.
 */
SymbolSet first_of_sequence(const FirstSetsTable *t, const int *seq, int len,
                             int trailing_lookahead);

void first_sets_print(const Grammar *g, const FirstSetsTable *t);

#endif /* CLR_FIRST_H */
