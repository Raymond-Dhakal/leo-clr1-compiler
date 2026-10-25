/*
 * nova_ast.h
 * Part of the Leo compiler extension - the Abstract Syntax Tree.
 *
 * The AST is built DURING the CLR(1) parse (see nova_parse.c): every
 * reduction of a grammar production creates or combines AST nodes. Later
 * phases (semantic analysis, IR generation) only walk this tree.
 */
#ifndef NOVA_AST_H
#define NOVA_AST_H

#include <stdio.h>

typedef enum {
    TYPE_UNKNOWN = 0,  /* not yet computed by semantic analysis      */
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_ERROR         /* an error was already reported; avoid cascades */
} DataType;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE,
    OP_AND, OP_OR
} BinOp;

typedef enum { UOP_NEG, UOP_NOT } UnOp;

typedef enum {
    AST_PROGRAM,   /* statement list, root of the tree               */
    AST_BLOCK,     /* statement list inside IF / WHILE / REPEAT      */
    AST_DECL,      /* INT x   or   INT x <- expr                      */
    AST_ASSIGN,    /* x <- expr                                       */
    AST_IF,        /* IF (cond) THEN ... [ELSE ...] END               */
    AST_WHILE,     /* WHILE (cond) DO ... END                         */
    AST_REPEAT,    /* REPEAT ... UNTIL (cond)                         */
    AST_FOR,       /* FOR [(] [Type] i <- a TO b [STEP s] [)] DO ... END */
    AST_INPUT,     /* INPUT x   or   INPUT "prompt", x               */
    AST_PRINT,     /* PRINT item {, item}   (items in u.block)        */
    AST_BINARY,
    AST_UNARY,
    AST_VAR,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_BOOL_LIT,
    AST_STR_LIT    /* "text" (only as a PRINT item)                   */
} AstKind;

typedef struct AstNode AstNode;

struct AstNode {
    AstKind kind;
    int line, col;     /* source position (see nova_parse.c for which token) */
    int print_newline; /* AST_PRINT: whether PRINT ends the current line */
    DataType type;     /* type of an expression, filled in by semantic analysis */
    union {
        struct { AstNode **stmts; int count, capacity; } block;  /* PROGRAM, BLOCK */
        struct { DataType decl_type; char *name; AstNode *init; int sym; } decl;
        struct { char *name; AstNode *value; int sym; } assign;
        struct { AstNode *cond; AstNode *then_block; AstNode *else_block; } if_stmt;
        struct { AstNode *cond; AstNode *body; } loop;           /* WHILE, REPEAT */
        struct {
            int has_decl;            /* FOR INT i ...: the loop declares its variable   */
            DataType decl_type;
            char *name; int sym;
            AstNode *start, *limit, *step;   /* step may be NULL (means 1) */
            AstNode *body;
        } for_stmt;
        struct { char *name; int sym; char *prompt; } input;   /* prompt may be NULL */
        char *str;                                              /* AST_STR_LIT, decoded text */
        struct { BinOp op; AstNode *left, *right; } binary;
        struct { UnOp op; AstNode *operand; } unary;
        struct { char *name; int sym; } var;
        long long ival;
        double fval;
        int bval;
    } u;
};

AstNode *ast_new(AstKind kind, int line, int col);
void ast_block_append(AstNode *block, AstNode *stmt);
void ast_free(AstNode *n);

/* If `n` is an INT or FLOAT literal, possibly under unary '~', stores its
   value and returns 1 (is_float tells which); otherwise returns 0. */
int ast_const_number(const AstNode *n, int *is_float, long long *iv, double *fv);

/* Prints an indented tree. With show_types, expression nodes are followed
   by ": INT" etc. (only meaningful after semantic analysis). */
void ast_print(const AstNode *n, FILE *out, int show_types);

const char *data_type_name(DataType t);
const char *binop_text(BinOp op);
const char *unop_text(UnOp op);

#endif /* NOVA_AST_H */
