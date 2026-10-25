/*
 * nova_parse.h
 * Part of the Leo compiler extension - PHASE 2: syntax analysis.
 *
 * This is the glue between the Leo language and the original CLR(1)
 * parser generator:
 *   1. nova_frontend_build() loads language/grammar.txt and runs the
 *      generator's own pipeline: grammar -> FIRST -> canonical LR(1)
 *      collection -> ACTION/GOTO table (+ conflict detection).
 *   2. nova_parse() feeds the lexer's tokens to the generator's table
 *      driven parser (parser_run_hooks) and builds the AST in the
 *      reduction callback.
 */
#ifndef NOVA_PARSE_H
#define NOVA_PARSE_H

#include "grammar.h"
#include "first.h"
#include "canonical.h"
#include "conflict.h"
#include "parsing_table.h"
#include "parser.h"
#include "nova_ast.h"
#include "nova_lexer.h"
#include "nova_diag.h"

typedef struct LeoFrontend {
    Grammar *grammar;
    FirstSetsTable *first;
    CanonicalCollection *canonical;
    ConflictList *conflicts;
    ParsingTable *table;
    int *symbol_of_kind;     /* TokenKind -> grammar terminal id (-1 if unused) */
    int *kind_of_symbol;     /* grammar symbol id -> TokenKind (-1 if none)     */
    int *action_of_prod;     /* production id -> semantic action id             */
    int type_symbol;         /* id of the non-terminal "Type" (value is not a pointer) */
} LeoFrontend;

/*
 * Builds everything described above. Returns NULL (with a message in
 * errbuf) if the grammar cannot be loaded or does not match the lexer /
 * AST builder. A grammar with parsing conflicts is NOT an error here: the
 * caller must check frontend->conflicts->count and refuse to compile.
 */
LeoFrontend *nova_frontend_build(const char *grammar_path, char *errbuf, size_t errlen);
void nova_frontend_free(LeoFrontend *fe);

/*
 * Parses the token list (which must end with TK_EOF). Returns the AST
 * (AST_PROGRAM) on success, or NULL after adding a "syntax" error to
 * `diags`. With trace != 0 the generator's shift/reduce trace is printed.
 */
AstNode *nova_parse(const LeoFrontend *fe, const TokenList *tokens,
                    DiagList *diags, int trace);

#endif /* NOVA_PARSE_H */
