/*
 * nova_interp.c - executes the IR directly (see nova_interp.h).
 *
 * Every variable is a 64 bit cell. INT and BOOL use the integer member,
 * FLOAT the double member; the static type of the IR variable tells which.
 * The arithmetic mirrors the helpers emitted by nova_codegen.c one to one.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nova_interp.h"
#include "xalloc.h"

typedef union { long long i; double f; } Cell;

typedef struct {
    const IRProgram *p;
    Cell *vars;
    FILE *in, *out;
    int failed;
    int interactive;
} Vm;

static int fail(Vm *vm, int line, const char *msg) {
    fflush(vm->out);
    fprintf(stderr, "runtime error (line %d): %s\n", line, msg);
    vm->failed = 1;
    return 1;
}

static long long wrap_neg(long long a) { return (long long)(0ULL - (unsigned long long)a); }

/* Value of an operand as an integer / as a double (INT converts like a C cast). */
static long long opd_i(const Vm *vm, Operand o) {
    switch (o.kind) {
        case OPD_VAR: return vm->vars[o.var].i;
        case OPD_INT: case OPD_BOOL: return o.i;
        default: return 0;
    }
}

static double opd_f(const Vm *vm, Operand o) {
    switch (o.kind) {
        case OPD_VAR:
            return vm->p->vars[o.var].type == TYPE_FLOAT ? vm->vars[o.var].f : (double)vm->vars[o.var].i;
        case OPD_FLOAT: return o.f;
        case OPD_INT: case OPD_BOOL: return (double)o.i;
        default: return 0.0;
    }
}

/* dst = value, converting INT to FLOAT when the variable is FLOAT. */
static void store(Vm *vm, int dst, Operand a) {
    if (vm->p->vars[dst].type == TYPE_FLOAT) vm->vars[dst].f = opd_f(vm, a);
    else vm->vars[dst].i = opd_i(vm, a);
}

static long long ipow(long long base, long long e) {
    unsigned long long r = 1, b = (unsigned long long)base;
    while (e > 0) { if (e & 1) r *= b; b *= b; e >>= 1; }
    return (long long)r;
}

static int read_value(Vm *vm, const IRInstr *ins) {
    const IRVar *v = &vm->p->vars[ins->dst];
    char msg[160];
    if (ins->str >= 0) {
        fputs(vm->p->strs[ins->str], vm->out);
        fflush(vm->out);
    }
    if (v->type == TYPE_FLOAT) {
        int scanned = fscanf(vm->in, "%lf", &vm->vars[ins->dst].f);
        if (scanned != 1) {
            if (vm->interactive && feof(vm->in)) return 2;
            snprintf(msg, sizeof msg, "could not read a FLOAT value for '%s'", v->name);
            return fail(vm, ins->line, msg);
        }
    } else if (v->type == TYPE_BOOL) {
        char w[32];
        int scanned = fscanf(vm->in, "%31s", w);
        if (scanned == 1) {
            if (!strcmp(w, "TRUE") || !strcmp(w, "true") || !strcmp(w, "1")) { vm->vars[ins->dst].i = 1; return 0; }
            if (!strcmp(w, "FALSE") || !strcmp(w, "false") || !strcmp(w, "0")) { vm->vars[ins->dst].i = 0; return 0; }
        }
        if (scanned == EOF && vm->interactive && feof(vm->in)) return 2;
        snprintf(msg, sizeof msg, "could not read a BOOL value (TRUE/FALSE/1/0) for '%s'", v->name);
        return fail(vm, ins->line, msg);
    } else {
        int scanned = fscanf(vm->in, "%lld", &vm->vars[ins->dst].i);
        if (scanned != 1) {
            if (vm->interactive && feof(vm->in)) return 2;
            snprintf(msg, sizeof msg, "could not read an INT value for '%s'", v->name);
            return fail(vm, ins->line, msg);
        }
    }
    return 0;
}

static void print_item(Vm *vm, const IRInstr *ins) {
    if (ins->op == IR_PRINT && ins->a.kind == OPD_NONE && ins->str < 0) {
        fputc('\n', vm->out);
        return;
    }
    if (ins->str >= 0) {
        fputs(vm->p->strs[ins->str], vm->out);
    } else {
        DataType t = opd_type(vm->p, ins->a);
        if (t == TYPE_FLOAT) fprintf(vm->out, "%g", opd_f(vm, ins->a));
        else if (t == TYPE_BOOL) fputs(opd_i(vm, ins->a) ? "TRUE" : "FALSE", vm->out);
        else fprintf(vm->out, "%lld", opd_i(vm, ins->a));
    }
    if (ins->op == IR_PRINT) fputc('\n', vm->out);
    else if (ins->op == IR_WRITE) fputc(' ', vm->out);
}

/* One arithmetic or comparison instruction. Returns 1 on a run time error. */
static int exec_binary(Vm *vm, const IRInstr *ins) {
    DataType ot = ins->optype;
    int dst = ins->dst;
    if (ot == TYPE_FLOAT) {
        double a = opd_f(vm, ins->a), b = opd_f(vm, ins->b), r = 0;
        switch (ins->op) {
            case IR_ADD: r = a + b; break;
            case IR_SUB: r = a - b; break;
            case IR_MUL: r = a * b; break;
            case IR_DIV: if (b == 0.0) return fail(vm, ins->line, "division by zero"); r = a / b; break;
            case IR_POW: r = pow(a, b); break;
            case IR_LT: vm->vars[dst].i = a < b;  return 0;
            case IR_GT: vm->vars[dst].i = a > b;  return 0;
            case IR_LE: vm->vars[dst].i = a <= b; return 0;
            case IR_GE: vm->vars[dst].i = a >= b; return 0;
            case IR_EQ: vm->vars[dst].i = a == b; return 0;
            case IR_NE: vm->vars[dst].i = a != b; return 0;
            default: break;
        }
        if (vm->p->vars[dst].type == TYPE_FLOAT) vm->vars[dst].f = r; else vm->vars[dst].i = (long long)r;
        return 0;
    }
    long long a = opd_i(vm, ins->a), b = opd_i(vm, ins->b), r = 0;
    switch (ins->op) {
        case IR_ADD: r = (long long)((unsigned long long)a + (unsigned long long)b); break;
        case IR_SUB: r = (long long)((unsigned long long)a - (unsigned long long)b); break;
        case IR_MUL: r = (long long)((unsigned long long)a * (unsigned long long)b); break;
        case IR_DIV:
            if (b == 0) return fail(vm, ins->line, "integer division by zero");
            r = (b == -1) ? wrap_neg(a) : a / b;
            break;
        case IR_MOD:
            if (b == 0) return fail(vm, ins->line, "MOD by zero");
            r = (b == -1) ? 0 : a % b;
            break;
        case IR_POW:
            if (b < 0) return fail(vm, ins->line, "negative exponent in INT power (use a FLOAT base)");
            r = ipow(a, b);
            break;
        case IR_LT: r = a < b;  break;
        case IR_GT: r = a > b;  break;
        case IR_LE: r = a <= b; break;
        case IR_GE: r = a >= b; break;
        case IR_EQ: r = a == b; break;
        case IR_NE: r = a != b; break;
        default: break;
    }
    if (vm->p->vars[dst].type == TYPE_FLOAT) vm->vars[dst].f = (double)r; else vm->vars[dst].i = r;
    return 0;
}

int nova_interpret(const IRProgram *p, FILE *in, FILE *out, int interactive) {
    Vm vm = { p, (Cell *)xcalloc((size_t)(p->var_count ? p->var_count : 1), sizeof(Cell)), in, out, 0, interactive };

    /* label number -> instruction index */
    int *where = (int *)xcalloc((size_t)p->label_count + 2, sizeof(int));
    for (int i = 0; i < p->count; i++)
        if (p->code[i].op == IR_LABEL) where[p->code[i].label] = i;

    int pc = 0;
    while (pc < p->count && !vm.failed) {
        const IRInstr *ins = &p->code[pc++];
        switch (ins->op) {
            case IR_ASSIGN: store(&vm, ins->dst, ins->a); break;
            case IR_NEG:
                if (p->vars[ins->dst].type == TYPE_FLOAT) vm.vars[ins->dst].f = -opd_f(&vm, ins->a);
                else vm.vars[ins->dst].i = wrap_neg(opd_i(&vm, ins->a));
                break;
            case IR_NOT: vm.vars[ins->dst].i = !opd_i(&vm, ins->a); break;
            case IR_LABEL: case IR_NOP: break;
            case IR_GOTO: pc = where[ins->label]; break;
            case IR_IF_FALSE: if (!opd_i(&vm, ins->a)) pc = where[ins->label]; break;
            case IR_IF_TRUE:  if (opd_i(&vm, ins->a))  pc = where[ins->label]; break;
            case IR_INPUT: {
                int input_result = read_value(&vm, ins);
                if (input_result == 2) { fflush(out); free(where); free(vm.vars); return 2; }
                break;
            }
            case IR_PRINT: case IR_PRINT_CONT: case IR_WRITE: print_item(&vm, ins); break;
            case IR_CHKSTEP:
                if (opd_type(p, ins->a) == TYPE_FLOAT ? opd_f(&vm, ins->a) == 0.0 : opd_i(&vm, ins->a) == 0)
                    fail(&vm, ins->line, "FOR step is zero");
                break;
            default:
                if (ir_is_binary(ins->op)) exec_binary(&vm, ins);
                break;
        }
    }
    fflush(out);
    free(where);
    free(vm.vars);
    return vm.failed ? 1 : 0;
}
