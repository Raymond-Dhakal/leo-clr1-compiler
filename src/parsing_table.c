/*
 * parsing_table.c
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * For every state Ii and every item in it:
 *   [A -> alpha . a beta, b]  (a terminal)  => ACTION[i,a] = shift GOTO(Ii,a)
 *   [A -> alpha ., a]         (A != S')     => ACTION[i,a] = reduce A -> alpha
 *   [S' -> S ., $]                          => ACTION[i,$] = accept
 * GOTO[i, A] for non-terminal A is copied directly from the state's
 * recorded transition on A.
 *
 * Conflicts are recorded (via conflict.c) and the FIRST action set for
 * a given (state, symbol) cell always wins; later attempts are logged
 * but never overwrite it.
 */
#include <stdio.h>
#include <stdlib.h>
#include "parsing_table.h"
#include "lr1_item.h"

static ParsingAction action_none(void) {
    ParsingAction a; a.type = ACTION_NONE; a.value = -1; return a;
}

static int actions_equal(ParsingAction a, ParsingAction b) {
    return a.type == b.type && a.value == b.value;
}

/* Attempts to set action[state][symbol]. If a different action is
   already there, records a conflict instead of overwriting. */
static void set_action(const Grammar *g, ParsingTable *pt, ConflictList *conflicts,
                        int state, int symbol, ParsingAction new_action) {
    ParsingAction *cell = &pt->action[state][symbol];
    if (cell->type == ACTION_NONE) {
        *cell = new_action;
        return;
    }
    if (actions_equal(*cell, new_action)) return; /* identical, not a conflict */

    Conflict c;
    c.state = state;
    c.symbol = symbol;
    c.existing = *cell;
    c.attempted = new_action;
    c.type = (cell->type == ACTION_SHIFT || new_action.type == ACTION_SHIFT)
                 ? CONFLICT_SHIFT_REDUCE : CONFLICT_REDUCE_REDUCE;
    conflict_list_add(conflicts, c);
    (void)g;
    /* existing action is kept; do not overwrite */
}

ParsingTable *parsing_table_build(const Grammar *g, const CanonicalCollection *cc,
                                   ConflictList *conflicts) {
    ParsingTable *pt = (ParsingTable *)malloc(sizeof(ParsingTable));
    pt->grammar = g;
    pt->num_states = cc->count;
    pt->num_symbols = g->symbol_count;

    pt->action = (ParsingAction **)malloc(sizeof(ParsingAction *) * pt->num_states);
    pt->go_to = (int **)malloc(sizeof(int *) * pt->num_states);
    for (int i = 0; i < pt->num_states; i++) {
        pt->action[i] = (ParsingAction *)malloc(sizeof(ParsingAction) * pt->num_symbols);
        pt->go_to[i] = (int *)malloc(sizeof(int) * pt->num_symbols);
        for (int j = 0; j < pt->num_symbols; j++) {
            pt->action[i][j] = action_none();
            pt->go_to[i][j] = -1;
        }
    }

    for (int i = 0; i < cc->count; i++) {
        State *state = cc->states[i];

        /* Shift + accept actions, and GOTO entries, come from transitions. */
        for (int t = 0; t < state->transition_count; t++) {
            int sym = state->transitions[t].symbol;
            int target = state->transitions[t].target_state;
            if (grammar_is_terminal(g, sym) || grammar_is_end(g, sym)) {
                ParsingAction shift; shift.type = ACTION_SHIFT; shift.value = target;
                set_action(g, pt, conflicts, i, sym, shift);
            } else if (grammar_is_nonterminal(g, sym)) {
                pt->go_to[i][sym] = target;
            }
        }

        /* Reduce + accept actions come from complete items. */
        for (int k = 0; k < state->items->count; k++) {
            const LR1Item *item = &state->items->items[k];
            if (!lr1_item_is_complete(g, item)) continue;

            const Production *p = &g->productions[item->production_id];
            if (p->id == g->augmented_production && item->lookahead == g->end_symbol) {
                ParsingAction accept; accept.type = ACTION_ACCEPT; accept.value = -1;
                set_action(g, pt, conflicts, i, g->end_symbol, accept);
            } else {
                ParsingAction reduce; reduce.type = ACTION_REDUCE; reduce.value = p->id;
                set_action(g, pt, conflicts, i, item->lookahead, reduce);
            }
        }
    }

    return pt;
}

void parsing_table_destroy(ParsingTable *pt) {
    if (!pt) return;
    for (int i = 0; i < pt->num_states; i++) {
        free(pt->action[i]);
        free(pt->go_to[i]);
    }
    free(pt->action);
    free(pt->go_to);
    free(pt);
}

static void print_action_cell(const Grammar *g, ParsingAction a) {
    switch (a.type) {
        case ACTION_SHIFT: printf("s%-4d", a.value); break;
        case ACTION_REDUCE: printf("r%-4d", a.value); break;
        case ACTION_ACCEPT: printf("acc  "); break;
        default: printf("     "); break;
    }
    (void)g;
}

void parsing_table_print(const ParsingTable *pt) {
    const Grammar *g = pt->grammar;
    printf("=== ACTION / GOTO Table ===\n");
    printf("%-6s", "State");
    for (int j = 0; j < pt->num_symbols; j++) {
        if (grammar_is_terminal(g, j) || grammar_is_end(g, j))
            printf("%-6s", grammar_symbol_name(g, j));
    }
    for (int j = 0; j < pt->num_symbols; j++) {
        if (grammar_is_nonterminal(g, j) && j != g->augmented_start)
            printf("%-6s", grammar_symbol_name(g, j));
    }
    printf("\n");

    for (int i = 0; i < pt->num_states; i++) {
        printf("%-6d", i);
        for (int j = 0; j < pt->num_symbols; j++) {
            if (grammar_is_terminal(g, j) || grammar_is_end(g, j)) {
                print_action_cell(g, pt->action[i][j]);
            }
        }
        for (int j = 0; j < pt->num_symbols; j++) {
            if (grammar_is_nonterminal(g, j) && j != g->augmented_start) {
                if (pt->go_to[i][j] != -1) printf("%-6d", pt->go_to[i][j]);
                else printf("%-6s", "");
            }
        }
        printf("\n");
    }
}
