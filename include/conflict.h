/*
 * conflict.h
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * Detects and records shift-reduce and reduce-reduce conflicts while
 * the ACTION table is being built. Conflicting actions are NEVER
 * silently overwritten: the first action wins in the table, and every
 * conflict is recorded here for reporting.
 */
#ifndef CLR_CONFLICT_H
#define CLR_CONFLICT_H

#include "grammar.h"

typedef enum {
    ACTION_NONE,
    ACTION_SHIFT,
    ACTION_REDUCE,
    ACTION_ACCEPT,
    ACTION_ERROR
} ActionType;

typedef struct {
    ActionType type;
    int value; /* SHIFT: target state id. REDUCE: production id. else: unused */
} ParsingAction;

typedef enum {
    CONFLICT_SHIFT_REDUCE,
    CONFLICT_REDUCE_REDUCE
} ConflictType;

typedef struct {
    ConflictType type;
    int state;
    int symbol;             /* lookahead terminal the conflict is on */
    ParsingAction existing;
    ParsingAction attempted;
} Conflict;

typedef struct {
    Conflict *items;
    int count;
    int capacity;
} ConflictList;

ConflictList *conflict_list_create(void);
void conflict_list_destroy(ConflictList *list);
void conflict_list_add(ConflictList *list, Conflict c);
void conflict_list_print(const Grammar *g, const ConflictList *list);

#endif /* CLR_CONFLICT_H */
