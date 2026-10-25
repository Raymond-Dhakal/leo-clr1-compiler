/*
 * canonical.h
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * Builds C = {I0, I1, I2, ...}, the canonical collection of LR(1) item
 * sets, by repeatedly applying GOTO to already-discovered states until
 * no new state is produced. This module is the glue between
 * FIRST/lookahead (Member 2), LR(1) items + CLOSURE/GOTO (Member 3),
 * and the parsing-table/conflict layer built on top of it (Member 4).
 */
#ifndef CLR_CANONICAL_H
#define CLR_CANONICAL_H

#include "grammar.h"
#include "first.h"
#include "state.h"

typedef struct {
    State **states;
    int count;
    int capacity;
    Grammar *grammar;
    const FirstSetsTable *first;
} CanonicalCollection;

CanonicalCollection *canonical_build(Grammar *g, const FirstSetsTable *first);
void canonical_destroy(CanonicalCollection *c);
void canonical_print(const CanonicalCollection *c);

#endif /* CLR_CANONICAL_H */
