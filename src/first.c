/*
 * first.c
 * Owner: Member 2 - Ephraim Immanuel (24BEL0091)
 *
 * Standard fixed-point computation of nullability and FIRST sets for
 * every grammar symbol, plus FIRST-of-a-sequence used by CLOSURE to
 * compute LR(1) lookaheads (FIRST(beta a)).
 */
#include <stdio.h>
#include <stdlib.h>
#include "first.h"

#define INITIAL_SET_CAP 8

void symbolset_init(SymbolSet *s) {
    s->capacity = INITIAL_SET_CAP;
    s->symbols = (int *)malloc(sizeof(int) * s->capacity);
    s->count = 0;
}

void symbolset_free(SymbolSet *s) {
    free(s->symbols);
    s->symbols = NULL;
    s->count = 0;
    s->capacity = 0;
}

int symbolset_contains(const SymbolSet *s, int sym) {
    for (int i = 0; i < s->count; i++)
        if (s->symbols[i] == sym) return 1;
    return 0;
}

int symbolset_add(SymbolSet *s, int sym) {
    if (symbolset_contains(s, sym)) return 0;
    if (s->count >= s->capacity) {
        s->capacity *= 2;
        s->symbols = (int *)realloc(s->symbols, sizeof(int) * s->capacity);
    }
    s->symbols[s->count++] = sym;
    return 1;
}

void symbolset_union(SymbolSet *dst, const SymbolSet *src) {
    for (int i = 0; i < src->count; i++) symbolset_add(dst, src->symbols[i]);
}

FirstSetsTable *first_sets_compute(Grammar *g) {
    FirstSetsTable *t = (FirstSetsTable *)malloc(sizeof(FirstSetsTable));
    t->grammar = g;
    t->first_sets = (SymbolSet *)malloc(sizeof(SymbolSet) * g->symbol_count);
    t->nullable = (int *)calloc(g->symbol_count, sizeof(int));

    for (int i = 0; i < g->symbol_count; i++) symbolset_init(&t->first_sets[i]);

    /* Base cases: FIRST(terminal) = {terminal}. EPSILON is nullable and
       FIRST(EPSILON) = {EPSILON} conceptually, but we treat nullability
       via the `nullable` flag rather than stuffing EPSILON into FIRST
       sets of non-terminals (cleaner for the closure computation). */
    for (int i = 0; i < g->symbol_count; i++) {
        if (g->symbols[i].type == SYM_TERMINAL || g->symbols[i].type == SYM_END) {
            symbolset_add(&t->first_sets[i], i);
        }
        if (g->symbols[i].type == SYM_EPSILON) {
            t->nullable[i] = 1;
        }
    }

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int p = 0; p < g->production_count; p++) {
            Production *prod = &g->productions[p];
            int A = prod->lhs;

            /* A -> epsilon (rhs_len == 0) makes A nullable. */
            if (prod->rhs_len == 0) {
                if (!t->nullable[A]) { t->nullable[A] = 1; changed = 1; }
                continue;
            }

            int all_nullable_so_far = 1;
            for (int k = 0; k < prod->rhs_len; k++) {
                int Xk = prod->rhs[k];

                if (all_nullable_so_far) {
                    /* Add FIRST(Xk) \ {} into FIRST(A) */
                    for (int j = 0; j < t->first_sets[Xk].count; j++) {
                        if (symbolset_add(&t->first_sets[A], t->first_sets[Xk].symbols[j]))
                            changed = 1;
                    }
                }
                if (!t->nullable[Xk]) {
                    all_nullable_so_far = 0;
                    break;
                }
            }
            /* If every symbol on the RHS is nullable, A is nullable too. */
            if (all_nullable_so_far && !t->nullable[A]) {
                t->nullable[A] = 1;
                changed = 1;
            }
        }
    }

    return t;
}

void first_sets_destroy(FirstSetsTable *t) {
    if (!t) return;
    for (int i = 0; i < t->grammar->symbol_count; i++) symbolset_free(&t->first_sets[i]);
    free(t->first_sets);
    free(t->nullable);
    free(t);
}

SymbolSet first_of_sequence(const FirstSetsTable *t, const int *seq, int len,
                             int trailing_lookahead) {
    SymbolSet result;
    symbolset_init(&result);

    int all_nullable_so_far = 1;
    for (int i = 0; i < len; i++) {
        int Xi = seq[i];
        symbolset_union(&result, &t->first_sets[Xi]);
        if (!t->nullable[Xi]) {
            all_nullable_so_far = 0;
            break;
        }
    }
    if (all_nullable_so_far) {
        /* every symbol in seq can vanish -> the trailing lookahead itself
           is also a valid "first" symbol of (seq, lookahead) */
        symbolset_add(&result, trailing_lookahead);
    }
    return result;
}

void first_sets_print(const Grammar *g, const FirstSetsTable *t) {
    printf("=== FIRST sets ===\n");
    for (int i = 0; i < g->symbol_count; i++) {
        if (g->symbols[i].type != SYM_NONTERMINAL) continue;
        printf("FIRST(%s) = { ", grammar_symbol_name(g, i));
        for (int j = 0; j < t->first_sets[i].count; j++) {
            printf("%s ", grammar_symbol_name(g, t->first_sets[i].symbols[j]));
        }
        if (t->nullable[i]) printf("EPSILON ");
        printf("}\n");
    }
}
