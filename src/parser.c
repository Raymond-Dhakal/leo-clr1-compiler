/*
 * parser.c
 * Owner: Member 4 - Raymond Dhakal (24BEL0100)
 *
 * Standard table-driven LR parsing algorithm using a state stack and a
 * symbol stack in lock-step.
 *
 * Leo extension: the algorithm now lives in one shared routine,
 * parser_core(), which optionally maintains a third stack of semantic
 * values (see ParserHooks in parser.h). parser_run() keeps its original
 * behaviour and output; parser_run_hooks() exposes the values so that the
 * Leo compiler can build an AST during the parse.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

typedef struct {
    int *data;
    int count;
    int capacity;
} IntStack;

static void stack_init(IntStack *s) {
    s->capacity = 64;
    s->data = (int *)malloc(sizeof(int) * s->capacity);
    s->count = 0;
}
static void stack_push(IntStack *s, int v) {
    if (s->count >= s->capacity) {
        s->capacity *= 2;
        s->data = (int *)realloc(s->data, sizeof(int) * s->capacity);
    }
    s->data[s->count++] = v;
}
static int stack_pop(IntStack *s) { return s->data[--s->count]; }
static int stack_top(const IntStack *s) { return s->data[s->count - 1]; }
static void stack_free(IntStack *s) { free(s->data); }

/* Same idea for the semantic value stack (opaque pointers). */
typedef struct {
    void **data;
    int count;
    int capacity;
} PtrStack;

static void pstack_init(PtrStack *s) {
    s->capacity = 64;
    s->data = (void **)malloc(sizeof(void *) * s->capacity);
    s->count = 0;
}
static void pstack_push(PtrStack *s, void *v) {
    if (s->count >= s->capacity) {
        s->capacity *= 2;
        s->data = (void **)realloc(s->data, sizeof(void *) * s->capacity);
    }
    s->data[s->count++] = v;
}
static void pstack_free(PtrStack *s) { free(s->data); }

/* The one and only LR driver loop. `hooks` may be NULL. */
static ParseOutcome parser_core(const Grammar *g, const ParsingTable *pt,
                                const int *input_tokens, int input_len,
                                const ParserHooks *hooks, int verbose) {
    IntStack states, symbols;
    PtrStack values;
    stack_init(&states);
    stack_init(&symbols);
    pstack_init(&values);
    stack_push(&states, 0);

    /* Build the input buffer with an explicit trailing $. */
    int *buffer = (int *)malloc(sizeof(int) * (input_len + 1));
    for (int i = 0; i < input_len; i++) buffer[i] = input_tokens[i];
    buffer[input_len] = g->end_symbol;
    int ip = 0;
    int buf_len = input_len + 1;

    ParseOutcome out;
    memset(&out, 0, sizeof(out));
    out.result = PARSE_ERROR;
    out.error_index = -1;
    out.error_state = -1;
    out.error_symbol = -1;

    if (verbose) {
        printf("%-30s %-30s %-20s %s\n", "STATE STACK", "SYMBOL STACK", "INPUT", "ACTION");
    }

    while (1) {
        int state = stack_top(&states);
        int lookahead = buffer[ip];
        ParsingAction action = pt->action[state][lookahead];

        if (verbose) {
            char state_buf[256] = {0}, sym_buf[256] = {0}, in_buf[256] = {0};
            char tmp[32];
            for (int i = 0; i < states.count; i++) { snprintf(tmp, sizeof(tmp), "%d ", states.data[i]); strncat(state_buf, tmp, sizeof(state_buf)-strlen(state_buf)-1); }
            for (int i = 0; i < symbols.count; i++) { snprintf(tmp, sizeof(tmp), "%s ", grammar_symbol_name(g, symbols.data[i])); strncat(sym_buf, tmp, sizeof(sym_buf)-strlen(sym_buf)-1); }
            for (int i = ip; i < buf_len; i++) { snprintf(tmp, sizeof(tmp), "%s ", grammar_symbol_name(g, buffer[i])); strncat(in_buf, tmp, sizeof(in_buf)-strlen(in_buf)-1); }
            printf("%-30s %-30s %-20s ", state_buf, sym_buf, in_buf);
        }

        if (action.type == ACTION_SHIFT) {
            if (verbose) printf("shift to I%d\n", action.value);
            stack_push(&symbols, lookahead);
            stack_push(&states, action.value);
            pstack_push(&values, (hooks && hooks->shift) ? hooks->shift(hooks->userdata, ip) : NULL);
            ip++;
        } else if (action.type == ACTION_REDUCE) {
            const Production *p = &g->productions[action.value];
            if (verbose) {
                printf("reduce by ");
                production_print(g, p);
                printf("\n");
            }

            /* Semantic action: hand the RHS values to the client, which
               returns the value of the left-hand side. */
            void *result_value = NULL;
            if (hooks && hooks->reduce) {
                void **children = (p->rhs_len > 0) ? &values.data[values.count - p->rhs_len] : NULL;
                result_value = hooks->reduce(hooks->userdata, p->id, children, p->rhs_len);
            }
            for (int i = 0; i < p->rhs_len; i++) {
                stack_pop(&states);
                stack_pop(&symbols);
                values.count--;
            }
            int prev_state = stack_top(&states);
            int goto_state = pt->go_to[prev_state][p->lhs];
            if (goto_state == -1) {
                if (hooks && hooks->discard && result_value) hooks->discard(hooks->userdata, p->lhs, result_value);
                out.result = PARSE_ERROR;
                out.internal_error = 1;
                out.error_state = prev_state;
                out.error_symbol = p->lhs;
                out.error_index = ip;
                break;
            }
            stack_push(&symbols, p->lhs);
            stack_push(&states, goto_state);
            pstack_push(&values, result_value);
        } else if (action.type == ACTION_ACCEPT) {
            if (verbose) printf("accept\n");
            out.result = PARSE_ACCEPT;
            /* The start symbol's value is the only one left on the stack. */
            if (values.count > 0) {
                out.value = values.data[values.count - 1];
                values.count--;
                symbols.count--;
            }
            break;
        } else {
            if (verbose) printf("ERROR\n");
            out.result = PARSE_ERROR;
            out.error_state = state;
            out.error_symbol = lookahead;
            out.error_index = ip;
            break;
        }
    }

    /* Give the client a chance to free any partial results. */
    if (hooks && hooks->discard) {
        for (int i = 0; i < values.count; i++) {
            if (values.data[i]) hooks->discard(hooks->userdata, symbols.data[i], values.data[i]);
        }
    }

    free(buffer);
    stack_free(&states);
    stack_free(&symbols);
    pstack_free(&values);
    return out;
}

ParseResult parser_run(const Grammar *g, const ParsingTable *pt,
                        const int *input_tokens, int input_len, int verbose) {
    ParseOutcome o = parser_core(g, pt, input_tokens, input_len, NULL, verbose);
    if (o.result == PARSE_ERROR) {
        if (o.internal_error) {
            fprintf(stderr, "parser_run: missing GOTO entry for state %d on %s\n",
                    o.error_state, grammar_symbol_name(g, o.error_symbol));
        } else {
            fprintf(stderr, "parser_run: syntax error in state %d on lookahead '%s' (input position %d)\n",
                    o.error_state, grammar_symbol_name(g, o.error_symbol), o.error_index);
        }
    }
    return o.result;
}

ParseOutcome parser_run_hooks(const Grammar *g, const ParsingTable *pt,
                              const int *input_tokens, int input_len,
                              const ParserHooks *hooks, int verbose) {
    return parser_core(g, pt, input_tokens, input_len, hooks, verbose);
}

int parser_expected_symbols(const Grammar *g, const ParsingTable *pt,
                            int state, int *out, int max) {
    int n = 0;
    for (int s = 0; s < pt->num_symbols && n < max; s++) {
        if (!(grammar_is_terminal(g, s) || grammar_is_end(g, s))) continue;
        if (pt->action[state][s].type != ACTION_NONE) out[n++] = s;
    }
    return n;
}
