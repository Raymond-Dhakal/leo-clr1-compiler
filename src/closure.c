/*
 * closure.c
 * Owner: Member 3 - Goyal Hemant Anil (24BYB0059)
 *
 * CLOSURE(I) for Canonical LR(1):
 *   for every item [A -> alpha . B beta, a] in I where B is a
 *   non-terminal, and for every production B -> gamma, add
 *   [B -> . gamma, b] for every b in FIRST(beta a).
 * Repeat until no more items can be added (fixed point).
 *
 * This is what distinguishes CLR(1) from LALR/SLR: lookaheads are
 * computed fresh per item via FIRST(beta a), never approximated with
 * FOLLOW(B).
 */
#include <stdio.h>
#include <stdlib.h>
#include "closure.h"

#define INITIAL_ITEMSET_CAP 16

ItemSet *itemset_create(void) {
    ItemSet *set = (ItemSet *)malloc(sizeof(ItemSet));
    set->capacity = INITIAL_ITEMSET_CAP;
    set->items = (LR1Item *)malloc(sizeof(LR1Item) * set->capacity);
    set->count = 0;
    return set;
}

void itemset_destroy(ItemSet *set) {
    if (!set) return;
    free(set->items);
    free(set);
}

int itemset_add_item(ItemSet *set, LR1Item item) {
    /* Same test as lr1_item_equal(), written out: this loop dominates the time
       needed to build the canonical collection. */
    for (int i = 0; i < set->count; i++) {
        const LR1Item *it = &set->items[i];
        if (it->production_id == item.production_id && it->dot_pos == item.dot_pos &&
            it->lookahead == item.lookahead) return 0;  /* already present */
    }
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        set->items = (LR1Item *)realloc(set->items, sizeof(LR1Item) * set->capacity);
    }
    set->items[set->count++] = item;
    return 1;
}

ItemSet *itemset_copy(const ItemSet *set) {
    ItemSet *copy = itemset_create();
    for (int i = 0; i < set->count; i++) itemset_add_item(copy, set->items[i]);
    return copy;
}

/* Two item sets are equal (as canonical LR(1) states) iff they contain
   exactly the same set of (core, lookahead) items, order independent. */
int itemset_equal(const ItemSet *a, const ItemSet *b) {
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++) {
        int found = 0;
        for (int j = 0; j < b->count; j++) {
            const LR1Item *x = &a->items[i], *y = &b->items[j];
            if (x->production_id == y->production_id && x->dot_pos == y->dot_pos &&
                x->lookahead == y->lookahead) { found = 1; break; }
        }
        if (!found) return 0;
    }
    return 1;
}

void itemset_print(const Grammar *g, const ItemSet *set) {
    for (int i = 0; i < set->count; i++) {
        printf("  ");
        lr1_item_print(g, &set->items[i]);
        printf("\n");
    }
}

ItemSet *closure_compute(Grammar *g, const FirstSetsTable *first, const ItemSet *I) {
    ItemSet *result = itemset_copy(I);

    int changed = 1;
    while (changed) {
        changed = 0;
        /* Snapshot count: iterate up to the count at the START of this
           pass; newly appended items are picked up on the NEXT pass.
           This avoids re-scanning items on every single addition while
           still reaching the correct fixed point. */
        int pass_count = result->count;
        for (int i = 0; i < pass_count; i++) {
            LR1Item item = result->items[i];
            int B = lr1_item_dot_symbol(g, &item);
            if (B == -1 || !grammar_is_nonterminal(g, B)) continue;

            const Production *p_item = &g->productions[item.production_id];
            int beta_len = p_item->rhs_len - (item.dot_pos + 1);
            const int *beta = (beta_len > 0) ? &p_item->rhs[item.dot_pos + 1] : NULL;

            SymbolSet lookaheads = first_of_sequence(first, beta, beta_len, item.lookahead);

            for (int p = 0; p < g->production_count; p++) {
                if (g->productions[p].lhs != B) continue;
                for (int L = 0; L < lookaheads.count; L++) {
                    LR1Item new_item = lr1_item_create(p, 0, lookaheads.symbols[L]);
                    if (itemset_add_item(result, new_item)) changed = 1;
                }
            }
            symbolset_free(&lookaheads);
        }
    }
    return result;
}
