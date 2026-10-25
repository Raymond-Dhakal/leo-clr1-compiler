/*
 * main.c
 * Integration point (Member 4 owns the integration layer).
 *
 * Coordinates the full pipeline. Deliberately thin: every algorithm
 * lives in its own module, main.c just calls them in order.
 *
 * Usage:
 *   clr_parser <grammar_file> [input_string]
 *
 * If input_string is omitted, the tool still builds and prints the
 * grammar, FIRST sets, canonical collection, parsing table, and any
 * conflicts, then exits (useful for Review-1 screenshots).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grammar.h"
#include "lexer.h"
#include "first.h"
#include "closure.h"
#include "canonical.h"
#include "parsing_table.h"
#include "conflict.h"
#include "parser.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <grammar_file> [input_string] [-v]\n", prog);
    fprintf(stderr, "  -v  verbose parse trace\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *grammar_file = argv[1];
    const char *input_string = NULL;
    int verbose = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else input_string = argv[i];
    }

    /* 1. Load grammar (Member 1) */
    Grammar *g = grammar_create();
    if (grammar_load_from_file(g, grammar_file) != 0) {
        fprintf(stderr, "Failed to load grammar from '%s'\n", grammar_file);
        grammar_destroy(g);
        return 1;
    }
    grammar_augment(g);
    grammar_print(g);
    printf("\n");

    /* 2. FIRST sets / lookahead foundation (Member 2) */
    FirstSetsTable *first = first_sets_compute(g);
    first_sets_print(g, first);
    printf("\n");

    /* 3. Canonical LR(1) collection via CLOSURE + GOTO (Member 3 + 4) */
    CanonicalCollection *cc = canonical_build(g, first);
    canonical_print(cc);

    /* 4. Parsing table + conflict analysis (Member 4) */
    ConflictList *conflicts = conflict_list_create();
    ParsingTable *pt = parsing_table_build(g, cc, conflicts);
    parsing_table_print(pt);
    printf("\n");
    conflict_list_print(g, conflicts);
    printf("\n");

    /* 5. Parse an input string, if one was given */
    if (input_string) {
        printf("=== Parsing input: \"%s\" ===\n", input_string);
        TokenStream *ts = lexer_tokenize(g, input_string);
        if (!ts) {
            printf("Lexical error: input contains a token not defined as a terminal.\n");
        } else {
            ParseResult result = parser_run(g, pt, ts->tokens, ts->count, verbose);
            printf("Result: %s\n", result == PARSE_ACCEPT ? "ACCEPTED" : "REJECTED");
            lexer_destroy(ts);
        }
    } else {
        printf("(No input string given; skipping parse step. Pass one as the 2nd argument.)\n");
    }

    /* Cleanup */
    conflict_list_destroy(conflicts);
    parsing_table_destroy(pt);
    canonical_destroy(cc);
    first_sets_destroy(first);
    grammar_destroy(g);

    return 0;
}
