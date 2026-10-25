/*
 * grammar.h
 * Owner: Member 1 - Gurutheja Kavuturi (24BEL0086)
 *
 * Grammar representation: symbols, productions, augmented grammar.
 * This module owns the canonical integer-ID representation of symbols
 * that EVERY other module (first, lr1_item, closure, goto, state,
 * canonical, parsing_table, conflict, parser) must reuse. Do not
 * invent a second symbol representation anywhere else.
 */
#ifndef CLR_GRAMMAR_H
#define CLR_GRAMMAR_H

#define MAX_NAME_LEN 64
#define EPSILON_NAME "EPSILON"
#define END_NAME "$"

typedef enum {
    SYM_TERMINAL,
    SYM_NONTERMINAL,
    SYM_EPSILON,
    SYM_END,
    SYM_UNKNOWN
} SymbolType;

typedef struct {
    int id;                     /* index into Grammar->symbols          */
    char name[MAX_NAME_LEN];
    SymbolType type;
} Symbol;

typedef struct {
    int id;                     /* index into Grammar->productions      */
    int lhs;                    /* symbol id of left-hand side          */
    int *rhs;                   /* array of symbol ids (NULL if len==0) */
    int rhs_len;
} Production;

typedef struct {
    Symbol *symbols;
    int symbol_count;
    int symbol_capacity;

    Production *productions;
    int production_count;
    int production_capacity;

    int start_symbol;           /* original grammar start symbol id     */
    int augmented_start;        /* S' symbol id                         */
    int augmented_production;   /* id of production S' -> S             */

    int epsilon_symbol;         /* id of EPSILON pseudo-symbol          */
    int end_symbol;             /* id of $ pseudo-symbol                */
} Grammar;

/* lifecycle */
Grammar *grammar_create(void);
void grammar_destroy(Grammar *g);

/* symbol table */
int grammar_add_symbol(Grammar *g, const char *name, SymbolType type);
int grammar_find_symbol(const Grammar *g, const char *name);

/* productions */
int grammar_add_production(Grammar *g, int lhs, int *rhs, int rhs_len);

/* loading + augmenting */
int grammar_load_from_file(Grammar *g, const char *filename);
void grammar_augment(Grammar *g);

/* classification helpers used by every downstream module */
int grammar_is_terminal(const Grammar *g, int sym_id);
int grammar_is_nonterminal(const Grammar *g, int sym_id);
int grammar_is_epsilon(const Grammar *g, int sym_id);
int grammar_is_end(const Grammar *g, int sym_id);
const char *grammar_symbol_name(const Grammar *g, int sym_id);

/* debugging */
void grammar_print(const Grammar *g);
void production_print(const Grammar *g, const Production *p);

#endif /* CLR_GRAMMAR_H */
