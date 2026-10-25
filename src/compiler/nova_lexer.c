/*
 * nova_lexer.c - PHASE 1: lexical analysis for Leo.
 *
 * A hand-written scanner using longest-match rules:
 *   - two-character operators (<-  <=  >=  ==  !=) win over their
 *     one-character prefixes (<  >  =  !)
 *   - identifiers/keywords are scanned as one maximal [A-Za-z_][A-Za-z0-9_]*
 *     word and looked up in the (case-sensitive) keyword table
 *   - numbers are scanned as one maximal digit run (optionally with one
 *     fractional part); a number that runs straight into letters or a
 *     second '.' is reported as malformed instead of being split silently
 * String literals ("...") exist only for PRINT and INPUT prompts; the escapes
 * \n \t \\ \" are decoded here and the token keeps the decoded text.
 * Comments start with '#' and run to the end of the line.
 */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "nova_lexer.h"
#include "nova_util.h"
#include "xalloc.h"

typedef struct {
    TokenKind kind;
    const char *grammar_name;  /* terminal name used in language/grammar.txt */
    const char *name;          /* name printed by --tokens                   */
    const char *describe;      /* wording used in diagnostics                */
} KindInfo;

static const KindInfo kind_info[TK_KIND_COUNT] = {
    [TK_EOF]      = { TK_EOF,      "$",       "EOF",      "end of file" },
    [TK_ID]       = { TK_ID,       "id",      "ID",       "identifier" },
    [TK_INTLIT]   = { TK_INTLIT,   "intlit",  "INTLIT",   "integer literal" },
    [TK_FLOATLIT] = { TK_FLOATLIT, "floatlit","FLOATLIT", "float literal" },
    [TK_STRING]   = { TK_STRING,   "strlit",  "STRING",   "string literal" },
    [TK_TRUE]     = { TK_TRUE,     "TRUE",    "TRUE",     "'TRUE'" },
    [TK_FALSE]    = { TK_FALSE,    "FALSE",   "FALSE",    "'FALSE'" },
    [TK_IF]       = { TK_IF,       "IF",      "IF",       "'IF'" },
    [TK_THEN]     = { TK_THEN,     "THEN",    "THEN",     "'THEN'" },
    [TK_ELSE]     = { TK_ELSE,     "ELSE",    "ELSE",     "'ELSE'" },
    [TK_END]      = { TK_END,      "END",     "END",      "'END'" },
    [TK_WHILE]    = { TK_WHILE,    "WHILE",   "WHILE",    "'WHILE'" },
    [TK_DO]       = { TK_DO,       "DO",      "DO",       "'DO'" },
    [TK_REPEAT]   = { TK_REPEAT,   "REPEAT",  "REPEAT",   "'REPEAT'" },
    [TK_UNTIL]    = { TK_UNTIL,    "UNTIL",   "UNTIL",    "'UNTIL'" },
    [TK_FOR]      = { TK_FOR,      "FOR",     "FOR",      "'FOR'" },
    [TK_TO]       = { TK_TO,       "TO",      "TO",       "'TO'" },
    [TK_STEP]     = { TK_STEP,     "STEP",    "STEP",     "'STEP'" },
    [TK_INT]      = { TK_INT,      "INT",     "INT",      "'INT'" },
    [TK_FLOAT]    = { TK_FLOAT,    "FLOAT",   "FLOAT",    "'FLOAT'" },
    [TK_BOOL]     = { TK_BOOL,     "BOOL",    "BOOL",     "'BOOL'" },
    [TK_INPUT]    = { TK_INPUT,    "INPUT",   "INPUT",    "'INPUT'" },
    [TK_PRINT]    = { TK_PRINT,    "PRINT",   "PRINT",    "'PRINT'" },
    [TK_PRINT_NL] = { TK_PRINT_NL, "PRINTN", "PRINT_NL", "'PRINTN'" },
    [TK_PRINT_NL_EMPTY] = { TK_PRINT_NL_EMPTY, "PRINTN_EMPTY", "PRINTN_EMPTY", "'PRINTN' at end of line" },
    [TK_AND]      = { TK_AND,      "AND",     "AND",      "'AND'" },
    [TK_OR]       = { TK_OR,       "OR",      "OR",       "'OR'" },
    [TK_NOT]      = { TK_NOT,      "NOT",     "NOT",      "'NOT'" },
    [TK_MOD]      = { TK_MOD,      "MOD",     "MOD",      "'MOD'" },
    [TK_ASSIGN]   = { TK_ASSIGN,   "<-",      "ASSIGN",   "'<-'" },
    [TK_PLUS]     = { TK_PLUS,     "+",       "PLUS",     "'+'" },
    [TK_TILDE]    = { TK_TILDE,    "~",       "TILDE",    "'~'" },
    [TK_STAR]     = { TK_STAR,     "*",       "STAR",     "'*'" },
    [TK_SLASH]    = { TK_SLASH,    "/",       "SLASH",    "'/'" },
    [TK_CARET]    = { TK_CARET,    "^",       "CARET",    "'^'" },
    [TK_EQ]       = { TK_EQ,       "==",      "EQ",       "'=='" },
    [TK_NE]       = { TK_NE,       "!=",      "NE",       "'!='" },
    [TK_LT]       = { TK_LT,       "<",       "LT",       "'<'" },
    [TK_GT]       = { TK_GT,       ">",       "GT",       "'>'" },
    [TK_LE]       = { TK_LE,       "<=",      "LE",       "'<='" },
    [TK_GE]       = { TK_GE,       ">=",      "GE",       "'>='" },
    [TK_LPAREN]   = { TK_LPAREN,   "(",       "LPAREN",   "'('" },
    [TK_RPAREN]   = { TK_RPAREN,   ")",       "RPAREN",   "')'" },
    [TK_COMMA]    = { TK_COMMA,    ",",       "COMMA",    "','" },
};

const char *token_kind_grammar_name(TokenKind k) { return kind_info[k].grammar_name; }
const char *token_kind_name(TokenKind k)         { return kind_info[k].name; }
const char *token_kind_describe(TokenKind k)     { return kind_info[k].describe; }

/* Keywords are case sensitive: "if" is an identifier, "IF" is a keyword. */
static const struct { const char *text; TokenKind kind; } keywords[] = {
    {"IF", TK_IF}, {"THEN", TK_THEN}, {"ELSE", TK_ELSE}, {"END", TK_END},
    {"FOR", TK_FOR}, {"TO", TK_TO}, {"STEP", TK_STEP},
    {"WHILE", TK_WHILE}, {"DO", TK_DO}, {"REPEAT", TK_REPEAT}, {"UNTIL", TK_UNTIL},
    {"INT", TK_INT}, {"FLOAT", TK_FLOAT}, {"BOOL", TK_BOOL},
    {"INPUT", TK_INPUT}, {"PRINT", TK_PRINT}, {"PRINTN", TK_PRINT_NL},
    {"AND", TK_AND}, {"OR", TK_OR}, {"NOT", TK_NOT}, {"MOD", TK_MOD},
    {"TRUE", TK_TRUE}, {"FALSE", TK_FALSE},
};

void token_list_init(TokenList *t) { memset(t, 0, sizeof(*t)); }

void token_list_free(TokenList *t) {
    for (int i = 0; i < t->count; i++) free(t->items[i].lexeme);
    free(t->items);
    memset(t, 0, sizeof(*t));
}

static Token *push_token(TokenList *t, TokenKind kind, const char *start, size_t len,
                         int line, int col) {
    NOVA_GROW(t->items, t->count, t->capacity);
    Token *tk = &t->items[t->count++];
    memset(tk, 0, sizeof(*tk));
    tk->kind = kind;
    tk->lexeme = xstrndup(start, len);
    tk->line = line;
    tk->col = col;
    return tk;
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_char(int c)  { return isalnum(c) || c == '_'; }

/* Printable rendering of a (possibly non-ASCII) offending byte. */
static void describe_char(unsigned char c, char *buf, size_t n) {
    if (c >= 32 && c < 127) snprintf(buf, n, "'%c'", c);
    else snprintf(buf, n, "byte 0x%02X", c);
}

int nova_lex(const char *src, TokenList *out, DiagList *diags) {
    int errors = 0;
    int line = 1, col = 1;
    const char *p = src;

#define ADVANCE(n) do { p += (n); col += (int)(n); } while (0)

    while (*p) {
        unsigned char c = (unsigned char)*p;

        /* ---- whitespace ---- */
        if (c == '\n') { p++; line++; col = 1; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { ADVANCE(1); continue; }

        /* ---- comment: '#' to end of line ---- */
        if (c == '#') {
            while (*p && *p != '\n') ADVANCE(1);
            continue;
        }

        int tline = line, tcol = col;

        /* ---- identifiers and keywords ---- */
        if (is_ident_start(c)) {
            const char *start = p;
            while (is_ident_char((unsigned char)*p)) ADVANCE(1);
            size_t len = (size_t)(p - start);
            TokenKind kind = TK_ID;
            for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
                if (strlen(keywords[i].text) == len && strncmp(start, keywords[i].text, len) == 0) {
                    kind = keywords[i].kind;
                    break;
                }
            }
            if (kind == TK_PRINT_NL) {
                const char *look = p;
                while (*look == ' ' || *look == '\t' || *look == '\r' || *look == '\f' || *look == '\v') look++;
                if (*look == '#' || *look == '\n' || *look == '\0') kind = TK_PRINT_NL_EMPTY;
            }
            push_token(out, kind, start, len, tline, tcol);
            continue;
        }

        /* ---- numbers ---- */
        if (isdigit(c)) {
            const char *start = p;
            int is_float = 0, malformed = 0;
            while (isdigit((unsigned char)*p)) ADVANCE(1);
            if (*p == '.') {
                if (isdigit((unsigned char)p[1])) {
                    is_float = 1;
                    ADVANCE(1);
                    while (isdigit((unsigned char)*p)) ADVANCE(1);
                } else {
                    malformed = 1;  /* "12." : digits are required after the point */
                }
            }
            /* A number glued to letters, '_' or another '.' (12abc, 1.2.3, 1e5). */
            if (is_ident_char((unsigned char)*p) || *p == '.') malformed = 1;
            if (malformed) {
                while (is_ident_char((unsigned char)*p) || *p == '.') ADVANCE(1);
                char *text = xstrndup(start, (size_t)(p - start));
                diag_add(diags, DIAG_ERROR, "lexical", tline, tcol,
                         "malformed numeric literal '%s'", text);
                free(text);
                errors++;
                continue;
            }
            size_t len = (size_t)(p - start);
            Token *tk = push_token(out, is_float ? TK_FLOATLIT : TK_INTLIT, start, len, tline, tcol);
            errno = 0;
            if (is_float) {
                tk->fval = strtod(tk->lexeme, NULL);
                if (errno == ERANGE || !isfinite(tk->fval)) {
                    diag_add(diags, DIAG_ERROR, "lexical", tline, tcol,
                             "float literal '%s' is out of range", tk->lexeme);
                    errors++;
                }
            } else {
                tk->ival = strtoll(tk->lexeme, NULL, 10);
                if (errno == ERANGE) {
                    diag_add(diags, DIAG_ERROR, "lexical", tline, tcol,
                             "integer literal '%s' is out of range (max 9223372036854775807)",
                             tk->lexeme);
                    errors++;
                }
            }
            continue;
        }

        /* ---- string literals ---- */
        if (c == '"') {
            ADVANCE(1);
            size_t cap = 16, len = 0;
            char *buf = xmalloc(cap);
            int closed = 0, bad = 0;
            while (*p && *p != '\n') {
                if (*p == '"') { ADVANCE(1); closed = 1; break; }
                char ch = *p;
                if (ch == '\\') {
                    char e = p[1];
                    if (e == 'n') ch = '\n';
                    else if (e == 't') ch = '\t';
                    else if (e == '\\') ch = '\\';
                    else if (e == '"') ch = '"';
                    else {
                        if (!bad) diag_add(diags, DIAG_ERROR, "lexical", line, col,
                                           "unknown escape sequence in string literal (use \\n, \\t, \\\\ or \\\")");
                        bad = 1;
                        ADVANCE(1);
                        continue;
                    }
                    ADVANCE(1);
                }
                if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
                buf[len++] = ch;
                ADVANCE(1);
            }
            if (!closed) {
                diag_add(diags, DIAG_ERROR, "lexical", tline, tcol, "unterminated string literal (a string must end with '\"' on the same line)");
                errors++;
            } else if (bad) {
                errors++;
            } else {
                push_token(out, TK_STRING, buf, len, tline, tcol);
            }
            free(buf);
            continue;
        }

        /* ---- operators: longest match first ---- */
        char n1 = p[1];
        TokenKind kind = TK_EOF;
        size_t len = 0;
        switch (c) {
            case '<': if (n1 == '-') { kind = TK_ASSIGN; len = 2; }
                      else if (n1 == '=') { kind = TK_LE; len = 2; }
                      else { kind = TK_LT; len = 1; }
                      break;
            case '>': if (n1 == '=') { kind = TK_GE; len = 2; } else { kind = TK_GT; len = 1; } break;
            case '=': if (n1 == '=') { kind = TK_EQ; len = 2; } break;
            case '!': if (n1 == '=') { kind = TK_NE; len = 2; } break;
            case '+': kind = TK_PLUS;   len = 1; break;
            case '~': kind = TK_TILDE;  len = 1; break;
            case '*': kind = TK_STAR;   len = 1; break;
            case '/': kind = TK_SLASH;  len = 1; break;
            case '^': kind = TK_CARET;  len = 1; break;
            case '(': kind = TK_LPAREN; len = 1; break;
            case ')': kind = TK_RPAREN; len = 1; break;
            case ',': kind = TK_COMMA;  len = 1; break;
            default: break;
        }
        if (len > 0) {
            push_token(out, kind, p, len, tline, tcol);
            ADVANCE(len);
            continue;
        }

        /* ---- anything else is an invalid character ---- */
        char shown[32];
        describe_char(c, shown, sizeof(shown));
        const char *hint = "";
        switch (c) {
            case '=': hint = " (use '<-' for assignment or '==' to compare)"; break;
            case '!': hint = " (use '!=' for inequality or NOT for negation)"; break;
            case '-': hint = " (Leo uses '~' for subtraction and negation)"; break;
            case '.': if (isdigit((unsigned char)n1)) hint = " (a float literal needs digits before the '.')"; break;
            case '\'': hint = " (character literals are not supported; strings use double quotes and only appear in PRINT and INPUT)"; break;
            case ';': hint = " (statements are not terminated with ';')"; break;
            default: break;
        }
        diag_add(diags, DIAG_ERROR, "lexical", tline, tcol, "invalid character %s%s", shown, hint);
        errors++;
        ADVANCE(1);
    }

    push_token(out, TK_EOF, "", 0, line, col);
    return errors;
#undef ADVANCE
}

void token_list_print(const TokenList *t, FILE *out) {
    fprintf(out, "%-8s %-10s %s\n", "LINE:COL", "KIND", "LEXEME");
    for (int i = 0; i < t->count; i++) {
        const Token *tk = &t->items[i];
        char pos[24];
        snprintf(pos, sizeof(pos), "%d:%d", tk->line, tk->col);
        fprintf(out, "%-8s %-10s ", pos, token_kind_name(tk->kind));
        if (tk->kind == TK_EOF) fputs("<eof>", out);
        else if (tk->kind == TK_STRING) {
            StrBuf q; strbuf_init(&q);
            strbuf_append_quoted(&q, tk->lexeme, 0);
            fputs(q.data, out);
            strbuf_free(&q);
        } else fputs(tk->lexeme, out);
        fputc('\n', out);
    }
}
