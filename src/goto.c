/*
 * goto.c
 * Owner: Member 3 - Goyal Hemant Anil (24BYB0059)
 *
 * GOTO(I, X) = CLOSURE({ [A -> alpha X . beta, a] |
 *                          [A -> alpha . X beta, a] in I })
 */
#include <stdlib.h>
#include "goto.h"

ItemSet *goto_compute(Grammar *g, const FirstSetsTable *first,
                       const ItemSet *I, int symbol) {
    ItemSet *moved = itemset_create();
    int any = 0;

    for (int i = 0; i < I->count; i++) {
        const LR1Item *item = &I->items[i];
        int dot_sym = lr1_item_dot_symbol(g, item);
        if (dot_sym == symbol) {
            LR1Item advanced = lr1_item_create(item->production_id,
                                                item->dot_pos + 1,
                                                item->lookahead);
            itemset_add_item(moved, advanced);
            any = 1;
        }
    }

    if (!any) {
        itemset_destroy(moved);
        return NULL;
    }

    ItemSet *closed = closure_compute(g, first, moved);
    itemset_destroy(moved);
    return closed;
}
