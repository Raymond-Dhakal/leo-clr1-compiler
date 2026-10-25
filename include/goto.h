/*
 * goto.h
 * Owner: Member 3 - Goyal Hemant Anil (24BYB0059)
 *
 * GOTO(I, X): move the dot over grammar symbol X for every item in I
 * that has X immediately after the dot, then take the closure of the
 * result. Reuses lr1_item.h and closure.h rather than duplicating them.
 */
#ifndef CLR_GOTO_H
#define CLR_GOTO_H

#include "grammar.h"
#include "first.h"
#include "closure.h"

/*
 * Returns NULL if GOTO(I, X) is empty (no transition on X), otherwise a
 * newly allocated, already-closed ItemSet that the caller owns.
 */
ItemSet *goto_compute(Grammar *g, const FirstSetsTable *first,
                       const ItemSet *I, int symbol);

#endif /* CLR_GOTO_H */
