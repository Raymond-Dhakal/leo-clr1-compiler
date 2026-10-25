/*
 * nova_semantic.h
 * Part of the Leo compiler extension - PHASE 3: semantic analysis.
 *
 * Walks the AST once, maintaining a scoped symbol table, and
 *   - resolves every variable use to a unique symbol id (stored in the AST),
 *   - computes the type of every expression (stored in AstNode.type),
 *   - reports semantic errors and warnings through DiagList.
 *
 * Type system (see language/language_spec.md):
 *   INT, FLOAT, BOOL. INT converts implicitly to FLOAT (widening) in
 *   arithmetic, comparisons and assignment; FLOAT never converts to INT
 *   implicitly; BOOL never converts to or from a number.
 * Scoping: declarations are visible from the declaration to the end of the
 *   enclosing block. Redeclaring a name in the SAME block is an error;
 *   declaring it in a nested block shadows the outer variable.
 */
#ifndef NOVA_SEMANTIC_H
#define NOVA_SEMANTIC_H

#include <stdio.h>
#include "nova_ast.h"
#include "nova_diag.h"

typedef struct {
    char *name;
    DataType type;
    int line, col;     /* where it was declared */
    int scope;         /* nesting depth of the declaring block (0 = program level) */
} VarSymbol;

typedef struct {
    VarSymbol *vars;           /* every variable ever declared; index = symbol id */
    int count, capacity;
    int *active;               /* symbol ids currently visible (innermost last)   */
    int active_count, active_capacity;
    int *scope_marks;          /* active_count at the moment each scope was opened */
    int depth, marks_capacity;
} SymTab;

void symtab_init(SymTab *st);
void symtab_free(SymTab *st);
void symtab_print(const SymTab *st, FILE *out);

/*
 * Analyses the program. Returns the number of semantic ERRORS (warnings
 * are only added to `diags`). `st` receives the symbol table; it must
 * have been initialised with symtab_init().
 */
int nova_analyze(AstNode *program, SymTab *st, DiagList *diags);

#endif /* NOVA_SEMANTIC_H */
