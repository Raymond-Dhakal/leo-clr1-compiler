/*
 * conflict.c
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 */
#include <stdio.h>
#include <stdlib.h>
#include "conflict.h"

#define INITIAL_CONFLICT_CAP 8

ConflictList *conflict_list_create(void) {
    ConflictList *list = (ConflictList *)malloc(sizeof(ConflictList));
    list->capacity = INITIAL_CONFLICT_CAP;
    list->items = (Conflict *)malloc(sizeof(Conflict) * list->capacity);
    list->count = 0;
    return list;
}

void conflict_list_destroy(ConflictList *list) {
    if (!list) return;
    free(list->items);
    free(list);
}

void conflict_list_add(ConflictList *list, Conflict c) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->items = (Conflict *)realloc(list->items, sizeof(Conflict) * list->capacity);
    }
    list->items[list->count++] = c;
}

static const char *action_type_name(ActionType t) {
    switch (t) {
        case ACTION_SHIFT: return "SHIFT";
        case ACTION_REDUCE: return "REDUCE";
        case ACTION_ACCEPT: return "ACCEPT";
        case ACTION_ERROR: return "ERROR";
        default: return "NONE";
    }
}

static void print_action(const Grammar *g, ParsingAction a) {
    if (a.type == ACTION_SHIFT) {
        printf("%s I%d", action_type_name(a.type), a.value);
    } else if (a.type == ACTION_REDUCE) {
        printf("%s (", action_type_name(a.type));
        production_print(g, &g->productions[a.value]);
        printf(")");
    } else {
        printf("%s", action_type_name(a.type));
    }
}

void conflict_list_print(const Grammar *g, const ConflictList *list) {
    if (list->count == 0) {
        printf("=== Conflicts ===\nNone. Grammar is Canonical-LR(1).\n");
        return;
    }
    printf("=== Conflicts (%d) ===\n", list->count);
    for (int i = 0; i < list->count; i++) {
        const Conflict *c = &list->items[i];
        printf("State I%d, lookahead '%s': %s conflict\n",
               c->state, grammar_symbol_name(g, c->symbol),
               c->type == CONFLICT_SHIFT_REDUCE ? "shift-reduce" : "reduce-reduce");
        printf("  existing action : ");
        print_action(g, c->existing);
        printf("\n  attempted action: ");
        print_action(g, c->attempted);
        printf("\n  (kept existing action; attempted action was NOT applied)\n");
    }
}
