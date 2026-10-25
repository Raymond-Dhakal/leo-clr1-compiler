/*
 * lexer.c
 * Owner: Member 1 - Gurutheja Kavuturi (24BEL0086)
 *
 * Tokenizes a whitespace-separated input line (e.g. "id + id * id")
 * into the terminal symbol ids already registered by grammar.c.
 * Unknown tokens are reported as errors so parser.c never has to
 * guess about malformed input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

#define INITIAL_TOKEN_CAP 16

static void tokenstream_push(TokenStream *ts, int sym) {
    if (ts->count >= ts->capacity) {
        ts->capacity *= 2;
        ts->tokens = (int *)realloc(ts->tokens, sizeof(int) * ts->capacity);
    }
    ts->tokens[ts->count++] = sym;
}

TokenStream *lexer_tokenize(const Grammar *g, const char *input_line) {
    TokenStream *ts = (TokenStream *)malloc(sizeof(TokenStream));
    ts->capacity = INITIAL_TOKEN_CAP;
    ts->tokens = (int *)malloc(sizeof(int) * ts->capacity);
    ts->count = 0;

    char *buf = strdup(input_line);
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t\r\n", &saveptr);

    int had_error = 0;
    while (tok) {
        int sym = grammar_find_symbol(g, tok);
        if (sym == -1 || !grammar_is_terminal(g, sym)) {
            fprintf(stderr, "lexer_tokenize: unrecognized terminal token '%s'\n", tok);
            had_error = 1;
        } else {
            tokenstream_push(ts, sym);
        }
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    free(buf);

    if (had_error) {
        lexer_destroy(ts);
        return NULL;
    }
    return ts;
}

void lexer_destroy(TokenStream *ts) {
    if (!ts) return;
    free(ts->tokens);
    free(ts);
}

void lexer_print(const Grammar *g, const TokenStream *ts) {
    printf("Tokens:");
    for (int i = 0; i < ts->count; i++) {
        printf(" %s", grammar_symbol_name(g, ts->tokens[i]));
    }
    printf("\n");
}
