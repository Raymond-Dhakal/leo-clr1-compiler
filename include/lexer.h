/*
 * lexer.h
 * Owner: Member 1 - Gurutheja Kavuturi (24BEL0086)
 *
 * Converts a raw input string (the string to be parsed, NOT the grammar
 * file) into a sequence of grammar symbol IDs, using the terminal names
 * already registered in the Grammar's symbol table. This is intentionally
 * separate from grammar.c: grammar.c owns *what the symbols are*, lexer.c
 * owns *how raw text becomes a stream of those symbols*.
 */
#ifndef CLR_LEXER_H
#define CLR_LEXER_H

#include "grammar.h"

typedef struct {
    int *tokens;     /* array of symbol ids, terminated implicitly by count */
    int count;
    int capacity;
} TokenStream;

TokenStream *lexer_tokenize(const Grammar *g, const char *input_line);
void lexer_destroy(TokenStream *ts);
void lexer_print(const Grammar *g, const TokenStream *ts);

#endif /* CLR_LEXER_H */
