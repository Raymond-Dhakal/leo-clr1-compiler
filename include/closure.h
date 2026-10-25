/*
 * closure.h
 * Owner: Member 3 - Goyal Hemant Anil (24BYB0059)
 *
 * ItemSet = a set of LR(1) items (I in the textbook notation), plus
 * CLOSURE(I). This is full Canonical-LR(1) closure: lookaheads are
 * propagated using FIRST(beta a), NOT the LR(0)/SLR shortcut of
 * reusing FOLLOW(A). Do not replace this with LR(0) closure.
 */
#ifndef CLR_CLOSURE_H
#define CLR_CLOSURE_H

#include "grammar.h"
#include "first.h"
#include "lr1_item.h"

typedef struct {
    LR1Item *items;
    int count;
    int capacity;
} ItemSet;

ItemSet *itemset_create(void);
void itemset_destroy(ItemSet *set);

/*
 * Add an item to the set. If an item with the same CORE (production_id,
 * dot_pos) already exists but with a different lookahead, both items
 * are kept (each (core, lookahead) pair is a distinct LR(1) item).
 * Returns 1 if the exact item (core+lookahead) was newly added, 0 if
 * it was already present (used to detect closure fixed-point).
 */
int itemset_add_item(ItemSet *set, LR1Item item);

ItemSet *itemset_copy(const ItemSet *set);
int itemset_equal(const ItemSet *a, const ItemSet *b);
void itemset_print(const Grammar *g, const ItemSet *set);

/* CLOSURE(I) per the Canonical LR(1) construction. Returns a new ItemSet. */
ItemSet *closure_compute(Grammar *g, const FirstSetsTable *first, const ItemSet *I);

#endif /* CLR_CLOSURE_H */
