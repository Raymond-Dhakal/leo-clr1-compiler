/*
 * lr1_item.h
 * Owner: Member 3 - Goyal Hemant Anil (24BYB0059)
 *
 * An LR(1) item: [A -> alpha . beta, a]
 * Stored compactly as (production_id, dot_pos, lookahead) rather than
 * copying symbol arrays around; the actual production body lives in
 * Grammar->productions[production_id].
 */
#ifndef CLR_LR1_ITEM_H
#define CLR_LR1_ITEM_H

#include "grammar.h"

typedef struct {
    int production_id;
    int dot_pos;      /* number of symbols already consumed (0..rhs_len) */
    int lookahead;    /* symbol id of the single LR(1) lookahead terminal */
} LR1Item;

LR1Item lr1_item_create(int production_id, int dot_pos, int lookahead);
int lr1_item_equal(const LR1Item *a, const LR1Item *b);
int lr1_item_core_equal(const LR1Item *a, const LR1Item *b); /* ignore lookahead */
LR1Item lr1_item_copy(const LR1Item *item);

/* symbol immediately after the dot, or -1 if the dot is at the end */
int lr1_item_dot_symbol(const Grammar *g, const LR1Item *item);

/* true if dot is at the very end of the production (reduce candidate) */
int lr1_item_is_complete(const Grammar *g, const LR1Item *item);

void lr1_item_print(const Grammar *g, const LR1Item *item);

#endif /* CLR_LR1_ITEM_H */
