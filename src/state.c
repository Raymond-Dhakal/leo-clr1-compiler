/*
 * state.c
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 */
#include <stdio.h>
#include <stdlib.h>
#include "state.h"

#define INITIAL_TRANSITION_CAP 8

State *state_create(int id, ItemSet *items) {
    State *s = (State *)malloc(sizeof(State));
    s->id = id;
    s->items = items;
    s->transition_capacity = INITIAL_TRANSITION_CAP;
    s->transitions = (Transition *)malloc(sizeof(Transition) * s->transition_capacity);
    s->transition_count = 0;
    return s;
}

void state_destroy(State *state) {
    if (!state) return;
    itemset_destroy(state->items);
    free(state->transitions);
    free(state);
}

void state_add_transition(State *state, int symbol, int target) {
    if (state_find_transition(state, symbol) != -1) return; /* already recorded */
    if (state->transition_count >= state->transition_capacity) {
        state->transition_capacity *= 2;
        state->transitions = (Transition *)realloc(state->transitions,
                                  sizeof(Transition) * state->transition_capacity);
    }
    state->transitions[state->transition_count].symbol = symbol;
    state->transitions[state->transition_count].target_state = target;
    state->transition_count++;
}

int state_find_transition(const State *state, int symbol) {
    for (int i = 0; i < state->transition_count; i++) {
        if (state->transitions[i].symbol == symbol) return state->transitions[i].target_state;
    }
    return -1;
}

void state_print(const Grammar *g, const State *state) {
    printf("State I%d:\n", state->id);
    itemset_print(g, state->items);
    for (int i = 0; i < state->transition_count; i++) {
        printf("  on %s -> I%d\n",
               grammar_symbol_name(g, state->transitions[i].symbol),
               state->transitions[i].target_state);
    }
}
