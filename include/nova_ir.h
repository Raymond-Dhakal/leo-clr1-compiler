/*
 * nova_ir.h
 * Part of the Leo compiler extension - PHASE 4: intermediate code.
 *
 * The IR is a linear list of three-address instructions over typed
 * operands (no strings are pasted together to represent code):
 *
 *     t1 = a - b          arithmetic / comparison  (dst = a OP b)
 *     t2 = - a            unary                    (NEG, NOT)
 *     x  = t1             copy / constant load
 *     L1:                 label
 *     GOTO L1             unconditional jump
 *     IF_FALSE t3 GOTO L2 conditional jumps (also IF_TRUE)
 *     INPUT x / INPUT "prompt", x
 *     PRINT_NL x / PRINT_NL "text" value or text followed by a newline
 *     PRINT x / PRINT "text"      value or text with no trailing character
 *     WRITE x / WRITE "text"      value or text followed by a space (non final PRINT items)
 *     CHECK_STEP s                run time error if a FOR step is zero
 *
 * Variables are entries of IRProgram.vars: the first `user_var_count`
 * entries are the program's variables (index == semantic symbol id), the
 * rest are compiler temporaries (t1, t2 ...).
 */
#ifndef NOVA_IR_H
#define NOVA_IR_H

#include <stdio.h>
#include "nova_ast.h"
#include "nova_semantic.h"

typedef enum { OPD_NONE, OPD_VAR, OPD_INT, OPD_FLOAT, OPD_BOOL } OpdKind;

typedef struct {
    OpdKind kind;
    int var;        /* OPD_VAR: index into IRProgram.vars          */
    long long i;    /* OPD_INT value; OPD_BOOL value (0 or 1)      */
    double f;       /* OPD_FLOAT value                             */
} Operand;

typedef enum {
    IR_ASSIGN,                                   /* dst = a            */
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD, IR_POW,
    IR_LT, IR_GT, IR_LE, IR_GE, IR_EQ, IR_NE,    /* dst = a OP b       */
    IR_NEG, IR_NOT,                              /* dst = OP a         */
    IR_LABEL,                                    /* label:             */
    IR_GOTO,                                     /* GOTO label         */
    IR_IF_FALSE, IR_IF_TRUE,                     /* IF_x a GOTO label  */
    IR_INPUT,                                    /* INPUT dst          */
    IR_PRINT,                                    /* PRINT_NL a or PRINT_NL "str" (then newline) */
    IR_PRINT_CONT,                               /* PRINT a or PRINT "str" (no trailing character) */
    IR_WRITE,                                    /* WRITE a  or  WRITE "str"  (then a space) */
    IR_CHKSTEP,                                  /* CHECK_STEP a: fail if a == 0 */
    IR_NOP                                       /* removed by the optimizer */
} IROp;

typedef struct {
    IROp op;
    int dst;            /* variable index, or -1                         */
    Operand a, b;
    int label;          /* LABEL / GOTO / IF_*: label number             */
    DataType optype;    /* arithmetic domain: TYPE_INT, TYPE_FLOAT, or TYPE_BOOL (== / != on BOOLs) */
    int line;           /* source line (used in run-time error messages) */
    int str;            /* index into IRProgram.strs: INPUT prompt, or the text of PRINT/WRITE; -1 if none */
} IRInstr;

typedef struct {
    char *name;         /* unique display name (also used for the C identifier) */
    DataType type;
    int is_temp;
} IRVar;

typedef struct {
    IRInstr *code;
    int count, capacity;
    IRVar *vars;
    int var_count, var_capacity;
    int user_var_count;
    int label_count;    /* labels are numbered 1..label_count */
    int temp_counter;
    char **strs;        /* string pool: prompts and PRINT texts (decoded) */
    int str_count, str_capacity;
} IRProgram;

void ir_init(IRProgram *p);
void ir_free(IRProgram *p);

/* Lowers a checked AST (all types and symbol ids filled in) to IR. */
void ir_generate(const AstNode *program, const SymTab *st, IRProgram *out);

void ir_print(const IRProgram *p, FILE *out);
/* Text of one instruction without indentation, e.g. "t1 = a - b". */
void ir_format_instr(const IRProgram *p, const IRInstr *ins, char *buf, size_t n);

/* Builders shared with the optimizer. */
Operand opd_var(int v);
Operand opd_int(long long v);
Operand opd_float(double v);
Operand opd_bool(int v);
Operand opd_none(void);
int opd_is_const(Operand o);
DataType opd_type(const IRProgram *p, Operand o);

int ir_new_temp(IRProgram *p, DataType type);
int ir_new_label(IRProgram *p);
int ir_add_string(IRProgram *p, const char *s);   /* returns its pool index */
void ir_emit(IRProgram *p, IRInstr ins);
IRInstr ir_instr(IROp op, int dst, Operand a, Operand b, DataType optype, int line);

/* Classification helpers. */
int ir_is_binary(IROp op);          /* ADD .. NE */
int ir_defines(const IRInstr *ins); /* returns dst var index or -1 */
int ir_uses(const IRInstr *ins, int out[2]);   /* var indexes read; returns count */
const char *ir_op_text(IROp op);

#endif /* NOVA_IR_H */
