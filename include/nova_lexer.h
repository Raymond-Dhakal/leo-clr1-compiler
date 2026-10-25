/*
 * nova_lexer.h
 * Part of the Leo compiler extension - PHASE 1: lexical analysis.
 *
 * Turns Leo source text into a list of Token records that carry the
 * lexeme, numeric value and source position. This is distinct from the
 * generator's lexer.c (which tokenizes test strings made of grammar
 * terminals): a Token's `kind` is a SOURCE-LANGUAGE token kind, never a
 * grammar symbol id. The mapping kind -> grammar terminal is done once, by
 * name, in nova_parse.c.
 */
#ifndef NOVA_LEXER_H
#define NOVA_LEXER_H

#include "nova_diag.h"

typedef enum {
    TK_EOF = 0,
    /* literals and names */
    TK_ID, TK_INTLIT, TK_FLOATLIT, TK_STRING, TK_TRUE, TK_FALSE,
    /* keywords */
    TK_IF, TK_THEN, TK_ELSE, TK_END, TK_WHILE, TK_DO, TK_REPEAT, TK_UNTIL,
    TK_FOR, TK_TO, TK_STEP,
    TK_INT, TK_FLOAT, TK_BOOL, TK_INPUT, TK_PRINT, TK_PRINT_NL, TK_PRINT_NL_EMPTY, TK_AND, TK_OR, TK_NOT, TK_MOD,
    /* operators and delimiters */
    TK_ASSIGN,   /* <-  */
    TK_PLUS,     /* +   */
    TK_TILDE,    /* ~   subtraction (binary) / negation (unary) */
    TK_STAR,     /* *   */
    TK_SLASH,    /* /   */
    TK_CARET,    /* ^   */
    TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_LPAREN, TK_RPAREN, TK_COMMA,
    TK_KIND_COUNT
} TokenKind;

typedef struct {
    TokenKind kind;
    char *lexeme;       /* owned copy of the source text of the token; for TK_STRING the
                           decoded text between the quotes (escapes resolved)    */
    int line, col;      /* 1-based position of the first character    */
    long long ival;     /* value of TK_INTLIT                          */
    double fval;        /* value of TK_FLOATLIT                        */
} Token;

typedef struct {
    Token *items;
    int count, capacity;
} TokenList;

/*
 * Tokenizes `source`. The list always ends with a TK_EOF token. Lexical
 * errors (invalid characters, malformed or out-of-range numbers) are added
 * to `diags` and scanning continues so several errors can be reported at
 * once. Returns the number of lexical errors found.
 */
int nova_lex(const char *source, TokenList *out, DiagList *diags);

void token_list_init(TokenList *t);
void token_list_free(TokenList *t);
void token_list_print(const TokenList *t, FILE *out);

/* Name of the grammar terminal that corresponds to a token kind
   (e.g. TK_ASSIGN -> "<-", TK_ID -> "id"). Used to bind to grammar.txt. */
const char *token_kind_grammar_name(TokenKind k);
/* Short upper-case name for --tokens listings (ASSIGN, PLUS, ID ...). */
const char *token_kind_name(TokenKind k);
/* Human readable description for error messages ("identifier", "'<-'"). */
const char *token_kind_describe(TokenKind k);

#endif /* NOVA_LEXER_H */
