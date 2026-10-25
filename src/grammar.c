/*
 * grammar.c
 * Owner: Member 1 - Gurutheja Kavuturi (24BEL0086)
 *
 * Grammar file format (documented in README.md):
 *
 *   %start E
 *   E -> E + T | T
 *   T -> T * F | F
 *   F -> ( E ) | id
 *   B -> EPSILON
 *
 *   - Whitespace-separated tokens only (no single-character grammars).
 *   - "%start X" declares the start symbol (must appear before/along
 *     with the productions; order of productions in the file doesn't
 *     matter otherwise).
 *   - "->" separates LHS from RHS, "|" separates alternative bodies.
 *   - "EPSILON" denotes an empty production body.
 *   - "$" is reserved for end-of-input and must not appear in the
 *     grammar file; it is added automatically.
 *   - Any symbol that is never used as the LHS of a production is
 *     classified as a terminal.
 *   - Lines starting with '#' and blank lines are ignored.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grammar.h"

#define INITIAL_SYMBOL_CAP 32
#define INITIAL_PROD_CAP 32

Grammar *grammar_create(void) {
    Grammar *g = (Grammar *)malloc(sizeof(Grammar));
    if (!g) { fprintf(stderr, "grammar_create: out of memory\n"); exit(1); }

    g->symbol_capacity = INITIAL_SYMBOL_CAP;
    g->symbols = (Symbol *)malloc(sizeof(Symbol) * g->symbol_capacity);
    g->symbol_count = 0;

    g->production_capacity = INITIAL_PROD_CAP;
    g->productions = (Production *)malloc(sizeof(Production) * g->production_capacity);
    g->production_count = 0;

    g->start_symbol = -1;
    g->augmented_start = -1;
    g->augmented_production = -1;

    /* Reserve EPSILON and $ up front so every Grammar has stable IDs for them. */
    g->epsilon_symbol = grammar_add_symbol(g, EPSILON_NAME, SYM_EPSILON);
    g->end_symbol = grammar_add_symbol(g, END_NAME, SYM_END);

    return g;
}

void grammar_destroy(Grammar *g) {
    if (!g) return;
    for (int i = 0; i < g->production_count; i++) {
        free(g->productions[i].rhs);
    }
    free(g->productions);
    free(g->symbols);
    free(g);
}

int grammar_find_symbol(const Grammar *g, const char *name) {
    for (int i = 0; i < g->symbol_count; i++) {
        if (strcmp(g->symbols[i].name, name) == 0) return i;
    }
    return -1;
}

int grammar_add_symbol(Grammar *g, const char *name, SymbolType type) {
    int existing = grammar_find_symbol(g, name);
    if (existing != -1) {
        /* Allow refining UNKNOWN -> NONTERMINAL once we see it as an LHS. */
        if (g->symbols[existing].type == SYM_UNKNOWN && type != SYM_UNKNOWN) {
            g->symbols[existing].type = type;
        }
        return existing;
    }
    if (g->symbol_count >= g->symbol_capacity) {
        g->symbol_capacity *= 2;
        g->symbols = (Symbol *)realloc(g->symbols, sizeof(Symbol) * g->symbol_capacity);
        if (!g->symbols) { fprintf(stderr, "grammar_add_symbol: out of memory\n"); exit(1); }
    }
    Symbol *s = &g->symbols[g->symbol_count];
    s->id = g->symbol_count;
    strncpy(s->name, name, MAX_NAME_LEN - 1);
    s->name[MAX_NAME_LEN - 1] = '\0';
    s->type = type;
    g->symbol_count++;
    return s->id;
}

int grammar_add_production(Grammar *g, int lhs, int *rhs, int rhs_len) {
    if (g->production_count >= g->production_capacity) {
        g->production_capacity *= 2;
        g->productions = (Production *)realloc(g->productions,
                              sizeof(Production) * g->production_capacity);
        if (!g->productions) { fprintf(stderr, "grammar_add_production: out of memory\n"); exit(1); }
    }
    Production *p = &g->productions[g->production_count];
    p->id = g->production_count;
    p->lhs = lhs;
    p->rhs_len = rhs_len;
    if (rhs_len > 0) {
        p->rhs = (int *)malloc(sizeof(int) * rhs_len);
        memcpy(p->rhs, rhs, sizeof(int) * rhs_len);
    } else {
        p->rhs = NULL;
    }
    /* Make sure the LHS is (or becomes) known as a non-terminal. */
    g->symbols[lhs].type = SYM_NONTERMINAL;
    g->production_count++;
    return p->id;
}

int grammar_is_terminal(const Grammar *g, int sym_id) {
    return g->symbols[sym_id].type == SYM_TERMINAL;
}
int grammar_is_nonterminal(const Grammar *g, int sym_id) {
    return g->symbols[sym_id].type == SYM_NONTERMINAL;
}
int grammar_is_epsilon(const Grammar *g, int sym_id) {
    return sym_id == g->epsilon_symbol;
}
int grammar_is_end(const Grammar *g, int sym_id) {
    return sym_id == g->end_symbol;
}
const char *grammar_symbol_name(const Grammar *g, int sym_id) {
    return g->symbols[sym_id].name;
}

/* ---------- Parsing the grammar file ---------- */

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return s;
}

/* Split a production body like "E + T" into symbol-name tokens. */
static int split_tokens(char *body, char **out_tokens, int max_tokens) {
    int n = 0;
    char *tok = strtok(body, " \t");
    while (tok && n < max_tokens) {
        out_tokens[n++] = tok;
        tok = strtok(NULL, " \t");
    }
    return n;
}

int grammar_load_from_file(Grammar *g, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "grammar_load_from_file: cannot open '%s'\n", filename);
        return -1;
    }

    char line[1024];
    char start_name[MAX_NAME_LEN] = {0};

    /* First pass: read every line, split "LHS -> alt1 | alt2 | ..." and
       record raw alternative strings so we can add symbols/productions
       in a second pass once we know which symbols are non-terminals. */
    typedef struct { char lhs[MAX_NAME_LEN]; char alt[512]; } RawProd;
    RawProd raw[256];
    int raw_count = 0;

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

        if (strncmp(trimmed, "%start", 6) == 0) {
            char sname[MAX_NAME_LEN];
            if (sscanf(trimmed, "%%start %63s", sname) == 1) {
                strncpy(start_name, sname, MAX_NAME_LEN - 1);
            }
            continue;
        }

        char *arrow = strstr(trimmed, "->");
        if (!arrow) {
            fprintf(stderr, "grammar_load_from_file: malformed line (no '->'): %s\n", trimmed);
            continue;
        }
        *arrow = '\0';
        char *lhs_name = trim(trimmed);
        char *rest = trim(arrow + 2);

        /* split alternatives on '|' */
        char *alt_ctx = NULL;
        char *alt = strtok_r(rest, "|", &alt_ctx);
        while (alt) {
            char *alt_trimmed = trim(alt);
            if (raw_count >= 256) {
                fprintf(stderr, "grammar_load_from_file: too many productions\n");
                break;
            }
            strncpy(raw[raw_count].lhs, lhs_name, MAX_NAME_LEN - 1);
            raw[raw_count].lhs[MAX_NAME_LEN - 1] = '\0';
            strncpy(raw[raw_count].alt, alt_trimmed, sizeof(raw[raw_count].alt) - 1);
            raw[raw_count].alt[sizeof(raw[raw_count].alt) - 1] = '\0';
            raw_count++;
            alt = strtok_r(NULL, "|", &alt_ctx);
        }
    }
    fclose(f);

    /* Pass 2a: register every LHS as a non-terminal first, so RHS
       occurrences of the same name are recognized correctly later. */
    for (int i = 0; i < raw_count; i++) {
        grammar_add_symbol(g, raw[i].lhs, SYM_NONTERMINAL);
    }

    /* Pass 2b: register RHS symbols (as UNKNOWN unless already a
       non-terminal / EPSILON), then build productions. */
    for (int i = 0; i < raw_count; i++) {
        int lhs_id = grammar_find_symbol(g, raw[i].lhs);

        char body[512];
        strncpy(body, raw[i].alt, sizeof(body) - 1);
        body[sizeof(body) - 1] = '\0';

        char *tokens[64];
        int ntok = split_tokens(body, tokens, 64);

        int rhs_ids[64];
        int rhs_len = 0;

        for (int t = 0; t < ntok; t++) {
            if (strcmp(tokens[t], EPSILON_NAME) == 0) {
                /* epsilon production: contributes no RHS symbols */
                continue;
            }
            int existing = grammar_find_symbol(g, tokens[t]);
            int sid;
            if (existing != -1) {
                sid = existing;
            } else {
                sid = grammar_add_symbol(g, tokens[t], SYM_UNKNOWN);
            }
            rhs_ids[rhs_len++] = sid;
        }

        grammar_add_production(g, lhs_id, rhs_ids, rhs_len);
    }

    /* Anything still UNKNOWN never appeared as an LHS -> terminal. */
    for (int i = 0; i < g->symbol_count; i++) {
        if (g->symbols[i].type == SYM_UNKNOWN) {
            g->symbols[i].type = SYM_TERMINAL;
        }
    }

    if (start_name[0] == '\0') {
        fprintf(stderr, "grammar_load_from_file: missing '%%start' declaration\n");
        return -1;
    }
    int start_id = grammar_find_symbol(g, start_name);
    if (start_id == -1) {
        fprintf(stderr, "grammar_load_from_file: start symbol '%s' not defined by any production\n", start_name);
        return -1;
    }
    g->start_symbol = start_id;
    return 0;
}

void grammar_augment(Grammar *g) {
    if (g->start_symbol == -1) {
        fprintf(stderr, "grammar_augment: start symbol not set\n");
        return;
    }
    char aug_name[MAX_NAME_LEN];
    snprintf(aug_name, sizeof(aug_name), "%s'", grammar_symbol_name(g, g->start_symbol));
    /* Avoid clashing with an existing symbol of the same augmented name. */
    while (grammar_find_symbol(g, aug_name) != -1) {
        strncat(aug_name, "'", sizeof(aug_name) - strlen(aug_name) - 1);
    }
    g->augmented_start = grammar_add_symbol(g, aug_name, SYM_NONTERMINAL);
    int rhs[1] = { g->start_symbol };
    g->augmented_production = grammar_add_production(g, g->augmented_start, rhs, 1);
}

/* ---------- Debug printing ---------- */

void production_print(const Grammar *g, const Production *p) {
    printf("%s ->", grammar_symbol_name(g, p->lhs));
    if (p->rhs_len == 0) {
        printf(" %s", EPSILON_NAME);
    } else {
        for (int i = 0; i < p->rhs_len; i++) {
            printf(" %s", grammar_symbol_name(g, p->rhs[i]));
        }
    }
}

void grammar_print(const Grammar *g) {
    printf("=== Grammar ===\n");
    printf("Start symbol: %s\n", g->start_symbol != -1 ? grammar_symbol_name(g, g->start_symbol) : "(none)");
    if (g->augmented_start != -1) {
        printf("Augmented start: %s\n", grammar_symbol_name(g, g->augmented_start));
    }
    printf("Terminals: ");
    for (int i = 0; i < g->symbol_count; i++)
        if (g->symbols[i].type == SYM_TERMINAL) printf("%s ", g->symbols[i].name);
    printf("\nNon-terminals: ");
    for (int i = 0; i < g->symbol_count; i++)
        if (g->symbols[i].type == SYM_NONTERMINAL) printf("%s ", g->symbols[i].name);
    printf("\nProductions:\n");
    for (int i = 0; i < g->production_count; i++) {
        printf("  [%d] ", i);
        production_print(g, &g->productions[i]);
        printf("\n");
    }
}
