/*
 * nova_codegen.c - PHASE 6: C code generation.
 *
 * Mapping of Leo to C:
 *   INT   -> long long        (64-bit, wraps on overflow: built with -fwrapv)
 *   FLOAT -> double
 *   BOOL  -> int (0 / 1)
 *   every IR variable becomes a C local in main(), initialised to zero
 *   labels / GOTO / IF_x      -> C labels, goto, if (...) goto
 * Operations that can fail (division, MOD, INT power, INPUT) call small
 * helper functions that print "runtime error (line N): ..." and exit(1).
 * Only the helpers actually used are emitted.
 */
#include <limits.h>
#include <string.h>
#include "nova_codegen.h"
#include "xalloc.h"

typedef struct {
    const IRProgram *p;
    int need_fail, need_div_int, need_mod_int, need_div_float, need_pow_int;
    int need_in_int, need_in_float, need_in_bool, need_math, need_string;
} Cg;

static const char *c_type(DataType t) {
    switch (t) {
        case TYPE_FLOAT: return "double";
        case TYPE_BOOL:  return "int";
        default:         return "long long";
    }
}

/* C identifier of IR variable v. User variables get a v_ prefix so they can
   never collide with C keywords, library names or the temporaries. */
static void c_name(const IRProgram *p, int v, char *buf, size_t n) {
    if (p->vars[v].is_temp) snprintf(buf, n, "%s", p->vars[v].name);
    else snprintf(buf, n, "v_%s", p->vars[v].name);
}

/* Operand as a C expression whose value has type `want` (INT operands are
   converted when a FLOAT is wanted). */
static void c_operand(const IRProgram *p, Operand o, DataType want, char *buf, size_t n) {
    char tmp[96];
    switch (o.kind) {
        case OPD_VAR:
            c_name(p, o.var, tmp, sizeof(tmp));
            if (want == TYPE_FLOAT && p->vars[o.var].type == TYPE_INT) snprintf(buf, n, "(double)%s", tmp);
            else snprintf(buf, n, "%s", tmp);
            break;
        case OPD_INT:
            if (want == TYPE_FLOAT) {
                nova_fmt_double_c((double)o.i, tmp, sizeof(tmp));
                snprintf(buf, n, o.i < 0 ? "(%s)" : "%s", tmp);
            } else if (o.i == LLONG_MIN) {
                snprintf(buf, n, "(-9223372036854775807LL - 1)");
            } else if (o.i < 0) {
                snprintf(buf, n, "(%lldLL)", o.i);
            } else {
                snprintf(buf, n, "%lldLL", o.i);
            }
            break;
        case OPD_FLOAT:
            nova_fmt_double_c(o.f, tmp, sizeof(tmp));
            snprintf(buf, n, (o.f < 0 || (o.f == 0 && tmp[0] == '-')) ? "(%s)" : "%s", tmp);
            break;
        case OPD_BOOL:
            snprintf(buf, n, "%d", (int)o.i);
            break;
        default:
            snprintf(buf, n, "0");
    }
}

static void emit_instr(Cg *cg, StrBuf *body, const IRInstr *ins) {
    const IRProgram *p = cg->p;
    char text[256], d[96], a[128], b[128], an[128];
    ir_format_instr(p, ins, text, sizeof(text));

    if (ins->op == IR_LABEL) {
        strbuf_printf(body, "L%d:;\n", ins->label);
        return;
    }
    strbuf_printf(body, "    // %s\n    ", text);

    if (ins->dst >= 0) c_name(p, ins->dst, d, sizeof(d));
    DataType ot = ins->optype;

    switch (ins->op) {
        case IR_ASSIGN:
            c_operand(p, ins->a, p->vars[ins->dst].type, a, sizeof(a));
            strbuf_printf(body, "%s = %s;\n", d, a);
            break;
        case IR_NEG:
            c_operand(p, ins->a, ot, a, sizeof(a));
            strbuf_printf(body, "%s = -%s;\n", d, a);
            break;
        case IR_NOT:
            c_operand(p, ins->a, TYPE_BOOL, a, sizeof(a));
            strbuf_printf(body, "%s = !%s;\n", d, a);
            break;
        case IR_ADD: case IR_SUB: case IR_MUL: {
            c_operand(p, ins->a, ot, a, sizeof(a));
            c_operand(p, ins->b, ot, b, sizeof(b));
            strbuf_printf(body, "%s = %s %s %s;\n", d, a, ir_op_text(ins->op), b);
            break;
        }
        case IR_DIV:
            c_operand(p, ins->a, ot, a, sizeof(a));
            c_operand(p, ins->b, ot, b, sizeof(b));
            cg->need_fail = 1;
            if (ot == TYPE_FLOAT) { cg->need_div_float = 1; strbuf_printf(body, "%s = nv_div_float(%s, %s, %d);\n", d, a, b, ins->line); }
            else { cg->need_div_int = 1; strbuf_printf(body, "%s = nv_div_int(%s, %s, %d);\n", d, a, b, ins->line); }
            break;
        case IR_MOD:
            c_operand(p, ins->a, TYPE_INT, a, sizeof(a));
            c_operand(p, ins->b, TYPE_INT, b, sizeof(b));
            cg->need_fail = cg->need_mod_int = 1;
            strbuf_printf(body, "%s = nv_mod_int(%s, %s, %d);\n", d, a, b, ins->line);
            break;
        case IR_POW:
            c_operand(p, ins->a, ot, a, sizeof(a));
            c_operand(p, ins->b, ot, b, sizeof(b));
            if (ot == TYPE_FLOAT) { cg->need_math = 1; strbuf_printf(body, "%s = pow(%s, %s);\n", d, a, b); }
            else { cg->need_fail = cg->need_pow_int = 1; strbuf_printf(body, "%s = nv_pow_int(%s, %s, %d);\n", d, a, b, ins->line); }
            break;
        case IR_LT: case IR_GT: case IR_LE: case IR_GE: case IR_EQ: case IR_NE:
            c_operand(p, ins->a, ot, a, sizeof(a));
            c_operand(p, ins->b, ot, b, sizeof(b));
            strbuf_printf(body, "%s = (%s %s %s);\n", d, a, ir_op_text(ins->op), b);
            break;
        case IR_GOTO:
            strbuf_printf(body, "goto L%d;\n", ins->label);
            break;
        case IR_IF_FALSE:
            c_operand(p, ins->a, TYPE_BOOL, a, sizeof(a));
            strbuf_printf(body, "if (!%s) goto L%d;\n", a, ins->label);
            break;
        case IR_IF_TRUE:
            c_operand(p, ins->a, TYPE_BOOL, a, sizeof(a));
            strbuf_printf(body, "if (%s) goto L%d;\n", a, ins->label);
            break;
        case IR_INPUT: {
            DataType t = p->vars[ins->dst].type;
            snprintf(an, sizeof(an), "%s", p->vars[ins->dst].name);
            cg->need_fail = 1;
            if (ins->str >= 0) {   /* prompt: shown before waiting for the value */
                strbuf_append(body, "fputs(");
                strbuf_append_quoted(body, p->strs[ins->str], 1);
                strbuf_append(body, ", stdout); fflush(stdout);\n");
            }
            if (t == TYPE_FLOAT)     { cg->need_in_float = 1; strbuf_printf(body, "nv_input_float(&%s, \"%s\", %d);\n", d, an, ins->line); }
            else if (t == TYPE_BOOL) { cg->need_in_bool = cg->need_string = 1; strbuf_printf(body, "nv_input_bool(&%s, \"%s\", %d);\n", d, an, ins->line); }
            else                     { cg->need_in_int = 1; strbuf_printf(body, "nv_input_int(&%s, \"%s\", %d);\n", d, an, ins->line); }
            break;
        }
        case IR_PRINT:
        case IR_PRINT_CONT:
        case IR_WRITE: {
            if (ins->op == IR_PRINT && ins->a.kind == OPD_NONE && ins->str < 0) {
                strbuf_append(body, "putchar('\\n');\n");
                break;
            }
            const char *tail = ins->op == IR_PRINT ? "\\n" : ins->op == IR_WRITE ? " " : "";
            if (ins->str >= 0) {
                strbuf_append(body, "fputs(");
                strbuf_append_quoted(body, p->strs[ins->str], 1);
                strbuf_append(body, ", stdout);\n");
                if (ins->op == IR_PRINT) strbuf_append(body, "putchar('\\n');\n");
                else if (ins->op == IR_WRITE) strbuf_append(body, "putchar(' ');\n");
                break;
            }
            DataType t = opd_type(p, ins->a);
            c_operand(p, ins->a, t, a, sizeof(a));
            if (t == TYPE_FLOAT)     strbuf_printf(body, "printf(\"%%g%s\", %s);\n", tail, a);
            else if (t == TYPE_BOOL) strbuf_printf(body, "printf(\"%%s%s\", %s ? \"TRUE\" : \"FALSE\");\n", tail, a);
            else                     strbuf_printf(body, "printf(\"%%lld%s\", %s);\n", tail, a);
            break;
        }
        case IR_CHKSTEP: {
            DataType t = opd_type(p, ins->a);
            c_operand(p, ins->a, t, a, sizeof(a));
            cg->need_fail = 1;
            strbuf_printf(body, "if (%s == %s) nv_fail(%d, \"FOR step is zero\");\n", a, t == TYPE_FLOAT ? "0.0" : "0LL", ins->line);
            break;
        }
        default:
            strbuf_printf(body, ";\n");
    }
}

static void emit_helpers(const Cg *cg, StrBuf *out) {
    if (cg->need_fail)
        strbuf_append(out,
            "static void nv_fail(int line, const char *msg) {\n"
            "    fflush(stdout);\n"
            "    fprintf(stderr, \"runtime error (line %d): %s\\n\", line, msg);\n"
            "    exit(1);\n"
            "}\n\n");
    if (cg->need_div_int)
        strbuf_append(out,
            "static long long nv_div_int(long long a, long long b, int line) {\n"
            "    if (b == 0) nv_fail(line, \"integer division by zero\");\n"
            "    if (b == -1) return (long long)(0ULL - (unsigned long long)a);\n"
            "    return a / b;\n"
            "}\n\n");
    if (cg->need_mod_int)
        strbuf_append(out,
            "static long long nv_mod_int(long long a, long long b, int line) {\n"
            "    if (b == 0) nv_fail(line, \"MOD by zero\");\n"
            "    if (b == -1) return 0;\n"
            "    return a % b;\n"
            "}\n\n");
    if (cg->need_div_float)
        strbuf_append(out,
            "static double nv_div_float(double a, double b, int line) {\n"
            "    if (b == 0.0) nv_fail(line, \"division by zero\");\n"
            "    return a / b;\n"
            "}\n\n");
    if (cg->need_pow_int)
        strbuf_append(out,
            "static long long nv_pow_int(long long base, long long e, int line) {\n"
            "    unsigned long long r = 1, b = (unsigned long long)base;\n"
            "    if (e < 0) nv_fail(line, \"negative exponent in INT power (use a FLOAT base)\");\n"
            "    while (e > 0) { if (e & 1) r *= b; b *= b; e >>= 1; }\n"
            "    return (long long)r;\n"
            "}\n\n");
    if (cg->need_in_int)
        strbuf_append(out,
            "static void nv_input_int(long long *dst, const char *name, int line) {\n"
            "    char msg[96];\n"
            "    if (scanf(\"%lld\", dst) != 1) {\n"
            "        snprintf(msg, sizeof msg, \"could not read an INT value for '%s'\", name);\n"
            "        nv_fail(line, msg);\n"
            "    }\n"
            "}\n\n");
    if (cg->need_in_float)
        strbuf_append(out,
            "static void nv_input_float(double *dst, const char *name, int line) {\n"
            "    char msg[96];\n"
            "    if (scanf(\"%lf\", dst) != 1) {\n"
            "        snprintf(msg, sizeof msg, \"could not read a FLOAT value for '%s'\", name);\n"
            "        nv_fail(line, msg);\n"
            "    }\n"
            "}\n\n");
    if (cg->need_in_bool)
        strbuf_append(out,
            "static void nv_input_bool(int *dst, const char *name, int line) {\n"
            "    char w[32], msg[96];\n"
            "    if (scanf(\"%31s\", w) == 1) {\n"
            "        if (!strcmp(w, \"TRUE\") || !strcmp(w, \"true\") || !strcmp(w, \"1\")) { *dst = 1; return; }\n"
            "        if (!strcmp(w, \"FALSE\") || !strcmp(w, \"false\") || !strcmp(w, \"0\")) { *dst = 0; return; }\n"
            "    }\n"
            "    snprintf(msg, sizeof msg, \"could not read a BOOL value (TRUE/FALSE/1/0) for '%s'\", name);\n"
            "    nv_fail(line, msg);\n"
            "}\n\n");
}

void codegen_c(const IRProgram *p, const char *source_name, StrBuf *out) {
    Cg cg;
    memset(&cg, 0, sizeof(cg));
    cg.p = p;

    /* 1. translate the body first: this tells us which helpers are needed */
    StrBuf body;
    strbuf_init(&body);
    for (int i = 0; i < p->count; i++) emit_instr(&cg, &body, &p->code[i]);

    /* 2. which variables does the code actually mention? */
    char *used = (char *)xcalloc((size_t)(p->var_count ? p->var_count : 1), 1);
    for (int i = 0; i < p->count; i++) {
        int d = ir_defines(&p->code[i]);
        if (d >= 0) used[d] = 1;
        int u[2], n = ir_uses(&p->code[i], u);
        for (int k = 0; k < n; k++) used[u[k]] = 1;
    }

    strbuf_printf(out,
        "/* Generated by leoc from %s - do not edit.\n"
        " * Build: gcc -O2 -fwrapv -o program program.c -lm\n"
        " * Leo INT is 64-bit and wraps on overflow, hence -fwrapv. */\n"
        "#include <stdio.h>\n#include <stdlib.h>\n", source_name);
    if (cg.need_string) strbuf_append(out, "#include <string.h>\n");
    if (cg.need_math)   strbuf_append(out, "#include <math.h>\n");
    strbuf_append(out, "\n");
    emit_helpers(&cg, out);

    strbuf_append(out, "int main(void) {\n");
    for (int v = 0; v < p->var_count; v++) {
        if (!used[v]) continue;
        char name[96];
        c_name(p, v, name, sizeof(name));
        strbuf_printf(out, "    %s %s = %s;\n", c_type(p->vars[v].type), name,
                      p->vars[v].type == TYPE_FLOAT ? "0.0" : "0");
    }
    strbuf_append(out, "\n");
    strbuf_append(out, body.data);
    strbuf_append(out, "\n    return 0;\n}\n");

    free(used);
    strbuf_free(&body);
}
