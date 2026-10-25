/*
 * state.h
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * A "state" is one canonical LR(1) item set (I_k) plus its outgoing
 * transitions on grammar symbols, discovered while building the
 * canonical collection.
 */
#ifndef CLR_STATE_H
#define CLR_STATE_H

#include "grammar.h"
#include "closure.h"

typedef struct {
    int symbol;        /* grammar symbol id the transition is on */
    int target_state;  /* destination state id                   */
} Transition;

typedef struct {
    int id;
    ItemSet *items;              /* the closed item set for this state */
    Transition *transitions;
    int transition_count;
    int transition_capacity;
} State;

State *state_create(int id, ItemSet *items);
void state_destroy(State *state);

void state_add_transition(State *state, int symbol, int target);
int state_find_transition(const State *state, int symbol); /* -1 if none */

void state_print(const Grammar *g, const State *state);

#endif /* CLR_STATE_H */
