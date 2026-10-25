/*
 * parsing_table.h
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * Builds ACTION[state][terminal] and GOTO[state][nonterminal] from the
 * canonical LR(1) collection, and reports conflicts via conflict.h
 * instead of silently overwriting them.
 */
#ifndef CLR_PARSING_TABLE_H
#define CLR_PARSING_TABLE_H

#include "grammar.h"
#include "canonical.h"
#include "conflict.h"

typedef struct {
    const Grammar *grammar;
    int num_states;
    int num_symbols;
    ParsingAction **action; /* [state][symbol], meaningful for terminals+$ */
    int **go_to;            /* [state][symbol], meaningful for nonterminals, -1 = none */
} ParsingTable;

/*
 * Builds the table. Any conflicts encountered are appended to
 * `conflicts` (must be a valid, already-created ConflictList*).
 */
ParsingTable *parsing_table_build(const Grammar *g, const CanonicalCollection *cc,
                                   ConflictList *conflicts);
void parsing_table_destroy(ParsingTable *pt);
void parsing_table_print(const ParsingTable *pt);

#endif /* CLR_PARSING_TABLE_H */
