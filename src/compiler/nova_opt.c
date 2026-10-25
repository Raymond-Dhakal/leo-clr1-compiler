/*
 * nova_opt.c - PHASE 5: IR optimization.
 *
 * Passes (run repeatedly until none of them changes anything):
 *   1. pass_constprop  - global constant propagation + constant folding +
 *                        branch folding, using a forward dataflow analysis
 *                        over the control-flow graph (so loops are handled
 *                        soundly: a variable changed inside a loop is not
 *                        treated as constant).
 *   2. pass_algebraic  - x+0, x*1, x-x, x==x ... (type-aware: FLOAT rules
 *                        that are not exact, such as x+0.0, are NOT applied)
 *   3. pass_copyprop   - local copy propagation inside basic blocks
 *   4. pass_cleanup    - unreachable code, jumps to the next instruction,
 *                        unused labels
 *   5. pass_dce        - liveness analysis: removes dead assignments and
 *                        merges "t = e; x = t" into "x = e"
 *
 * Language facts the analyses rely on: every variable starts at zero
 * (0 / 0.0 / FALSE), temporaries are always written before they are read,
 * and the only observable effects are PRINT, INPUT and run-time errors.
 */
#include <limits.h>
#include <math.h>
#include <string.h>
#include "nova_opt.h"
#include "xalloc.h"

/* ---------------------------------------------------------------------
 * Constant arithmetic (must agree with the helpers in generated C code)
 * --------------------------------------------------------------------- */

static double to_double(Operand o) { return o.kind == OPD_INT ? (double)o.i : o.f; }

static long long wrap_add(long long a, long long b) { return (long long)((unsigned long long)a + (unsigned long long)b); }
static long long wrap_sub(long long a, long long b) { return (long long)((unsigned long long)a - (unsigned long long)b); }
static long long wrap_mul(long long a, long long b) { return (long long)((unsigned long long)a * (unsigned long long)b); }

/* Exponentiation by squaring with two's complement wrap-around; e >= 0. */
static long long ipow_wrap(long long base, long long e) {
    long long result = 1;
    while (e > 0) {
        if (e & 1) result = wrap_mul(result, base);
        base = wrap_mul(base, base);
        e >>= 1;
    }
    return result;
}

/* Evaluates `ins` on constant operands. Returns 0 if the operation must be
   left for run time (division by zero, negative exponent, non-finite). */
static int fold(const IRInstr *ins, Operand a, Operand b, Operand *res) {
    switch (ins->op) {
        case IR_ASSIGN: *res = a; return 1;
        case IR_NOT:    if (a.kind != OPD_BOOL) return 0; *res = opd_bool(!a.i); return 1;
        case IR_NEG:
            if (ins->optype == TYPE_FLOAT) { *res = opd_float(-to_double(a)); return 1; }
            if (a.kind != OPD_INT) return 0;
            *res = opd_int(wrap_sub(0, a.i));
            return 1;
        default: break;
    }
    if (!ir_is_binary(ins->op)) return 0;

    if (ins->optype == TYPE_BOOL) {       /* == and != on BOOLs */
        if (a.kind != OPD_BOOL || b.kind != OPD_BOOL) return 0;
        if (ins->op == IR_EQ) { *res = opd_bool(a.i == b.i); return 1; }
        if (ins->op == IR_NE) { *res = opd_bool(a.i != b.i); return 1; }
        return 0;
    }

    if (ins->optype == TYPE_FLOAT) {
        double x = to_double(a), y = to_double(b), r;
        switch (ins->op) {
            case IR_ADD: r = x + y; break;
            case IR_SUB: r = x - y; break;
            case IR_MUL: r = x * y; break;
            case IR_DIV: if (y == 0.0) return 0; r = x / y; break;
            case IR_POW: r = pow(x, y); break;
            case IR_LT: *res = opd_bool(x < y);  return 1;
            case IR_GT: *res = opd_bool(x > y);  return 1;
            case IR_LE: *res = opd_bool(x <= y); return 1;
            case IR_GE: *res = opd_bool(x >= y); return 1;
            case IR_EQ: *res = opd_bool(x == y); return 1;
            case IR_NE: *res = opd_bool(x != y); return 1;
            default: return 0;
        }
        if (!isfinite(r)) return 0;
        *res = opd_float(r);
        return 1;
    }

    if (a.kind != OPD_INT || b.kind != OPD_INT) return 0;
    long long x = a.i, y = b.i;
    switch (ins->op) {
        case IR_ADD: *res = opd_int(wrap_add(x, y)); return 1;
        case IR_SUB: *res = opd_int(wrap_sub(x, y)); return 1;
        case IR_MUL: *res = opd_int(wrap_mul(x, y)); return 1;
        case IR_DIV:
            if (y == 0) return 0;
            *res = opd_int(y == -1 ? wrap_sub(0, x) : x / y);
            return 1;
        case IR_MOD:
            if (y == 0) return 0;
            *res = opd_int(y == -1 ? 0 : x % y);
            return 1;
        case IR_POW:
            if (y < 0) return 0;
            *res = opd_int(ipow_wrap(x, y));
            return 1;
        case IR_LT: *res = opd_bool(x < y);  return 1;
        case IR_GT: *res = opd_bool(x > y);  return 1;
        case IR_LE: *res = opd_bool(x <= y); return 1;
        case IR_GE: *res = opd_bool(x >= y); return 1;
        case IR_EQ: *res = opd_bool(x == y); return 1;
        case IR_NE: *res = opd_bool(x != y); return 1;
        default: return 0;
    }
}

/* Converts constant `c` for storage in a variable of type `to`.
   Only INT -> FLOAT is a real conversion. Returns 0 if impossible. */
static int convert_const(Operand c, DataType to, Operand *out) {
    if (to == TYPE_FLOAT && c.kind == OPD_INT)   { *out = opd_float((double)c.i); return 1; }
    if (to == TYPE_FLOAT && c.kind == OPD_FLOAT) { *out = c; return 1; }
    if (to == TYPE_INT   && c.kind == OPD_INT)   { *out = c; return 1; }
    if (to == TYPE_BOOL  && c.kind == OPD_BOOL)  { *out = c; return 1; }
    return 0;
}

static int const_equal(Operand a, Operand b) {
    if (a.kind != b.kind) return 0;
    if (a.kind == OPD_FLOAT) return memcmp(&a.f, &b.f, sizeof(double)) == 0;   /* distinguishes -0.0 */
    return a.i == b.i;
}

static Operand zero_const(DataType t) {
    if (t == TYPE_FLOAT) return opd_float(0.0);
    if (t == TYPE_BOOL) return opd_bool(0);
    return opd_int(0);
}

/* ---------------------------------------------------------------------
 * Control-flow graph of basic blocks
 * --------------------------------------------------------------------- */

typedef struct {
    int start, end;      /* instructions [start, end)            */
    int succ[2];
    int nsucc;
    int *preds;
    int npreds;
} Block;

typedef struct {
    Block *blocks;
    int n;
    int *label_block;    /* label number -> block index (or -1)  */
} Cfg;

static int is_jump(IROp op) { return op == IR_GOTO || op == IR_IF_FALSE || op == IR_IF_TRUE; }

static void cfg_build(const IRProgram *p, Cfg *c) {
    memset(c, 0, sizeof(*c));
    int n = p->count;
    if (n == 0) return;

    char *lead = (char *)xcalloc((size_t)n + 1, 1);
    lead[0] = 1;
    for (int i = 0; i < n; i++) {
        if (p->code[i].op == IR_LABEL) lead[i] = 1;
        if (is_jump(p->code[i].op) && i + 1 < n) lead[i + 1] = 1;
    }
    int nb = 0;
    for (int i = 0; i < n; i++) if (lead[i]) nb++;

    c->blocks = (Block *)xcalloc((size_t)nb, sizeof(Block));
    c->n = nb;
    int bi = -1;
    for (int i = 0; i < n; i++) {
        if (lead[i]) {
            if (bi >= 0) c->blocks[bi].end = i;
            bi++;
            c->blocks[bi].start = i;
        }
    }
    c->blocks[bi].end = n;
    free(lead);

    int label_cap = p->label_count + 2;
    c->label_block = (int *)xmalloc(sizeof(int) * (size_t)label_cap);
    for (int i = 0; i < label_cap; i++) c->label_block[i] = -1;
    for (int b = 0; b < nb; b++) {
        const IRInstr *first = &p->code[c->blocks[b].start];
        if (first->op == IR_LABEL && first->label < label_cap) c->label_block[first->label] = b;
    }

    for (int b = 0; b < nb; b++) {
        Block *blk = &c->blocks[b];
        const IRInstr *last = &p->code[blk->end - 1];
        int fall = (b + 1 < nb) ? b + 1 : -1;
        int target = (is_jump(last->op) && last->label < label_cap) ? c->label_block[last->label] : -1;
        if (last->op == IR_GOTO) {
            if (target >= 0) blk->succ[blk->nsucc++] = target;
        } else if (last->op == IR_IF_FALSE || last->op == IR_IF_TRUE) {
            if (target >= 0) blk->succ[blk->nsucc++] = target;
            if (fall >= 0 && fall != target) blk->succ[blk->nsucc++] = fall;
        } else if (fall >= 0) {
            blk->succ[blk->nsucc++] = fall;
        }
    }

    for (int b = 0; b < nb; b++)
        for (int s = 0; s < c->blocks[b].nsucc; s++) c->blocks[c->blocks[b].succ[s]].npreds++;
    for (int b = 0; b < nb; b++) {
        c->blocks[b].preds = (int *)xmalloc(sizeof(int) * (size_t)(c->blocks[b].npreds + 1));
        c->blocks[b].npreds = 0;
    }
    for (int b = 0; b < nb; b++)
        for (int s = 0; s < c->blocks[b].nsucc; s++) {
            Block *t = &c->blocks[c->blocks[b].succ[s]];
            t->preds[t->npreds++] = b;
        }
}

static void cfg_free(Cfg *c) {
    for (int b = 0; b < c->n; b++) free(c->blocks[b].preds);
    free(c->blocks);
    free(c->label_block);
    memset(c, 0, sizeof(*c));
}

/* Removes IR_NOP instructions. Returns how many were removed. */
static int compact(IRProgram *p) {
    int j = 0;
    for (int i = 0; i < p->count; i++)
        if (p->code[i].op != IR_NOP) p->code[j++] = p->code[i];
    int removed = p->count - j;
    p->count = j;
    return removed;
}

static void make_nop(IRInstr *ins) { ins->op = IR_NOP; ins->dst = -1; ins->a = opd_none(); ins->b = opd_none(); }

/* Which operand slots does an instruction read? */
static int uses_a(IROp op) {
    return ir_is_binary(op) || op == IR_ASSIGN || op == IR_NEG || op == IR_NOT ||
           op == IR_IF_FALSE || op == IR_IF_TRUE || op == IR_PRINT ||
           op == IR_WRITE || op == IR_PRINT_CONT || op == IR_CHKSTEP;
}
static int uses_b(IROp op) { return ir_is_binary(op); }

/* ---------------------------------------------------------------------
 * Pass 1: global constant propagation and folding
 * --------------------------------------------------------------------- */

enum { LAT_TOP = 0, LAT_CONST = 1, LAT_NAC = 2 };

typedef struct {
    unsigned char tag;   /* TOP = not reached yet, CONST = known value, NAC = varies */
    Operand c;
} Lat;

static Lat lat_meet(Lat a, Lat b) {
    if (a.tag == LAT_TOP) return b;
    if (b.tag == LAT_TOP) return a;
    if (a.tag == LAT_NAC || b.tag == LAT_NAC) { Lat r; memset(&r, 0, sizeof(r)); r.tag = LAT_NAC; return r; }
    if (const_equal(a.c, b.c)) return a;
    Lat r; memset(&r, 0, sizeof(r)); r.tag = LAT_NAC; return r;
}

static int lat_equal(Lat a, Lat b) {
    return a.tag == b.tag && (a.tag != LAT_CONST || const_equal(a.c, b.c));
}

static int eval_operand(const Lat *state, Operand o, Operand *c) {
    if (opd_is_const(o)) { *c = o; return 1; }
    if (o.kind == OPD_VAR && state[o.var].tag == LAT_CONST) { *c = state[o.var].c; return 1; }
    return 0;
}

/* If `ins` computes a compile-time constant under `state`, returns 1 and
   stores it, already converted to the type of the destination variable. */
static int try_fold(const IRProgram *p, const IRInstr *ins, const Lat *state, Operand *res) {
    if (ins->op == IR_INPUT || ins->dst < 0) return 0;
    Operand a = opd_none(), b = opd_none(), r;
    if (uses_a(ins->op) && !eval_operand(state, ins->a, &a)) return 0;
    if (uses_b(ins->op) && !eval_operand(state, ins->b, &b)) return 0;
    if (!fold(ins, a, b, &r)) return 0;
    return convert_const(r, p->vars[ins->dst].type, res);
}

static void apply_def(const IRProgram *p, const IRInstr *ins, Lat *state) {
    int d = ir_defines(ins);
    if (d < 0) return;
    Operand r;
    if (try_fold(p, ins, state, &r)) { state[d].tag = LAT_CONST; state[d].c = r; }
    else { memset(&state[d], 0, sizeof(Lat)); state[d].tag = LAT_NAC; }
}

static int pass_constprop(IRProgram *p, OptStats *st) {
    Cfg cfg;
    cfg_build(p, &cfg);
    if (cfg.n == 0) return 0;
    int V = p->var_count, nb = cfg.n;
    Lat *in = (Lat *)xcalloc((size_t)nb * (size_t)V, sizeof(Lat));
    Lat *out = (Lat *)xcalloc((size_t)nb * (size_t)V, sizeof(Lat));
    Lat *tmp = (Lat *)xcalloc((size_t)V, sizeof(Lat));
    Lat *entry = (Lat *)xcalloc((size_t)V, sizeof(Lat));
    char *visited = (char *)xcalloc((size_t)nb, 1);

    /* Program entry: user variables hold their zero value, temps are unknown. */
    for (int v = 0; v < V; v++) {
        if (v < p->user_var_count) { entry[v].tag = LAT_CONST; entry[v].c = zero_const(p->vars[v].type); }
        else entry[v].tag = LAT_NAC;
    }

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int b = 0; b < nb; b++) {
            Block *blk = &cfg.blocks[b];
            int any_pred = (b == 0);
            for (int k = 0; k < blk->npreds; k++) if (visited[blk->preds[k]]) any_pred = 1;
            if (!any_pred) continue;

            Lat *bin = &in[(size_t)b * V];
            for (int v = 0; v < V; v++) {
                Lat m; memset(&m, 0, sizeof(m));   /* TOP */
                if (b == 0) m = entry[v];
                for (int k = 0; k < blk->npreds; k++) {
                    int pb = blk->preds[k];
                    if (visited[pb]) m = lat_meet(m, out[(size_t)pb * V + v]);
                }
                bin[v] = m;
            }
            memcpy(tmp, bin, sizeof(Lat) * (size_t)V);
            for (int i = blk->start; i < blk->end; i++) apply_def(p, &p->code[i], tmp);

            Lat *bout = &out[(size_t)b * V];
            int differs = !visited[b];
            for (int v = 0; v < V && !differs; v++) if (!lat_equal(bout[v], tmp[v])) differs = 1;
            if (differs) {
                memcpy(bout, tmp, sizeof(Lat) * (size_t)V);
                visited[b] = 1;
                changed = 1;
            }
        }
    }

    /* Rewrite phase: use the final IN state of each reachable block. */
    int modified = 0;
    for (int b = 0; b < nb; b++) {
        if (!visited[b]) continue;
        Block *blk = &cfg.blocks[b];
        memcpy(tmp, &in[(size_t)b * V], sizeof(Lat) * (size_t)V);
        for (int i = blk->start; i < blk->end; i++) {
            IRInstr *ins = &p->code[i];

            /* (a) replace variable operands that are known constants */
            Operand c;
            if (uses_a(ins->op) && ins->a.kind == OPD_VAR && eval_operand(tmp, ins->a, &c)) {
                ins->a = c; st->propagated++; modified++;
            }
            if (uses_b(ins->op) && ins->b.kind == OPD_VAR && eval_operand(tmp, ins->b, &c)) {
                ins->b = c; st->propagated++; modified++;
            }

            /* a FOR step check on a non zero constant can never fail */
            if (ins->op == IR_CHKSTEP && opd_is_const(ins->a) &&
                !((ins->a.kind == OPD_INT && ins->a.i == 0) || (ins->a.kind == OPD_FLOAT && ins->a.f == 0.0))) {
                make_nop(ins); st->dead++; modified++;
                continue;
            }

            /* (b) resolve branches whose condition is now constant */
            if ((ins->op == IR_IF_FALSE || ins->op == IR_IF_TRUE) && ins->a.kind == OPD_BOOL) {
                int jump = (ins->op == IR_IF_FALSE) ? !ins->a.i : ins->a.i;
                if (jump) { ins->op = IR_GOTO; ins->a = opd_none(); }
                else make_nop(ins);
                st->branches++; modified++;
                continue;
            }

            /* (c) fold operations on constants into a plain assignment */
            if (ins->op != IR_ASSIGN && ins->op != IR_INPUT && ins->dst >= 0 &&
                (ir_is_binary(ins->op) || ins->op == IR_NEG || ins->op == IR_NOT)) {
                Operand r;
                if (try_fold(p, ins, tmp, &r)) {
                    ins->op = IR_ASSIGN; ins->a = r; ins->b = opd_none();
                    st->folded++; modified++;
                }
            }

            /* (d) "x = c" when x already holds c: nothing to do */
            if (ins->op == IR_ASSIGN && ins->dst < p->user_var_count && opd_is_const(ins->a)) {
                Operand conv;
                if (tmp[ins->dst].tag == LAT_CONST && convert_const(ins->a, p->vars[ins->dst].type, &conv) &&
                    const_equal(conv, tmp[ins->dst].c)) {
                    make_nop(ins);
                    st->dead++; modified++;
                    continue;
                }
            }

            apply_def(p, ins, tmp);
        }
    }

    free(in); free(out); free(tmp); free(entry); free(visited);
    cfg_free(&cfg);
    compact(p);
    return modified;
}

/* ---------------------------------------------------------------------
 * Pass 2: algebraic simplification
 * --------------------------------------------------------------------- */

static int is_num(Operand o, double v) {
    return (o.kind == OPD_INT && (double)o.i == v) || (o.kind == OPD_FLOAT && o.f == v);
}
static int same_var(Operand a, Operand b) { return a.kind == OPD_VAR && b.kind == OPD_VAR && a.var == b.var; }

/* If the binary instruction `ins` is an algebraic identity, stores the
   value it is equal to (one of its operands or a constant) in *result. */
static int simplify(const IRInstr *ins, Operand *result) {
    Operand a = ins->a, b = ins->b;
    if (ins->optype == TYPE_INT) {
        switch (ins->op) {
            case IR_ADD:
                if (is_num(b, 0)) { *result = a; return 1; }
                if (is_num(a, 0)) { *result = b; return 1; }
                return 0;
            case IR_SUB:
                if (is_num(b, 0)) { *result = a; return 1; }
                if (same_var(a, b)) { *result = opd_int(0); return 1; }
                return 0;
            case IR_MUL:
                if (is_num(b, 1)) { *result = a; return 1; }
                if (is_num(a, 1)) { *result = b; return 1; }
                if (is_num(a, 0) || is_num(b, 0)) { *result = opd_int(0); return 1; }
                return 0;
            case IR_DIV:
                if (is_num(b, 1)) { *result = a; return 1; }
                return 0;
            case IR_MOD:
                if (is_num(b, 1)) { *result = opd_int(0); return 1; }
                return 0;
            case IR_POW:
                if (is_num(b, 1)) { *result = a; return 1; }
                if (is_num(b, 0)) { *result = opd_int(1); return 1; }
                return 0;
            case IR_EQ: case IR_LE: case IR_GE:
                if (same_var(a, b)) { *result = opd_bool(1); return 1; }
                return 0;
            case IR_NE: case IR_LT: case IR_GT:
                if (same_var(a, b)) { *result = opd_bool(0); return 1; }
                return 0;
            default:
                return 0;
        }
    }
    if (ins->optype == TYPE_FLOAT) {
        /* Only identities that are exact for every double (NaN, inf, -0.0 included). */
        switch (ins->op) {
            case IR_MUL:
                if (is_num(b, 1)) { *result = a; return 1; }
                if (is_num(a, 1)) { *result = b; return 1; }
                return 0;
            case IR_DIV: case IR_POW:
                if (is_num(b, 1)) { *result = a; return 1; }
                return 0;
            default:
                return 0;
        }
    }
    if (ins->optype == TYPE_BOOL && same_var(a, b)) {
        if (ins->op == IR_EQ) { *result = opd_bool(1); return 1; }
        if (ins->op == IR_NE) { *result = opd_bool(0); return 1; }
    }
    return 0;
}

static int pass_algebraic(IRProgram *p, OptStats *st) {
    int changed = 0;
    for (int i = 0; i < p->count; i++) {
        IRInstr *ins = &p->code[i];
        Operand r;
        if (!ir_is_binary(ins->op) || !simplify(ins, &r)) continue;
        ins->op = IR_ASSIGN;
        ins->a = r;
        ins->b = opd_none();
        st->algebraic++;
        changed++;
    }
    return changed;
}

/* ---------------------------------------------------------------------
 * Pass 3: local copy propagation
 * --------------------------------------------------------------------- */

static int pass_copyprop(IRProgram *p, OptStats *st) {
    Cfg cfg;
    cfg_build(p, &cfg);
    int V = p->var_count, changed = 0;
    int *copy_of = (int *)xmalloc(sizeof(int) * (size_t)(V ? V : 1));

    for (int b = 0; b < cfg.n; b++) {
        for (int v = 0; v < V; v++) copy_of[v] = -1;
        for (int i = cfg.blocks[b].start; i < cfg.blocks[b].end; i++) {
            IRInstr *ins = &p->code[i];
            if (uses_a(ins->op) && ins->a.kind == OPD_VAR && copy_of[ins->a.var] >= 0) {
                ins->a = opd_var(copy_of[ins->a.var]); st->copies++; changed++;
            }
            if (uses_b(ins->op) && ins->b.kind == OPD_VAR && copy_of[ins->b.var] >= 0) {
                ins->b = opd_var(copy_of[ins->b.var]); st->copies++; changed++;
            }
            int d = ir_defines(ins);
            if (d < 0) continue;

            /* d gets a new value: forget copies of d and copies made from d */
            copy_of[d] = -1;
            for (int v = 0; v < V; v++) if (copy_of[v] == d) copy_of[v] = -1;

            if (ins->op == IR_ASSIGN && ins->a.kind == OPD_VAR) {
                if (ins->a.var == d) {            /* x = x */
                    make_nop(ins); st->dead++; changed++;
                } else if (p->vars[d].type == p->vars[ins->a.var].type) {
                    copy_of[d] = ins->a.var;      /* only same-type copies are interchangeable */
                }
            }
        }
    }
    free(copy_of);
    cfg_free(&cfg);
    compact(p);
    return changed;
}

/* ---------------------------------------------------------------------
 * Pass 4: CFG cleanup (unreachable code, redundant jumps, unused labels)
 * --------------------------------------------------------------------- */

static int pass_cleanup(IRProgram *p, OptStats *st) {
    int changed = 0;

    /* (1) code that no path from the entry can reach */
    Cfg cfg;
    cfg_build(p, &cfg);
    if (cfg.n > 0) {
        char *reach = (char *)xcalloc((size_t)cfg.n, 1);
        int *stack = (int *)xmalloc(sizeof(int) * (size_t)cfg.n);
        int sp = 0;
        stack[sp++] = 0; reach[0] = 1;
        while (sp > 0) {
            int b = stack[--sp];
            for (int s = 0; s < cfg.blocks[b].nsucc; s++) {
                int t = cfg.blocks[b].succ[s];
                if (!reach[t]) { reach[t] = 1; stack[sp++] = t; }
            }
        }
        for (int b = 0; b < cfg.n; b++) {
            if (reach[b]) continue;
            for (int i = cfg.blocks[b].start; i < cfg.blocks[b].end; i++) {
                if (p->code[i].op == IR_NOP) continue;
                make_nop(&p->code[i]);
                st->unreachable++; changed++;
            }
        }
        free(reach); free(stack);
    }
    cfg_free(&cfg);
    compact(p);

    /* (2) a jump to the label that immediately follows is not needed */
    for (int i = 0; i < p->count; i++) {
        IRInstr *ins = &p->code[i];
        if (!is_jump(ins->op)) continue;
        for (int j = i + 1; j < p->count && p->code[j].op == IR_LABEL; j++) {
            if (p->code[j].label == ins->label) {
                make_nop(ins); st->labels++; changed++;
                break;
            }
        }
    }
    compact(p);

    /* (3) labels nobody jumps to */
    int *refs = (int *)xcalloc((size_t)p->label_count + 2, sizeof(int));
    for (int i = 0; i < p->count; i++) if (is_jump(p->code[i].op)) refs[p->code[i].label]++;
    for (int i = 0; i < p->count; i++) {
        if (p->code[i].op == IR_LABEL && refs[p->code[i].label] == 0) {
            make_nop(&p->code[i]); st->labels++; changed++;
        }
    }
    free(refs);
    compact(p);
    return changed;
}

/* ---------------------------------------------------------------------
 * Pass 5: liveness-based dead code elimination and temp forwarding
 * --------------------------------------------------------------------- */

/* May the instruction be deleted if its result is never read? Anything that
   can fail at run time (division by a possibly-zero value, negative INT
   exponent) or has an effect (INPUT, PRINT, jumps) must stay. */
static int is_removable(const IRInstr *ins) {
    switch (ins->op) {
        case IR_ASSIGN: case IR_NEG: case IR_NOT:
        case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_LT: case IR_GT: case IR_LE: case IR_GE: case IR_EQ: case IR_NE:
            return 1;
        case IR_DIV: case IR_MOD:
            return (ins->b.kind == OPD_INT && ins->b.i != 0) || (ins->b.kind == OPD_FLOAT && ins->b.f != 0.0);
        case IR_POW:
            if (ins->optype == TYPE_FLOAT) return 1;
            return ins->b.kind == OPD_INT && ins->b.i >= 0;
        default:
            return 0;
    }
}

static void live_step(const IRInstr *ins, unsigned char *live) {
    int d = ir_defines(ins);
    if (d >= 0) live[d] = 0;
    int u[2], n = ir_uses(ins, u);
    for (int k = 0; k < n; k++) live[u[k]] = 1;
}

static int pass_dce(IRProgram *p, OptStats *st) {
    Cfg cfg;
    cfg_build(p, &cfg);
    if (cfg.n == 0) return 0;
    int V = p->var_count, nb = cfg.n, changed = 0;
    unsigned char *live_in = (unsigned char *)xcalloc((size_t)nb * (size_t)V, 1);
    unsigned char *live_out = (unsigned char *)xcalloc((size_t)nb * (size_t)V, 1);
    unsigned char *live = (unsigned char *)xcalloc((size_t)V, 1);

    /* backward dataflow: live-out(B) = union of live-in(successors) */
    int again = 1;
    while (again) {
        again = 0;
        for (int b = nb - 1; b >= 0; b--) {
            Block *blk = &cfg.blocks[b];
            unsigned char *bout = &live_out[(size_t)b * V];
            for (int v = 0; v < V; v++) {
                unsigned char o = 0;
                for (int s = 0; s < blk->nsucc; s++) o |= live_in[(size_t)blk->succ[s] * V + v];
                bout[v] = o;
            }
            memcpy(live, bout, (size_t)V);
            for (int i = blk->end - 1; i >= blk->start; i--) live_step(&p->code[i], live);
            unsigned char *bin = &live_in[(size_t)b * V];
            if (memcmp(bin, live, (size_t)V) != 0) { memcpy(bin, live, (size_t)V); again = 1; }
        }
    }

    for (int b = 0; b < nb; b++) {
        Block *blk = &cfg.blocks[b];
        memcpy(live, &live_out[(size_t)b * V], (size_t)V);
        for (int i = blk->end - 1; i >= blk->start; i--) {
            IRInstr *ins = &p->code[i];
            if (ins->op == IR_NOP) continue;
            int d = ir_defines(ins);

            if (d >= 0 && !live[d] && is_removable(ins)) {      /* dead assignment */
                make_nop(ins); st->dead++; changed++;
                continue;
            }

            /* "t = e ; x = t" with t dead afterwards  ==>  "x = e" */
            if (ins->op == IR_ASSIGN && ins->a.kind == OPD_VAR && p->vars[ins->a.var].is_temp &&
                ins->a.var != ins->dst && !live[ins->a.var]) {
                int j = i - 1;
                while (j >= blk->start && p->code[j].op == IR_NOP) j--;
                if (j >= blk->start && ir_defines(&p->code[j]) == ins->a.var && p->code[j].op != IR_INPUT) {
                    p->code[j].dst = ins->dst;
                    make_nop(ins);
                    st->forwarded++; changed++;
                    continue;      /* live-after of the merged instruction is unchanged */
                }
            }
            live_step(ins, live);
        }
    }

    free(live_in); free(live_out); free(live);
    cfg_free(&cfg);
    compact(p);
    return changed;
}

/* ---------------------------------------------------------------------
 * Driver
 * --------------------------------------------------------------------- */

/* Give labels and temporaries compact, ascending numbers for readable output. */
static void renumber(IRProgram *p) {
    int *map = (int *)xcalloc((size_t)p->label_count + 2, sizeof(int));
    int next = 0;
    for (int i = 0; i < p->count; i++)
        if (p->code[i].op == IR_LABEL) map[p->code[i].label] = ++next;
    for (int i = 0; i < p->count; i++)
        if (p->code[i].op == IR_LABEL || is_jump(p->code[i].op)) p->code[i].label = map[p->code[i].label];
    p->label_count = next;
    free(map);

    int V = p->var_count;
    int *seen = (int *)xcalloc((size_t)(V ? V : 1), sizeof(int));
    int order = 0;
    int *new_num = (int *)xcalloc((size_t)(V ? V : 1), sizeof(int));
    for (int i = 0; i < p->count; i++) {
        int refs[3], n = 0;
        int d = ir_defines(&p->code[i]);
        if (d >= 0) refs[n++] = d;
        int u[2], nu = ir_uses(&p->code[i], u);
        for (int k = 0; k < nu; k++) refs[n++] = u[k];
        for (int k = 0; k < n; k++)
            if (p->vars[refs[k]].is_temp && !seen[refs[k]]) { seen[refs[k]] = 1; new_num[refs[k]] = ++order; }
    }
    /* temps first get a neutral name, then the final one, so renaming
       cannot collide with a name that has not been replaced yet */
    for (int v = 0; v < V; v++) {
        if (!p->vars[v].is_temp) continue;
        char name[32];
        snprintf(name, sizeof(name), "~tmp%d", v);
        free(p->vars[v].name);
        p->vars[v].name = xstrdup(name);
    }
    for (int v = 0; v < V; v++) {
        if (!p->vars[v].is_temp || !seen[v]) continue;
        char name[32];
        int k = new_num[v];
        for (;;) {
            snprintf(name, sizeof(name), "t%d", k);
            int clash = 0;
            for (int w = 0; w < V; w++) if (w != v && strcmp(p->vars[w].name, name) == 0) { clash = 1; break; }
            if (!clash) break;
            k += 1000;     /* a user variable is called t<n>: pick another number */
        }
        free(p->vars[v].name);
        p->vars[v].name = xstrdup(name);
    }
    p->temp_counter = order;
    free(seen); free(new_num);
}

void opt_run(IRProgram *p, OptStats *st) {
    memset(st, 0, sizeof(*st));
    st->before = p->count;
    for (int round = 1; round <= 25; round++) {
        int changed = 0;
        changed += pass_constprop(p, st);
        changed += pass_algebraic(p, st);
        changed += pass_copyprop(p, st);
        changed += pass_cleanup(p, st);
        changed += pass_dce(p, st);
        st->rounds = round;
        if (!changed) break;
    }
    renumber(p);
    st->after = p->count;
}

void opt_stats_print(const OptStats *s, FILE *out) {
    fprintf(out, "Optimization summary (%d round%s):\n", s->rounds, s->rounds == 1 ? "" : "s");
    fprintf(out, "  constants propagated        : %d\n", s->propagated);
    fprintf(out, "  expressions folded          : %d\n", s->folded);
    fprintf(out, "  algebraic simplifications   : %d\n", s->algebraic);
    fprintf(out, "  branches resolved           : %d\n", s->branches);
    fprintf(out, "  copies propagated           : %d\n", s->copies);
    fprintf(out, "  unreachable instructions    : %d\n", s->unreachable);
    fprintf(out, "  jumps/labels removed        : %d\n", s->labels);
    fprintf(out, "  dead/redundant assignments  : %d\n", s->dead);
    fprintf(out, "  temporaries forwarded       : %d\n", s->forwarded);
    fprintf(out, "  instructions                : %d -> %d\n", s->before, s->after);
}
