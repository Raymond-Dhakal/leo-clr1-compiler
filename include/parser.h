/*
 * parser.h
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * Drives the standard LR shift-reduce-goto-accept algorithm over the
 * ACTION/GOTO tables produced by parsing_table.c, using a parallel
 * state stack and symbol stack.
 */
#ifndef CLR_PARSER_H
#define CLR_PARSER_H

#include "grammar.h"
#include "parsing_table.h"

typedef enum {
    PARSE_ACCEPT,
    PARSE_ERROR
} ParseResult;

/*
 * input_tokens: array of terminal symbol ids representing the input
 * string, WITHOUT a trailing $ (the parser appends it internally).
 * If verbose is non-zero, prints a full parse trace (stack / input /
 * action) suitable for review screenshots.
 */
ParseResult parser_run(const Grammar *g, const ParsingTable *pt,
                        const int *input_tokens, int input_len, int verbose);

/* ------------------------------------------------------------------
 * Semantic-value extension (added for the Leo compiler).
 *
 * parser_run_hooks() executes exactly the same table-driven algorithm as
 * parser_run(), but keeps a third stack of opaque semantic values next to
 * the state and symbol stacks and calls back into the client:
 *   shift   - called when terminal token number `token_index` is shifted;
 *             returns the value to store for it (e.g. a Token pointer).
 *   reduce  - called on every reduction A -> X1..Xn with the n values of
 *             X1..Xn; returns the value for A (e.g. a new AST node). The
 *             callback takes ownership of the child values.
 *   discard - called for every value still on the stack when parsing stops
 *             (error), so the client can free partial results.
 * The parser itself prints nothing on errors; the caller reports them
 * using the returned ParseOutcome.
 * ------------------------------------------------------------------ */
typedef struct {
    void *userdata;
    void *(*shift)(void *userdata, int token_index);
    void *(*reduce)(void *userdata, int production_id, void **children, int child_count);
    void (*discard)(void *userdata, int symbol, void *value);
} ParserHooks;

typedef struct {
    ParseResult result;
    void *value;         /* value of the start symbol when result == PARSE_ACCEPT */
    int error_index;     /* index of the offending token (== input_len means $)    */
    int error_state;     /* parser state in which the error was detected           */
    int error_symbol;    /* offending lookahead symbol id (or LHS for a missing GOTO) */
    int internal_error;  /* 1 if a GOTO entry was missing (corrupt table), else 0  */
} ParseOutcome;

/* `hooks` may be NULL (then no values are tracked). */
ParseOutcome parser_run_hooks(const Grammar *g, const ParsingTable *pt,
                              const int *input_tokens, int input_len,
                              const ParserHooks *hooks, int verbose);

/*
 * Writes into `out` (at most `max` entries) the ids of all terminals / $
 * that have a non-empty ACTION entry in `state`, i.e. the tokens the
 * parser would have accepted there. Because the table is canonical LR(1),
 * this is the exact "expected" set. Returns the number of entries written.
 */
int parser_expected_symbols(const Grammar *g, const ParsingTable *pt,
                            int state, int *out, int max);

#endif /* CLR_PARSER_H */
