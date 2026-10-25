/*
 * nova_parse.c - PHASE 2: syntax analysis with the CLR(1) generator core.
 *
 * Nothing here is a parser: the shift/reduce decisions are made by the
 * original table-driven driver (parser.c) using the canonical LR(1) tables
 * that the original modules build from language/grammar.txt. This file
 * only supplies
 *   - the terminal binding: lexer TokenKind -> grammar terminal id, and
 *   - the semantic actions: what AST node each production builds.
 *
 * Semantic actions are bound to productions by their TEXT (for example
 * "AddExpr -> AddExpr + MulExpr"), not by production number, so editing or
 * reordering grammar.txt cannot silently attach an action to the wrong
 * rule. nova_frontend_build() verifies that every production has exactly
 * one action and every action is used.
 */
#include <stdint.h>
#include <string.h>
#include "nova_parse.h"
#include "xalloc.h"

typedef enum {
    A_NONE = 0,
    A_PROGRAM, A_LIST_EMPTY, A_LIST_APPEND,
    A_DECL, A_DECL_INIT, A_ASSIGN,
    A_IF, A_IF_ELSE, A_WHILE, A_REPEAT, A_INPUT, A_INPUT_PROMPT, A_PRINT, A_PRINT_NL, A_PRINT_NL_EMPTY,
    A_FOR, A_FORHEAD_PAREN, A_FORSPEC, A_FORSPEC_STEP, A_FORVAR_ID, A_FORVAR_TYPE,
    A_PRINT_FIRST, A_PRINT_APPEND, A_STRLIT,
    A_TYPE_INT, A_TYPE_FLOAT, A_TYPE_BOOL,
    A_PASS,      /* A -> B : value of B is the value of A           */
    A_BINARY,    /* A -> A op B                                    */
    A_UNARY,     /* A -> op A                                      */
    A_PAREN,     /* Primary -> ( Expr )                            */
    A_VAR, A_INTLIT, A_FLOATLIT, A_TRUE, A_FALSE,
    A_COUNT
} ActionId;

static const struct { const char *signature; ActionId action; } bindings[] = {
    {"Program -> StmtList",                                   A_PROGRAM},
    {"StmtList -> EPSILON",                                   A_LIST_EMPTY},
    {"StmtList -> StmtList Stmt",                             A_LIST_APPEND},
    {"Stmt -> Type id",                                       A_DECL},
    {"Stmt -> Type id <- Expr",                               A_DECL_INIT},
    {"Stmt -> id <- Expr",                                    A_ASSIGN},
    {"Stmt -> IF ( Expr ) THEN StmtList END",                 A_IF},
    {"Stmt -> IF ( Expr ) THEN StmtList ELSE StmtList END",   A_IF_ELSE},
    {"Stmt -> WHILE ( Expr ) DO StmtList END",                A_WHILE},
    {"Stmt -> REPEAT StmtList UNTIL ( Expr )",                A_REPEAT},
    {"Stmt -> FOR ForHead DO StmtList END",                   A_FOR},
    {"Stmt -> INPUT id",                                      A_INPUT},
    {"Stmt -> INPUT strlit , id",                             A_INPUT_PROMPT},
    {"Stmt -> PRINT PrintList",                               A_PRINT},
    {"Stmt -> PRINTN PrintList",                              A_PRINT_NL},
    {"Stmt -> PRINTN_EMPTY",                                   A_PRINT_NL_EMPTY},
    {"ForHead -> ( ForSpec )",                                A_FORHEAD_PAREN},
    {"ForHead -> ForSpec",                                    A_PASS},
    {"ForSpec -> ForVar <- Expr TO Expr",                     A_FORSPEC},
    {"ForSpec -> ForVar <- Expr TO Expr STEP Expr",           A_FORSPEC_STEP},
    {"ForVar -> id",                                          A_FORVAR_ID},
    {"ForVar -> Type id",                                     A_FORVAR_TYPE},
    {"PrintList -> PrintItem",                                A_PRINT_FIRST},
    {"PrintList -> PrintList , PrintItem",                    A_PRINT_APPEND},
    {"PrintItem -> Expr",                                     A_PASS},
    {"PrintItem -> strlit",                                   A_STRLIT},
    {"Type -> INT",                                           A_TYPE_INT},
    {"Type -> FLOAT",                                         A_TYPE_FLOAT},
    {"Type -> BOOL",                                          A_TYPE_BOOL},
    {"Expr -> OrExpr",                                        A_PASS},
    {"OrExpr -> OrExpr OR AndExpr",                           A_BINARY},
    {"OrExpr -> AndExpr",                                     A_PASS},
    {"AndExpr -> AndExpr AND EqExpr",                         A_BINARY},
    {"AndExpr -> EqExpr",                                     A_PASS},
    {"EqExpr -> EqExpr == RelExpr",                           A_BINARY},
    {"EqExpr -> EqExpr != RelExpr",                           A_BINARY},
    {"EqExpr -> RelExpr",                                     A_PASS},
    {"RelExpr -> RelExpr < AddExpr",                          A_BINARY},
    {"RelExpr -> RelExpr > AddExpr",                          A_BINARY},
    {"RelExpr -> RelExpr <= AddExpr",                         A_BINARY},
    {"RelExpr -> RelExpr >= AddExpr",                         A_BINARY},
    {"RelExpr -> AddExpr",                                    A_PASS},
    {"AddExpr -> AddExpr + MulExpr",                          A_BINARY},
    {"AddExpr -> AddExpr ~ MulExpr",                          A_BINARY},
    {"AddExpr -> MulExpr",                                    A_PASS},
    {"MulExpr -> MulExpr * PowExpr",                          A_BINARY},
    {"MulExpr -> MulExpr / PowExpr",                          A_BINARY},
    {"MulExpr -> MulExpr MOD PowExpr",                        A_BINARY},
    {"MulExpr -> PowExpr",                                    A_PASS},
    {"PowExpr -> Unary ^ PowExpr",                            A_BINARY},
    {"PowExpr -> Unary",                                      A_PASS},
    {"Unary -> NOT Unary",                                    A_UNARY},
    {"Unary -> ~ Unary",                                      A_UNARY},
    {"Unary -> Primary",                                      A_PASS},
    {"Primary -> ( Expr )",                                   A_PAREN},
    {"Primary -> id",                                         A_VAR},
    {"Primary -> intlit",                                     A_INTLIT},
    {"Primary -> floatlit",                                   A_FLOATLIT},
    {"Primary -> TRUE",                                       A_TRUE},
    {"Primary -> FALSE",                                      A_FALSE},
};
#define BINDING_COUNT ((int)(sizeof(bindings) / sizeof(bindings[0])))

/* "LHS -> X Y Z" (or "LHS -> EPSILON") for production `p`. */
static void production_signature(const Grammar *g, const Production *p, char *buf, size_t n) {
    snprintf(buf, n, "%s ->", grammar_symbol_name(g, p->lhs));
    if (p->rhs_len == 0) {
        strncat(buf, " EPSILON", n - strlen(buf) - 1);
    } else {
        for (int i = 0; i < p->rhs_len; i++) {
            strncat(buf, " ", n - strlen(buf) - 1);
            strncat(buf, grammar_symbol_name(g, p->rhs[i]), n - strlen(buf) - 1);
        }
    }
}

LeoFrontend *nova_frontend_build(const char *grammar_path, char *errbuf, size_t errlen) {
    LeoFrontend *fe = (LeoFrontend *)xcalloc(1, sizeof(LeoFrontend));
    fe->type_symbol = -1;

    /* ---- the original generator pipeline, unchanged ---- */
    fe->grammar = grammar_create();
    if (grammar_load_from_file(fe->grammar, grammar_path) != 0) {
        snprintf(errbuf, errlen, "cannot load grammar '%s'", grammar_path);
        nova_frontend_free(fe);
        return NULL;
    }
    grammar_augment(fe->grammar);
    fe->first = first_sets_compute(fe->grammar);
    fe->canonical = canonical_build(fe->grammar, fe->first);
    fe->conflicts = conflict_list_create();
    fe->table = parsing_table_build(fe->grammar, fe->canonical, fe->conflicts);

    const Grammar *g = fe->grammar;

    /* ---- bind lexer token kinds to grammar terminals (by name) ---- */
    fe->symbol_of_kind = (int *)xmalloc(sizeof(int) * TK_KIND_COUNT);
    fe->kind_of_symbol = (int *)xmalloc(sizeof(int) * g->symbol_count);
    for (int i = 0; i < g->symbol_count; i++) fe->kind_of_symbol[i] = -1;
    for (int k = 0; k < TK_KIND_COUNT; k++) {
        fe->symbol_of_kind[k] = -1;
        if (k == TK_EOF) continue;
        int sym = grammar_find_symbol(g, token_kind_grammar_name((TokenKind)k));
        if (sym != -1 && grammar_is_terminal(g, sym)) {
            fe->symbol_of_kind[k] = sym;
            fe->kind_of_symbol[sym] = k;
        }
    }
    for (int s = 0; s < g->symbol_count; s++) {
        if (grammar_is_terminal(g, s) && fe->kind_of_symbol[s] == -1) {
            snprintf(errbuf, errlen, "grammar terminal '%s' does not correspond to any lexer token",
                     grammar_symbol_name(g, s));
            nova_frontend_free(fe);
            return NULL;
        }
    }
    fe->type_symbol = grammar_find_symbol(g, "Type");

    /* ---- bind semantic actions to productions (by text) ---- */
    fe->action_of_prod = (int *)xcalloc((size_t)g->production_count, sizeof(int));
    int used[BINDING_COUNT];
    memset(used, 0, sizeof(used));
    for (int p = 0; p < g->production_count; p++) {
        if (p == g->augmented_production) continue;
        char sig[512];
        production_signature(g, &g->productions[p], sig, sizeof(sig));
        int found = -1;
        for (int b = 0; b < BINDING_COUNT; b++) {
            if (strcmp(bindings[b].signature, sig) == 0) { found = b; break; }
        }
        if (found == -1) {
            snprintf(errbuf, errlen, "grammar production '%s' has no semantic action (see nova_parse.c)", sig);
            nova_frontend_free(fe);
            return NULL;
        }
        used[found] = 1;
        fe->action_of_prod[p] = bindings[found].action;
    }
    for (int b = 0; b < BINDING_COUNT; b++) {
        if (!used[b]) {
            snprintf(errbuf, errlen, "semantic action for '%s' matches no production in the grammar",
                     bindings[b].signature);
            nova_frontend_free(fe);
            return NULL;
        }
    }
    return fe;
}

void nova_frontend_free(LeoFrontend *fe) {
    if (!fe) return;
    parsing_table_destroy(fe->table);
    conflict_list_destroy(fe->conflicts);
    canonical_destroy(fe->canonical);
    first_sets_destroy(fe->first);
    grammar_destroy(fe->grammar);
    free(fe->symbol_of_kind);
    free(fe->kind_of_symbol);
    free(fe->action_of_prod);
    free(fe);
}

/* ---------------------------------------------------------------------
 * Semantic actions (called by parser_run_hooks on every reduction)
 * --------------------------------------------------------------------- */

typedef struct {
    const LeoFrontend *fe;
    const TokenList *tokens;
} BuildCtx;

#define TOK(i)  ((const Token *)c[i])
#define NODE(i) ((AstNode *)c[i])

static BinOp binop_of_token(TokenKind k) {
    switch (k) {
        case TK_PLUS: return OP_ADD;  case TK_TILDE: return OP_SUB;
        case TK_STAR: return OP_MUL;  case TK_SLASH: return OP_DIV;
        case TK_MOD:  return OP_MOD;  case TK_CARET: return OP_POW;
        case TK_LT:   return OP_LT;   case TK_GT:    return OP_GT;
        case TK_LE:   return OP_LE;   case TK_GE:    return OP_GE;
        case TK_EQ:   return OP_EQ;   case TK_NE:    return OP_NE;
        case TK_AND:  return OP_AND;  default:       return OP_OR;
    }
}

static void *hook_shift(void *ud, int token_index) {
    BuildCtx *ctx = (BuildCtx *)ud;
    return (void *)&ctx->tokens->items[token_index];
}

static void *hook_reduce(void *ud, int prod, void **c, int n) {
    const BuildCtx *ctx = (const BuildCtx *)ud;
    (void)n;
    AstNode *node;
    switch ((ActionId)ctx->fe->action_of_prod[prod]) {
        case A_PROGRAM:
            NODE(0)->kind = AST_PROGRAM;
            return NODE(0);
        case A_LIST_EMPTY:
            return ast_new(AST_BLOCK, 0, 0);
        case A_LIST_APPEND:
            ast_block_append(NODE(0), NODE(1));
            return NODE(0);
        case A_DECL:        /* Type id */
        case A_DECL_INIT:   /* Type id <- Expr */
            node = ast_new(AST_DECL, TOK(1)->line, TOK(1)->col);
            node->u.decl.decl_type = (DataType)(intptr_t)c[0];
            node->u.decl.name = xstrdup(TOK(1)->lexeme);
            node->u.decl.sym = -1;
            if (ctx->fe->action_of_prod[prod] == A_DECL_INIT) node->u.decl.init = NODE(3);
            return node;
        case A_ASSIGN:      /* id <- Expr */
            node = ast_new(AST_ASSIGN, TOK(0)->line, TOK(0)->col);
            node->u.assign.name = xstrdup(TOK(0)->lexeme);
            node->u.assign.value = NODE(2);
            node->u.assign.sym = -1;
            return node;
        case A_IF:          /* IF ( Expr ) THEN StmtList END */
            node = ast_new(AST_IF, TOK(0)->line, TOK(0)->col);
            node->u.if_stmt.cond = NODE(2);
            node->u.if_stmt.then_block = NODE(5);
            return node;
        case A_IF_ELSE:     /* IF ( Expr ) THEN StmtList ELSE StmtList END */
            node = ast_new(AST_IF, TOK(0)->line, TOK(0)->col);
            node->u.if_stmt.cond = NODE(2);
            node->u.if_stmt.then_block = NODE(5);
            node->u.if_stmt.else_block = NODE(7);
            return node;
        case A_WHILE:       /* WHILE ( Expr ) DO StmtList END */
            node = ast_new(AST_WHILE, TOK(0)->line, TOK(0)->col);
            node->u.loop.cond = NODE(2);
            node->u.loop.body = NODE(5);
            return node;
        case A_REPEAT:      /* REPEAT StmtList UNTIL ( Expr ) */
            node = ast_new(AST_REPEAT, TOK(0)->line, TOK(0)->col);
            node->u.loop.body = NODE(1);
            node->u.loop.cond = NODE(4);
            return node;
        case A_INPUT:       /* INPUT id */
            node = ast_new(AST_INPUT, TOK(1)->line, TOK(1)->col);
            node->u.input.name = xstrdup(TOK(1)->lexeme);
            node->u.input.sym = -1;
            return node;
        case A_INPUT_PROMPT: /* INPUT strlit , id */
            node = ast_new(AST_INPUT, TOK(0)->line, TOK(0)->col);
            node->u.input.prompt = xstrdup(TOK(1)->lexeme);
            node->u.input.name = xstrdup(TOK(3)->lexeme);
            node->u.input.sym = -1;
            return node;
        case A_PRINT:       /* PRINT PrintList : the list node becomes the statement */
        case A_PRINT_NL:    /* PRINTN PrintList */
            node = NODE(1);
            node->line = TOK(0)->line;
            node->col = TOK(0)->col;
            node->print_newline = ctx->fe->action_of_prod[prod] == A_PRINT_NL;
            return node;
        case A_PRINT_NL_EMPTY:
            node = ast_new(AST_PRINT, TOK(0)->line, TOK(0)->col);
            node->print_newline = 1;
            return node;
        case A_PRINT_FIRST: /* PrintList -> PrintItem */
            node = ast_new(AST_PRINT, 0, 0);
            ast_block_append(node, NODE(0));
            return node;
        case A_PRINT_APPEND: /* PrintList -> PrintList , PrintItem */
            ast_block_append(NODE(0), NODE(2));
            return NODE(0);
        case A_STRLIT:
            node = ast_new(AST_STR_LIT, TOK(0)->line, TOK(0)->col);
            node->u.str = xstrdup(TOK(0)->lexeme);
            return node;
        case A_FOR:         /* FOR ForHead DO StmtList END */
            node = NODE(1);          /* keeps the position of the loop variable */
            node->u.for_stmt.body = NODE(3);
            return node;
        case A_FORHEAD_PAREN: /* ( ForSpec ) */
            return NODE(1);
        case A_FORSPEC:     /* ForVar <- Expr TO Expr */
        case A_FORSPEC_STEP:/* ForVar <- Expr TO Expr STEP Expr */
            node = NODE(0);
            node->u.for_stmt.start = NODE(2);
            node->u.for_stmt.limit = NODE(4);
            if (ctx->fe->action_of_prod[prod] == A_FORSPEC_STEP) node->u.for_stmt.step = NODE(6);
            return node;
        case A_FORVAR_ID:   /* id */
            node = ast_new(AST_FOR, TOK(0)->line, TOK(0)->col);
            node->u.for_stmt.name = xstrdup(TOK(0)->lexeme);
            node->u.for_stmt.sym = -1;
            return node;
        case A_FORVAR_TYPE: /* Type id */
            node = ast_new(AST_FOR, TOK(1)->line, TOK(1)->col);
            node->u.for_stmt.has_decl = 1;
            node->u.for_stmt.decl_type = (DataType)(intptr_t)c[0];
            node->u.for_stmt.name = xstrdup(TOK(1)->lexeme);
            node->u.for_stmt.sym = -1;
            return node;
        case A_TYPE_INT:   return (void *)(intptr_t)TYPE_INT;
        case A_TYPE_FLOAT: return (void *)(intptr_t)TYPE_FLOAT;
        case A_TYPE_BOOL:  return (void *)(intptr_t)TYPE_BOOL;
        case A_PASS:
            return c[0];
        case A_BINARY:      /* left op right: position of the operator */
            node = ast_new(AST_BINARY, TOK(1)->line, TOK(1)->col);
            node->u.binary.op = binop_of_token(TOK(1)->kind);
            node->u.binary.left = NODE(0);
            node->u.binary.right = NODE(2);
            return node;
        case A_UNARY:       /* op operand */
            node = ast_new(AST_UNARY, TOK(0)->line, TOK(0)->col);
            node->u.unary.op = (TOK(0)->kind == TK_NOT) ? UOP_NOT : UOP_NEG;
            node->u.unary.operand = NODE(1);
            return node;
        case A_PAREN:
            return NODE(1);
        case A_VAR:
            node = ast_new(AST_VAR, TOK(0)->line, TOK(0)->col);
            node->u.var.name = xstrdup(TOK(0)->lexeme);
            node->u.var.sym = -1;
            return node;
        case A_INTLIT:
            node = ast_new(AST_INT_LIT, TOK(0)->line, TOK(0)->col);
            node->u.ival = TOK(0)->ival;
            return node;
        case A_FLOATLIT:
            node = ast_new(AST_FLOAT_LIT, TOK(0)->line, TOK(0)->col);
            node->u.fval = TOK(0)->fval;
            return node;
        case A_TRUE:
        case A_FALSE:
            node = ast_new(AST_BOOL_LIT, TOK(0)->line, TOK(0)->col);
            node->u.bval = (ctx->fe->action_of_prod[prod] == A_TRUE);
            return node;
        default:
            return NULL;
    }
}

/* Frees partial results when the parse stops with an error. */
static void hook_discard(void *ud, int symbol, void *value) {
    const BuildCtx *ctx = (const BuildCtx *)ud;
    const Grammar *g = ctx->fe->grammar;
    if (!grammar_is_nonterminal(g, symbol)) return;   /* terminals hold Token pointers (not owned) */
    if (symbol == ctx->fe->type_symbol) return;        /* Type holds a small integer, not a pointer */
    ast_free((AstNode *)value);
}

/* ---------------------------------------------------------------------
 * Driver
 * --------------------------------------------------------------------- */

static void report_syntax_error(const LeoFrontend *fe, const TokenList *tokens,
                                const ParseOutcome *o, DiagList *diags) {
    const Grammar *g = fe->grammar;
    const Token *tk = &tokens->items[o->error_index];

    char expected[512] = "";
    int ids[64];
    int n = parser_expected_symbols(g, fe->table, o->error_state, ids, 64);
    int shown = 0;
    for (int i = 0; i < n; i++) {
        const char *text;
        if (grammar_is_end(g, ids[i])) text = "end of file";
        else text = token_kind_describe((TokenKind)fe->kind_of_symbol[ids[i]]);
        if (shown == 8) { strncat(expected, ", ...", sizeof(expected) - strlen(expected) - 1); break; }
        if (shown > 0) strncat(expected, ", ", sizeof(expected) - strlen(expected) - 1);
        strncat(expected, text, sizeof(expected) - strlen(expected) - 1);
        shown++;
    }

    if (tk->kind == TK_EOF) {
        diag_add(diags, DIAG_ERROR, "syntax", tk->line, tk->col,
                 "unexpected end of file; expected %s", expected);
    } else {
        const char *what = token_kind_describe(tk->kind);
        if (what[0] == '\'')   /* keywords and operators are already quoted: 'THEN' */
            diag_add(diags, DIAG_ERROR, "syntax", tk->line, tk->col,
                     "unexpected %s; expected %s", what, expected);
        else                    /* identifier 'x', integer literal '42' */
            diag_add(diags, DIAG_ERROR, "syntax", tk->line, tk->col,
                     "unexpected %s '%s'; expected %s", what, tk->lexeme, expected);
    }
}

AstNode *nova_parse(const LeoFrontend *fe, const TokenList *tokens,
                    DiagList *diags, int trace) {
    int n = tokens->count - 1;   /* the parser appends its own $ for the TK_EOF token */
    if (n == 0) {
        const Token *eof = &tokens->items[0];
        diag_add(diags, DIAG_ERROR, "syntax", eof->line, eof->col,
                 "empty program: the file contains no statements");
        return NULL;
    }

    int *syms = (int *)xmalloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) {
        syms[i] = fe->symbol_of_kind[tokens->items[i].kind];
        if (syms[i] == -1) {
            const Token *tk = &tokens->items[i];
            diag_add(diags, DIAG_ERROR, "syntax", tk->line, tk->col,
                     "token '%s' is not part of the grammar", tk->lexeme);
            free(syms);
            return NULL;
        }
    }

    BuildCtx ctx = { fe, tokens };
    ParserHooks hooks = { &ctx, hook_shift, hook_reduce, hook_discard };
    ParseOutcome o = parser_run_hooks(fe->grammar, fe->table, syms, n, &hooks, trace);
    free(syms);

    if (o.result != PARSE_ACCEPT) {
        if (o.internal_error) {
            diag_add(diags, DIAG_ERROR, "syntax", 0, 0,
                     "internal error: missing GOTO entry in parsing table (state %d, symbol %s)",
                     o.error_state, grammar_symbol_name(fe->grammar, o.error_symbol));
        } else {
            report_syntax_error(fe, tokens, &o, diags);
        }
        return NULL;
    }
    return (AstNode *)o.value;
}
