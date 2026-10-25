/*
 * lr1_item.c
 * Owner: Member 3 - Goyal Hemant Anil (24BYB0059)
 */
#include <stdio.h>
#include "lr1_item.h"

LR1Item lr1_item_create(int production_id, int dot_pos, int lookahead) {
    LR1Item item;
    item.production_id = production_id;
    item.dot_pos = dot_pos;
    item.lookahead = lookahead;
    return item;
}

int lr1_item_core_equal(const LR1Item *a, const LR1Item *b) {
    return a->production_id == b->production_id && a->dot_pos == b->dot_pos;
}

int lr1_item_equal(const LR1Item *a, const LR1Item *b) {
    return lr1_item_core_equal(a, b) && a->lookahead == b->lookahead;
}

LR1Item lr1_item_copy(const LR1Item *item) {
    return lr1_item_create(item->production_id, item->dot_pos, item->lookahead);
}

int lr1_item_dot_symbol(const Grammar *g, const LR1Item *item) {
    const Production *p = &g->productions[item->production_id];
    if (item->dot_pos >= p->rhs_len) return -1;
    return p->rhs[item->dot_pos];
}

int lr1_item_is_complete(const Grammar *g, const LR1Item *item) {
    const Production *p = &g->productions[item->production_id];
    return item->dot_pos >= p->rhs_len;
}

void lr1_item_print(const Grammar *g, const LR1Item *item) {
    const Production *p = &g->productions[item->production_id];
    printf("[%s ->", grammar_symbol_name(g, p->lhs));
    if (p->rhs_len == 0) {
        printf(" . %s", EPSILON_NAME);
    } else {
        for (int i = 0; i < p->rhs_len; i++) {
            if (i == item->dot_pos) printf(" .");
            printf(" %s", grammar_symbol_name(g, p->rhs[i]));
        }
        if (item->dot_pos == p->rhs_len) printf(" .");
    }
    printf(", %s]", grammar_symbol_name(g, item->lookahead));
}
