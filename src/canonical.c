/*
 * canonical.c
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * Builds the canonical collection C = {I0, I1, ...} via BFS-style
 * worklist over states, computing GOTO(I, X) for every grammar symbol
 * X and every already-discovered state I, merging into an existing
 * state when the resulting item set already exists.
 */
#include <stdio.h>
#include <stdlib.h>
#include "canonical.h"
#include "lr1_item.h"
#include "goto.h"

#define INITIAL_STATE_CAP 16

static int canonical_find_equal_state(const CanonicalCollection *c, const ItemSet *items) {
    for (int i = 0; i < c->count; i++) {
        if (itemset_equal(c->states[i]->items, items)) return i;
    }
    return -1;
}

static void canonical_add_state(CanonicalCollection *c, State *s) {
    if (c->count >= c->capacity) {
        c->capacity *= 2;
        c->states = (State **)realloc(c->states, sizeof(State *) * c->capacity);
    }
    c->states[c->count++] = s;
}

CanonicalCollection *canonical_build(Grammar *g, const FirstSetsTable *first) {
    CanonicalCollection *c = (CanonicalCollection *)malloc(sizeof(CanonicalCollection));
    c->capacity = INITIAL_STATE_CAP;
    c->states = (State **)malloc(sizeof(State *) * c->capacity);
    c->count = 0;
    c->grammar = g;
    c->first = first;

    if (g->augmented_production == -1) {
        fprintf(stderr, "canonical_build: grammar has not been augmented\n");
        return c;
    }

    /* I0 = CLOSURE({ [S' -> . S, $] }) */
    ItemSet *seed = itemset_create();
    LR1Item start_item = lr1_item_create(g->augmented_production, 0, g->end_symbol);
    itemset_add_item(seed, start_item);
    ItemSet *I0 = closure_compute(g, first, seed);
    itemset_destroy(seed);

    State *s0 = state_create(0, I0);
    canonical_add_state(c, s0);

    /* Worklist: process states in the order they were added; new states
       discovered mid-loop are appended to c->states and will be
       reached later in this same for-loop because we re-check c->count
       on every iteration. */
    for (int i = 0; i < c->count; i++) {
        State *current = c->states[i];

        /* Collect the distinct symbols appearing right after a dot in
           this state's item set (candidate transition symbols). */
        int seen_symbols[512];
        int seen_count = 0;

        for (int k = 0; k < current->items->count; k++) {
            int sym = lr1_item_dot_symbol(g, &current->items->items[k]);
            if (sym == -1) continue;
            int already = 0;
            for (int j = 0; j < seen_count; j++) if (seen_symbols[j] == sym) { already = 1; break; }
            if (!already && seen_count < 512) seen_symbols[seen_count++] = sym;
        }

        for (int j = 0; j < seen_count; j++) {
            int X = seen_symbols[j];
            ItemSet *target = goto_compute(g, first, current->items, X);
            if (!target) continue;

            int existing_idx = canonical_find_equal_state(c, target);
            if (existing_idx != -1) {
                state_add_transition(current, X, c->states[existing_idx]->id);
                itemset_destroy(target);
            } else {
                int new_id = c->count;
                State *new_state = state_create(new_id, target);
                canonical_add_state(c, new_state);
                state_add_transition(current, X, new_id);
            }
        }
    }

    return c;
}

void canonical_destroy(CanonicalCollection *c) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) state_destroy(c->states[i]);
    free(c->states);
    free(c);
}

void canonical_print(const CanonicalCollection *c) {
    printf("=== Canonical LR(1) Collection (%d states) ===\n", c->count);
    for (int i = 0; i < c->count; i++) {
        state_print(c->grammar, c->states[i]);
        printf("\n");
    }
}
