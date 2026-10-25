/*
 * nova_semantic.c - PHASE 3: semantic analysis.
 *
 * One recursive pass over the AST. Expression checking returns the type of
 * the expression (and stores it in the node). A TYPE_ERROR result means
 * "an error about this expression was already reported", which stops one
 * mistake from producing a chain of follow-up messages.
 */
#include <stdarg.h>
#include <string.h>
#include "nova_semantic.h"
#include "xalloc.h"

#define SEM "semantic"

/* ---------------------------------------------------------------------
 * Symbol table
 * --------------------------------------------------------------------- */

void symtab_init(SymTab *st) { memset(st, 0, sizeof(*st)); }

void symtab_free(SymTab *st) {
    for (int i = 0; i < st->count; i++) free(st->vars[i].name);
    free(st->vars);
    free(st->active);
    free(st->scope_marks);
    memset(st, 0, sizeof(*st));
}

static void scope_enter(SymTab *st) {
    NOVA_GROW(st->scope_marks, st->depth, st->marks_capacity);
    st->scope_marks[st->depth++] = st->active_count;
}

static void scope_exit(SymTab *st) {
    st->depth--;
    st->active_count = st->scope_marks[st->depth];
}

/* Looks a name up from the innermost scope outwards. Returns symbol id or -1. */
static int lookup(const SymTab *st, const char *name) {
    for (int i = st->active_count - 1; i >= 0; i--) {
        if (strcmp(st->vars[st->active[i]].name, name) == 0) return st->active[i];
    }
    return -1;
}

/* Looks a name up in the CURRENT scope only (for redeclaration checks). */
static int lookup_current_scope(const SymTab *st, const char *name) {
    int start = (st->depth > 0) ? st->scope_marks[st->depth - 1] : 0;
    for (int i = st->active_count - 1; i >= start; i--) {
        if (strcmp(st->vars[st->active[i]].name, name) == 0) return st->active[i];
    }
    return -1;
}

static int declare(SymTab *st, const char *name, DataType type, int line, int col) {
    NOVA_GROW(st->vars, st->count, st->capacity);
    int id = st->count++;
    st->vars[id].name = xstrdup(name);
    st->vars[id].type = type;
    st->vars[id].line = line;
    st->vars[id].col = col;
    st->vars[id].scope = st->depth;
    NOVA_GROW(st->active, st->active_count, st->active_capacity);
    st->active[st->active_count++] = id;
    return id;
}

void symtab_print(const SymTab *st, FILE *out) {
    fprintf(out, "%-4s %-16s %-6s %-6s %s\n", "ID", "NAME", "TYPE", "SCOPE", "DECLARED");
    for (int i = 0; i < st->count; i++) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d:%d", st->vars[i].line, st->vars[i].col);
        fprintf(out, "%-4d %-16s %-6s %-6d %s\n", i, st->vars[i].name,
                data_type_name(st->vars[i].type), st->vars[i].scope, pos);
    }
}

/* ---------------------------------------------------------------------
 * Type rules
 * --------------------------------------------------------------------- */

typedef struct {
    SymTab *st;
    DiagList *diags;
    int errors;
} Sem;

static void error_at(Sem *s, const AstNode *n, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void error_at(Sem *s, const AstNode *n, const char *fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    diag_add(s->diags, DIAG_ERROR, SEM, n->line, n->col, "%s", buf);
    s->errors++;
}

static const char *article(DataType t) { return t == TYPE_INT ? "an" : "a"; }

static int is_numeric(DataType t) { return t == TYPE_INT || t == TYPE_FLOAT; }

/* Can a value of type `from` be stored in a variable of type `to`? */
static int assignable(DataType to, DataType from) {
    if (to == from) return 1;
    return to == TYPE_FLOAT && from == TYPE_INT;   /* the only implicit conversion */
}

static int is_zero_literal(const AstNode *n) {
    return (n->kind == AST_INT_LIT && n->u.ival == 0) ||
           (n->kind == AST_FLOAT_LIT && n->u.fval == 0.0);
}

static DataType check_expr(Sem *s, AstNode *n);
static void check_block(Sem *s, AstNode *block, int new_scope);

static DataType check_binary(Sem *s, AstNode *n) {
    DataType l = check_expr(s, n->u.binary.left);
    DataType r = check_expr(s, n->u.binary.right);
    BinOp op = n->u.binary.op;
    const char *ops = binop_text(op);

    int bad_operand = (l == TYPE_ERROR || r == TYPE_ERROR);

    switch (op) {
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_POW: case OP_MOD:
            if (bad_operand) return TYPE_ERROR;
            if (op == OP_MOD) {
                if (l != TYPE_INT || r != TYPE_INT) {
                    error_at(s, n, "operator 'MOD' requires INT operands, but got %s and %s",
                             data_type_name(l), data_type_name(r));
                    return TYPE_ERROR;
                }
                if (is_zero_literal(n->u.binary.right))
                    diag_add(s->diags, DIAG_WARNING, SEM, n->line, n->col,
                             "MOD by constant zero will fail at run time");
                return TYPE_INT;
            }
            if (!is_numeric(l) || !is_numeric(r)) {
                error_at(s, n, "operator '%s' requires numeric (INT or FLOAT) operands, but got %s and %s",
                         ops, data_type_name(l), data_type_name(r));
                return TYPE_ERROR;
            }
            if (op == OP_DIV && is_zero_literal(n->u.binary.right))
                diag_add(s->diags, DIAG_WARNING, SEM, n->line, n->col,
                         "division by constant zero will fail at run time");
            return (l == TYPE_FLOAT || r == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;

        case OP_LT: case OP_GT: case OP_LE: case OP_GE:
            if (bad_operand) return TYPE_BOOL;
            if (!is_numeric(l) || !is_numeric(r)) {
                error_at(s, n, "operator '%s' requires numeric (INT or FLOAT) operands, but got %s and %s",
                         ops, data_type_name(l), data_type_name(r));
                return TYPE_BOOL;   /* the result is a BOOL regardless: keep checking the condition */
            }
            return TYPE_BOOL;

        case OP_EQ: case OP_NE:
            if (bad_operand) return TYPE_BOOL;
            if (!((is_numeric(l) && is_numeric(r)) || (l == TYPE_BOOL && r == TYPE_BOOL))) {
                error_at(s, n, "operator '%s' needs two numbers or two BOOLs, but got %s and %s",
                         ops, data_type_name(l), data_type_name(r));
            }
            return TYPE_BOOL;

        case OP_AND: case OP_OR:
            if (bad_operand) return TYPE_BOOL;
            if (l != TYPE_BOOL || r != TYPE_BOOL) {
                error_at(s, n, "operator '%s' requires BOOL operands, but got %s and %s",
                         ops, data_type_name(l), data_type_name(r));
            }
            return TYPE_BOOL;
    }
    return TYPE_ERROR;
}

static DataType check_expr(Sem *s, AstNode *n) {
    DataType t = TYPE_ERROR;
    switch (n->kind) {
        case AST_INT_LIT:   t = TYPE_INT;   break;
        case AST_FLOAT_LIT: t = TYPE_FLOAT; break;
        case AST_BOOL_LIT:  t = TYPE_BOOL;  break;
        case AST_VAR: {
            int id = lookup(s->st, n->u.var.name);
            if (id == -1) {
                error_at(s, n, "use of undeclared variable '%s'", n->u.var.name);
                t = TYPE_ERROR;
            } else {
                n->u.var.sym = id;
                t = s->st->vars[id].type;
            }
            break;
        }
        case AST_UNARY: {
            DataType o = check_expr(s, n->u.unary.operand);
            if (n->u.unary.op == UOP_NEG) {
                if (o == TYPE_ERROR) t = TYPE_ERROR;
                else if (!is_numeric(o)) {
                    error_at(s, n, "unary '~' (negation) requires a numeric operand, but got %s", data_type_name(o));
                    t = TYPE_ERROR;
                } else t = o;
            } else {
                if (o != TYPE_ERROR && o != TYPE_BOOL)
                    error_at(s, n, "operator 'NOT' requires a BOOL operand, but got %s", data_type_name(o));
                t = TYPE_BOOL;
            }
            break;
        }
        case AST_BINARY:
            t = check_binary(s, n);
            break;
        default:
            break;
    }
    n->type = t;
    return t;
}

static void check_condition(Sem *s, AstNode *cond, const char *what) {
    DataType t = check_expr(s, cond);
    if (t != TYPE_BOOL && t != TYPE_ERROR) {
        error_at(s, cond, "the condition of %s must be BOOL, but the expression has type %s "
                 "(use a comparison such as '!= 0')", what, data_type_name(t));
    }
}

/* FOR i <- start TO limit [STEP step]: the three expressions are checked
   before the (optional) declaration of i, in the enclosing scope. */
static void check_for_value(Sem *s, AstNode *e, DataType var_type, const char *what, const char *name) {
    DataType t = check_expr(s, e);
    if (t == TYPE_ERROR || var_type == TYPE_ERROR) return;
    if (!is_numeric(t)) {
        error_at(s, e, "the FOR %s value must be numeric, but it has type %s", what, data_type_name(t));
    } else if (var_type == TYPE_INT && t == TYPE_FLOAT) {
        error_at(s, e, "the FOR %s value is FLOAT but loop variable '%s' is INT (FLOAT to INT narrowing is not implicit)",
                 what, name);
    }
}

static void check_for(Sem *s, AstNode *n) {
    const char *name = n->u.for_stmt.name;
    scope_enter(s->st);            /* the loop variable of FOR INT i ... lives in this scope */
    DataType vt = TYPE_ERROR;
    int id = -1;
    if (n->u.for_stmt.has_decl) {
        vt = n->u.for_stmt.decl_type;
    } else {
        id = lookup(s->st, name);
        if (id == -1) error_at(s, n, "FOR loop variable '%s' is not declared (declare it first or write FOR INT %s <- ...)", name, name);
        else vt = s->st->vars[id].type;
    }
    if (vt != TYPE_ERROR && !is_numeric(vt)) {
        error_at(s, n, "the FOR loop variable '%s' must be INT or FLOAT, but it is %s", name, data_type_name(vt));
        vt = TYPE_ERROR;
    }
    /* A declared variable must not be visible in its own start/limit/step. */
    check_for_value(s, n->u.for_stmt.start, vt, "start", name);
    check_for_value(s, n->u.for_stmt.limit, vt, "end", name);
    if (n->u.for_stmt.step) {
        check_for_value(s, n->u.for_stmt.step, vt, "STEP", name);
        int is_float; long long iv; double fv;
        if (ast_const_number(n->u.for_stmt.step, &is_float, &iv, &fv) && fv == 0.0 && iv == 0)
            error_at(s, n->u.for_stmt.step, "the FOR step is zero, so the loop would never end");
    }
    if (n->u.for_stmt.has_decl) {
        n->u.for_stmt.sym = declare(s->st, name, vt, n->line, n->col);
    } else {
        n->u.for_stmt.sym = id;
    }
    check_block(s, n->u.for_stmt.body, 1);
    scope_exit(s->st);
}

static void check_stmt(Sem *s, AstNode *n) {
    switch (n->kind) {
        case AST_DECL: {
            DataType vt = n->u.decl.decl_type;
            if (n->u.decl.init) {
                /* The initialiser is checked BEFORE the name is declared,
                   so "INT x <- x" refers to an outer x (or is undeclared). */
                DataType it = check_expr(s, n->u.decl.init);
                if (it != TYPE_ERROR && !assignable(vt, it)) {
                    error_at(s, n, "cannot initialise %s variable '%s' with %s %s value%s",
                             data_type_name(vt), n->u.decl.name, article(it), data_type_name(it),
                             (vt == TYPE_INT && it == TYPE_FLOAT) ? " (FLOAT to INT narrowing is not implicit)" : "");
                }
            }
            int prev = lookup_current_scope(s->st, n->u.decl.name);
            if (prev != -1) {
                error_at(s, n, "variable '%s' is already declared in this scope (first declared at %d:%d)",
                         n->u.decl.name, s->st->vars[prev].line, s->st->vars[prev].col);
            } else {
                n->u.decl.sym = declare(s->st, n->u.decl.name, vt, n->line, n->col);
            }
            break;
        }
        case AST_ASSIGN: {
            int id = lookup(s->st, n->u.assign.name);
            DataType vt = check_expr(s, n->u.assign.value);
            if (id == -1) {
                error_at(s, n, "assignment to undeclared variable '%s'", n->u.assign.name);
                break;
            }
            n->u.assign.sym = id;
            DataType target = s->st->vars[id].type;
            if (vt != TYPE_ERROR && !assignable(target, vt)) {
                error_at(s, n, "cannot assign %s %s value to %s variable '%s'%s",
                         article(vt), data_type_name(vt), data_type_name(target), n->u.assign.name,
                         (target == TYPE_INT && vt == TYPE_FLOAT) ? " (FLOAT to INT narrowing is not implicit)" : "");
            }
            break;
        }
        case AST_IF:
            check_condition(s, n->u.if_stmt.cond, "IF");
            check_block(s, n->u.if_stmt.then_block, 1);
            if (n->u.if_stmt.else_block) check_block(s, n->u.if_stmt.else_block, 1);
            break;
        case AST_WHILE:
            check_condition(s, n->u.loop.cond, "WHILE");
            check_block(s, n->u.loop.body, 1);
            break;
        case AST_REPEAT:
            /* The body is its own scope; the UNTIL condition is outside it. */
            check_block(s, n->u.loop.body, 1);
            check_condition(s, n->u.loop.cond, "UNTIL");
            break;
        case AST_INPUT: {
            int id = lookup(s->st, n->u.input.name);
            if (id == -1) error_at(s, n, "INPUT into undeclared variable '%s'", n->u.input.name);
            else n->u.input.sym = id;
            break;
        }
        case AST_PRINT:
            for (int i = 0; i < n->u.block.count; i++) {
                AstNode *item = n->u.block.stmts[i];
                if (item->kind == AST_STR_LIT) item->type = TYPE_UNKNOWN;   /* text, not a value */
                else check_expr(s, item);
            }
            break;
        case AST_FOR:
            check_for(s, n);
            break;
        default:
            break;
    }
}

static void check_block(Sem *s, AstNode *block, int new_scope) {
    if (new_scope) scope_enter(s->st);
    for (int i = 0; i < block->u.block.count; i++) check_stmt(s, block->u.block.stmts[i]);
    if (new_scope) scope_exit(s->st);
}

int nova_analyze(AstNode *program, SymTab *st, DiagList *diags) {
    Sem s = { st, diags, 0 };
    check_block(&s, program, 0);   /* program level = scope depth 0 */
    return s.errors;
}
