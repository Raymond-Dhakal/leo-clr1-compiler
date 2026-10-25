/*
 * nova_ir.c - PHASE 4: intermediate code generation.
 *
 * Lowering rules (naive on purpose: the optimizer cleans up afterwards):
 *   expression      -> temporaries, one instruction per operator
 *   a ~ b           -> IR_SUB            (the custom operator disappears here)
 *   a AND b, a OR b -> short-circuit jumps, so  x != 0 AND 10 / x > 1  is safe
 *   IF / WHILE / REPEAT -> labels and IF_FALSE / GOTO
 */
#include <string.h>
#include "nova_ir.h"
#include "nova_util.h"
#include "xalloc.h"

/* ---------------------------------------------------------------------
 * Basic construction helpers
 * --------------------------------------------------------------------- */

void ir_init(IRProgram *p) { memset(p, 0, sizeof(*p)); }

void ir_free(IRProgram *p) {
    for (int i = 0; i < p->var_count; i++) free(p->vars[i].name);
    free(p->vars);
    free(p->code);
    for (int i = 0; i < p->str_count; i++) free(p->strs[i]);
    free(p->strs);
    memset(p, 0, sizeof(*p));
}

Operand opd_none(void)        { Operand o; memset(&o, 0, sizeof(o)); o.kind = OPD_NONE; o.var = -1; return o; }
Operand opd_var(int v)        { Operand o = opd_none(); o.kind = OPD_VAR;   o.var = v; return o; }
Operand opd_int(long long v)  { Operand o = opd_none(); o.kind = OPD_INT;   o.i = v;   return o; }
Operand opd_float(double v)   { Operand o = opd_none(); o.kind = OPD_FLOAT; o.f = v;   return o; }
Operand opd_bool(int v)       { Operand o = opd_none(); o.kind = OPD_BOOL;  o.i = v ? 1 : 0; return o; }

int opd_is_const(Operand o) { return o.kind == OPD_INT || o.kind == OPD_FLOAT || o.kind == OPD_BOOL; }

DataType opd_type(const IRProgram *p, Operand o) {
    switch (o.kind) {
        case OPD_VAR:   return p->vars[o.var].type;
        case OPD_INT:   return TYPE_INT;
        case OPD_FLOAT: return TYPE_FLOAT;
        case OPD_BOOL:  return TYPE_BOOL;
        default:        return TYPE_UNKNOWN;
    }
}

IRInstr ir_instr(IROp op, int dst, Operand a, Operand b, DataType optype, int line) {
    IRInstr ins;
    memset(&ins, 0, sizeof(ins));
    ins.op = op;
    ins.dst = dst;
    ins.a = a;
    ins.b = b;
    ins.label = 0;
    ins.optype = optype;
    ins.line = line;
    ins.str = -1;
    return ins;
}

void ir_emit(IRProgram *p, IRInstr ins) {
    NOVA_GROW(p->code, p->count, p->capacity);
    p->code[p->count++] = ins;
}

static int name_in_use(const IRProgram *p, const char *name) {
    for (int i = 0; i < p->var_count; i++) if (strcmp(p->vars[i].name, name) == 0) return 1;
    return 0;
}

static int add_var(IRProgram *p, const char *name, DataType type, int is_temp) {
    NOVA_GROW(p->vars, p->var_count, p->var_capacity);
    int id = p->var_count++;
    p->vars[id].name = xstrdup(name);
    p->vars[id].type = type;
    p->vars[id].is_temp = is_temp;
    return id;
}

int ir_new_temp(IRProgram *p, DataType type) {
    char name[32];
    do {
        snprintf(name, sizeof(name), "t%d", ++p->temp_counter);
    } while (name_in_use(p, name));   /* never collide with a user variable named t1 */
    return add_var(p, name, type, 1);
}

int ir_new_label(IRProgram *p) { return ++p->label_count; }

int ir_add_string(IRProgram *p, const char *s) {
    NOVA_GROW(p->strs, p->str_count, p->str_capacity);
    p->strs[p->str_count] = xstrdup(s);
    return p->str_count++;
}

/* ---------------------------------------------------------------------
 * Instruction classification
 * --------------------------------------------------------------------- */

int ir_is_binary(IROp op) { return op >= IR_ADD && op <= IR_NE; }

int ir_defines(const IRInstr *ins) {
    switch (ins->op) {
        case IR_ASSIGN: case IR_NEG: case IR_NOT: case IR_INPUT:
            return ins->dst;
        default:
            return ir_is_binary(ins->op) ? ins->dst : -1;
    }
}

int ir_uses(const IRInstr *ins, int out[2]) {
    int n = 0;
    if (ir_is_binary(ins->op)) {
        if (ins->a.kind == OPD_VAR) out[n++] = ins->a.var;
        if (ins->b.kind == OPD_VAR) out[n++] = ins->b.var;
    } else if (ins->op == IR_ASSIGN || ins->op == IR_NEG || ins->op == IR_NOT ||
               ins->op == IR_IF_FALSE || ins->op == IR_IF_TRUE || ins->op == IR_PRINT ||
               ins->op == IR_WRITE || ins->op == IR_PRINT_CONT || ins->op == IR_CHKSTEP) {
        if (ins->a.kind == OPD_VAR) out[n++] = ins->a.var;
    }
    return n;
}

const char *ir_op_text(IROp op) {
    switch (op) {
        case IR_ADD: return "+";   case IR_SUB: return "-";   case IR_MUL: return "*";
        case IR_DIV: return "/";   case IR_MOD: return "MOD"; case IR_POW: return "^";
        case IR_LT:  return "<";   case IR_GT:  return ">";   case IR_LE:  return "<=";
        case IR_GE:  return ">=";  case IR_EQ:  return "==";  case IR_NE:  return "!=";
        default: return "?";
    }
}

/* ---------------------------------------------------------------------
 * Printing
 * --------------------------------------------------------------------- */

static void format_operand(const IRProgram *p, Operand o, char *buf, size_t n) {
    switch (o.kind) {
        case OPD_VAR:   snprintf(buf, n, "%s", p->vars[o.var].name); break;
        case OPD_INT:   snprintf(buf, n, "%lld", o.i); break;
        case OPD_FLOAT: nova_fmt_double_display(o.f, buf, n); break;
        case OPD_BOOL:  snprintf(buf, n, "%s", o.i ? "TRUE" : "FALSE"); break;
        default:        snprintf(buf, n, "?"); break;
    }
}

void ir_format_instr(const IRProgram *p, const IRInstr *ins, char *buf, size_t n) {
    char a[64], b[64];
    format_operand(p, ins->a, a, sizeof(a));
    format_operand(p, ins->b, b, sizeof(b));
    const char *d = (ins->dst >= 0) ? p->vars[ins->dst].name : "?";
    switch (ins->op) {
        case IR_ASSIGN:   snprintf(buf, n, "%s = %s", d, a); break;
        case IR_NEG:      snprintf(buf, n, "%s = - %s", d, a); break;
        case IR_NOT:      snprintf(buf, n, "%s = NOT %s", d, a); break;
        case IR_LABEL:    snprintf(buf, n, "L%d:", ins->label); break;
        case IR_GOTO:     snprintf(buf, n, "GOTO L%d", ins->label); break;
        case IR_IF_FALSE: snprintf(buf, n, "IF_FALSE %s GOTO L%d", a, ins->label); break;
        case IR_IF_TRUE:  snprintf(buf, n, "IF_TRUE %s GOTO L%d", a, ins->label); break;
        case IR_INPUT:
        case IR_PRINT:
        case IR_PRINT_CONT:
        case IR_WRITE: {
            if (ins->op == IR_PRINT && ins->a.kind == OPD_NONE && ins->str < 0) {
                snprintf(buf, n, "PRINTN");
                break;
            }
            StrBuf q; strbuf_init(&q);
            if (ins->str >= 0) strbuf_append_quoted(&q, p->strs[ins->str], 0);
            if (ins->op == IR_INPUT) {
                if (ins->str >= 0) snprintf(buf, n, "INPUT %s, %s", q.data, d);
                else snprintf(buf, n, "INPUT %s", d);
            } else {
                const char *name = ins->op == IR_PRINT ? "PRINTN" :
                                   ins->op == IR_PRINT_CONT ? "PRINT" : "WRITE";
                snprintf(buf, n, "%s %s", name, ins->str >= 0 ? q.data : a);
            }
            strbuf_free(&q);
            break;
        }
        case IR_CHKSTEP:  snprintf(buf, n, "CHECK_STEP %s", a); break;
        case IR_NOP:      snprintf(buf, n, "NOP"); break;
        default:          snprintf(buf, n, "%s = %s %s %s", d, a, ir_op_text(ins->op), b); break;
    }
}

void ir_print(const IRProgram *p, FILE *out) {
    for (int i = 0; i < p->count; i++) {
        char text[256];
        ir_format_instr(p, &p->code[i], text, sizeof(text));
        if (p->code[i].op == IR_LABEL) fprintf(out, "%s\n", text);
        else fprintf(out, "    %s\n", text);
    }
}

/* ---------------------------------------------------------------------
 * Lowering from the AST
 * --------------------------------------------------------------------- */

typedef struct {
    IRProgram *ir;
    int loop_depth;    /* > 0 while generating the body of a WHILE / REPEAT */
} Gen;

static Operand gen_expr(Gen *g, const AstNode *n);
static void gen_block(Gen *g, const AstNode *block);

static void emit_label(Gen *g, int label) {
    IRInstr l = ir_instr(IR_LABEL, -1, opd_none(), opd_none(), TYPE_UNKNOWN, 0);
    l.label = label;
    ir_emit(g->ir, l);
}

static void emit_jump(Gen *g, IROp op, Operand cond, int label, int line) {
    IRInstr j = ir_instr(op, -1, cond, opd_none(), TYPE_UNKNOWN, line);
    j.label = label;
    ir_emit(g->ir, j);
}

static IROp irop_of_binop(BinOp op) {
    switch (op) {
        case OP_ADD: return IR_ADD;  case OP_SUB: return IR_SUB;  /* '~' becomes plain subtraction */
        case OP_MUL: return IR_MUL;  case OP_DIV: return IR_DIV;
        case OP_MOD: return IR_MOD;  case OP_POW: return IR_POW;
        case OP_LT:  return IR_LT;   case OP_GT:  return IR_GT;
        case OP_LE:  return IR_LE;   case OP_GE:  return IR_GE;
        case OP_EQ:  return IR_EQ;   default:     return IR_NE;
    }
}

/* a AND b  /  a OR b with short-circuit evaluation:
 *     t = a
 *     IF_FALSE t GOTO Lend      (IF_TRUE for OR)
 *     t = b
 *   Lend:                                                              */
static Operand gen_logical(Gen *g, const AstNode *n) {
    IRProgram *ir = g->ir;
    int is_and = (n->u.binary.op == OP_AND);
    Operand left = gen_expr(g, n->u.binary.left);
    int t = ir_new_temp(ir, TYPE_BOOL);
    int end = ir_new_label(ir);
    ir_emit(ir, ir_instr(IR_ASSIGN, t, left, opd_none(), TYPE_BOOL, n->line));
    emit_jump(g, is_and ? IR_IF_FALSE : IR_IF_TRUE, opd_var(t), end, n->line);
    Operand right = gen_expr(g, n->u.binary.right);
    ir_emit(ir, ir_instr(IR_ASSIGN, t, right, opd_none(), TYPE_BOOL, n->line));
    emit_label(g, end);
    return opd_var(t);
}

static Operand gen_expr(Gen *g, const AstNode *n) {
    IRProgram *ir = g->ir;
    switch (n->kind) {
        case AST_INT_LIT:   return opd_int(n->u.ival);
        case AST_FLOAT_LIT: return opd_float(n->u.fval);
        case AST_BOOL_LIT:  return opd_bool(n->u.bval);
        case AST_VAR:       return opd_var(n->u.var.sym);
        case AST_UNARY: {
            Operand a = gen_expr(g, n->u.unary.operand);
            int t = ir_new_temp(ir, n->type);
            if (n->u.unary.op == UOP_NEG)
                ir_emit(ir, ir_instr(IR_NEG, t, a, opd_none(), n->type, n->line));
            else
                ir_emit(ir, ir_instr(IR_NOT, t, a, opd_none(), TYPE_BOOL, n->line));
            return opd_var(t);
        }
        case AST_BINARY: {
            BinOp op = n->u.binary.op;
            if (op == OP_AND || op == OP_OR) return gen_logical(g, n);
            Operand a = gen_expr(g, n->u.binary.left);
            Operand b = gen_expr(g, n->u.binary.right);
            DataType lt = n->u.binary.left->type, rt = n->u.binary.right->type;
            DataType optype;
            if (op == OP_EQ || op == OP_NE || op == OP_LT || op == OP_GT || op == OP_LE || op == OP_GE) {
                optype = (lt == TYPE_BOOL) ? TYPE_BOOL
                       : (lt == TYPE_FLOAT || rt == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
            } else {
                optype = n->type;   /* arithmetic is performed in the result type */
            }
            int t = ir_new_temp(ir, n->type);
            ir_emit(ir, ir_instr(irop_of_binop(op), t, a, b, optype, n->line));
            return opd_var(t);
        }
        default:
            return opd_none();
    }
}

static Operand zero_of(DataType t) {
    if (t == TYPE_FLOAT) return opd_float(0.0);
    if (t == TYPE_BOOL) return opd_bool(0);
    return opd_int(0);
}

/* A value that must not change while the loop runs: user variables are
   copied into a temporary, constants and temporaries are used as they are. */
static Operand freeze(Gen *g, Operand v, int line) {
    if (v.kind != OPD_VAR || g->ir->vars[v.var].is_temp) return v;
    int t = ir_new_temp(g->ir, g->ir->vars[v.var].type);
    ir_emit(g->ir, ir_instr(IR_ASSIGN, t, v, opd_none(), TYPE_UNKNOWN, line));
    return opd_var(t);
}

static DataType cmp_domain(DataType a, DataType b) {
    return (a == TYPE_FLOAT || b == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
}

/*
 *   FOR i <- a TO b STEP s        i = a ; lim = b ; stp = s
 *                                 [CHECK_STEP stp]            (only if s is not a constant)
 *                             Ltop:
 *                                 continue = (s > 0) ? i <= lim : i >= lim
 *                                 IF_FALSE continue GOTO Lend
 *                                 body
 *                                 i = i + stp ; GOTO Ltop
 *                             Lend:
 * a, b and s are evaluated once, in that order, before the first test (as in
 * QBASIC). With a constant step the direction is known and only one
 * comparison is emitted.
 */
/* Does the statement list (recursively) assign to variable `sym`? */
static int assigns_var(const AstNode *n, int sym) {
    if (!n) return 0;
    switch (n->kind) {
        case AST_PROGRAM: case AST_BLOCK:
            for (int i = 0; i < n->u.block.count; i++) if (assigns_var(n->u.block.stmts[i], sym)) return 1;
            return 0;
        case AST_ASSIGN: return n->u.assign.sym == sym;
        case AST_INPUT:  return n->u.input.sym == sym;
        case AST_DECL:   return n->u.decl.sym == sym;
        case AST_IF:     return assigns_var(n->u.if_stmt.then_block, sym) || assigns_var(n->u.if_stmt.else_block, sym);
        case AST_WHILE: case AST_REPEAT: return assigns_var(n->u.loop.body, sym);
        case AST_FOR:    return n->u.for_stmt.sym == sym || assigns_var(n->u.for_stmt.body, sym);
        default: return 0;
    }
}

/* True when `i + step` provably cannot wrap: constant limit and step with
   limit + step in range, and the body never assigns the loop variable. */
static int overflow_impossible(const AstNode *n, Operand limit, Operand step, int step_const, int var) {
    if (!step_const || limit.kind != OPD_INT || step.kind != OPD_INT) return 0;
    if (assigns_var(n->u.for_stmt.body, var)) return 0;
    long long sum;
    return !__builtin_add_overflow(limit.i, step.i, &sum);
}

static void gen_for(Gen *g, const AstNode *n) {
    IRProgram *ir = g->ir;
    int line = n->line;
    int var = n->u.for_stmt.sym;
    DataType vt = ir->vars[var].type;

    Operand start = gen_expr(g, n->u.for_stmt.start);
    Operand limit = freeze(g, gen_expr(g, n->u.for_stmt.limit), line);
    DataType lt = opd_type(ir, limit);

    int is_float = 0, have_const = 0; long long civ = 1; double cfv = 1.0;
    Operand step;
    if (!n->u.for_stmt.step) {
        step = (vt == TYPE_FLOAT) ? opd_float(1.0) : opd_int(1);
        have_const = 1; is_float = (vt == TYPE_FLOAT);
    } else if (ast_const_number(n->u.for_stmt.step, &is_float, &civ, &cfv)) {
        step = is_float ? opd_float(cfv) : opd_int(civ);
        have_const = 1;
    } else {
        step = freeze(g, gen_expr(g, n->u.for_stmt.step), line);
    }
    DataType st = opd_type(ir, step);
    /* All three values are known: only now does the loop variable change. */
    ir_emit(ir, ir_instr(IR_ASSIGN, var, start, opd_none(), TYPE_UNKNOWN, line));
    if (!have_const) ir_emit(ir, ir_instr(IR_CHKSTEP, -1, step, opd_none(), TYPE_UNKNOWN, line));

    int l_top = ir_new_label(ir), l_end = ir_new_label(ir);
    emit_label(g, l_top);
    DataType cd = cmp_domain(vt, lt);
    int cont = ir_new_temp(ir, TYPE_BOOL);
    if (have_const) {
        int up = is_float ? (cfv > 0.0) : (civ > 0);
        ir_emit(ir, ir_instr(up ? IR_LE : IR_GE, cont, opd_var(var), limit, cd, line));
    } else {
        int l_down = ir_new_label(ir), l_test = ir_new_label(ir);
        int pos = ir_new_temp(ir, TYPE_BOOL);
        ir_emit(ir, ir_instr(IR_GT, pos, step, (st == TYPE_FLOAT) ? opd_float(0.0) : opd_int(0), st, line));
        emit_jump(g, IR_IF_FALSE, opd_var(pos), l_down, line);
        ir_emit(ir, ir_instr(IR_LE, cont, opd_var(var), limit, cd, line));
        emit_jump(g, IR_GOTO, opd_none(), l_test, line);
        emit_label(g, l_down);
        ir_emit(ir, ir_instr(IR_GE, cont, opd_var(var), limit, cd, line));
        emit_label(g, l_test);
    }
    emit_jump(g, IR_IF_FALSE, opd_var(cont), l_end, line);

    g->loop_depth++;
    gen_block(g, n->u.for_stmt.body);
    g->loop_depth--;

    DataType ad = cmp_domain(vt, st);
    int next = ir_new_temp(ir, vt);
    ir_emit(ir, ir_instr(IR_ADD, next, opd_var(var), step, ad, line));
    if (vt == TYPE_INT && !overflow_impossible(n, limit, step, have_const, var)) {
        /* INT arithmetic wraps: i + step passed the largest/smallest INT exactly when the
           sum moved against the direction of the step. Stop the loop then, instead of
           wrapping around and running (almost) forever. */
        int below = ir_new_temp(ir, TYPE_BOOL);
        int wrapped = below;
        ir_emit(ir, ir_instr(IR_LT, below, opd_var(next), opd_var(var), TYPE_INT, line));
        if (have_const) {
            /* up: wrapped == (next < i);  down: wrapped == (next > i) == NOT (next < i) */
            if (civ <= 0) {
                wrapped = ir_new_temp(ir, TYPE_BOOL);
                ir_emit(ir, ir_instr(IR_NOT, wrapped, opd_var(below), opd_none(), TYPE_BOOL, line));
            }
        } else {
            wrapped = ir_new_temp(ir, TYPE_BOOL);
            int neg = ir_new_temp(ir, TYPE_BOOL);
            ir_emit(ir, ir_instr(IR_LT, neg, step, opd_int(0), TYPE_INT, line));
            ir_emit(ir, ir_instr(IR_NE, wrapped, opd_var(below), opd_var(neg), TYPE_BOOL, line));
        }
        emit_jump(g, IR_IF_TRUE, opd_var(wrapped), l_end, line);
    }
    ir_emit(ir, ir_instr(IR_ASSIGN, var, opd_var(next), opd_none(), TYPE_UNKNOWN, line));
    emit_jump(g, IR_GOTO, opd_none(), l_top, line);
    emit_label(g, l_end);
}

static void gen_stmt(Gen *g, const AstNode *n) {
    IRProgram *ir = g->ir;
    switch (n->kind) {
        case AST_DECL: {
            int v = n->u.decl.sym;
            if (n->u.decl.init) {
                Operand val = gen_expr(g, n->u.decl.init);
                ir_emit(ir, ir_instr(IR_ASSIGN, v, val, opd_none(), TYPE_UNKNOWN, n->line));
            } else if (g->loop_depth > 0) {
                /* Executing a declaration inside a loop re-creates the variable: reset it. */
                ir_emit(ir, ir_instr(IR_ASSIGN, v, zero_of(n->u.decl.decl_type), opd_none(), TYPE_UNKNOWN, n->line));
            }
            break;   /* a top-level declaration needs no code: variables start at zero */
        }
        case AST_ASSIGN: {
            Operand val = gen_expr(g, n->u.assign.value);
            ir_emit(ir, ir_instr(IR_ASSIGN, n->u.assign.sym, val, opd_none(), TYPE_UNKNOWN, n->line));
            break;
        }
        case AST_IF: {
            Operand c = gen_expr(g, n->u.if_stmt.cond);
            int has_else = (n->u.if_stmt.else_block != NULL);
            int l_else = ir_new_label(ir);
            int l_end = has_else ? ir_new_label(ir) : l_else;
            emit_jump(g, IR_IF_FALSE, c, l_else, n->line);
            gen_block(g, n->u.if_stmt.then_block);
            if (has_else) {
                emit_jump(g, IR_GOTO, opd_none(), l_end, n->line);
                emit_label(g, l_else);
                gen_block(g, n->u.if_stmt.else_block);
            }
            emit_label(g, l_end);
            break;
        }
        case AST_WHILE: {
            int l_start = ir_new_label(ir);
            int l_end = ir_new_label(ir);
            emit_label(g, l_start);
            Operand c = gen_expr(g, n->u.loop.cond);
            emit_jump(g, IR_IF_FALSE, c, l_end, n->line);
            g->loop_depth++;
            gen_block(g, n->u.loop.body);
            g->loop_depth--;
            emit_jump(g, IR_GOTO, opd_none(), l_start, n->line);
            emit_label(g, l_end);
            break;
        }
        case AST_REPEAT: {
            int l_start = ir_new_label(ir);
            emit_label(g, l_start);
            g->loop_depth++;
            gen_block(g, n->u.loop.body);
            g->loop_depth--;
            Operand c = gen_expr(g, n->u.loop.cond);
            emit_jump(g, IR_IF_FALSE, c, l_start, n->line);   /* leave the loop when the condition becomes true */
            break;
        }
        case AST_INPUT: {
            IRInstr ins = ir_instr(IR_INPUT, n->u.input.sym, opd_none(), opd_none(), TYPE_UNKNOWN, n->line);
            if (n->u.input.prompt) ins.str = ir_add_string(ir, n->u.input.prompt);
            ir_emit(ir, ins);
            break;
        }
        case AST_PRINT: {
            /* Items before the last get spaces; the last controls line ending. */
            if (n->u.block.count == 0 && n->print_newline) {
                ir_emit(ir, ir_instr(IR_PRINT, -1, opd_none(), opd_none(), TYPE_UNKNOWN, n->line));
            }
            for (int i = 0; i < n->u.block.count; i++) {
                const AstNode *item = n->u.block.stmts[i];
                IROp op = i < n->u.block.count - 1 ? IR_WRITE :
                          n->print_newline ? IR_PRINT : IR_PRINT_CONT;
                if (item->kind == AST_STR_LIT) {
                    IRInstr ins = ir_instr(op, -1, opd_none(), opd_none(), TYPE_UNKNOWN, item->line);
                    ins.str = ir_add_string(ir, item->u.str);
                    ir_emit(ir, ins);
                } else {
                    Operand v = gen_expr(g, item);
                    ir_emit(ir, ir_instr(op, -1, v, opd_none(), TYPE_UNKNOWN, item->line));
                }
            }
            break;
        }
        case AST_FOR:
            gen_for(g, n);
            break;
        default:
            break;
    }
}

static void gen_block(Gen *g, const AstNode *block) {
    for (int i = 0; i < block->u.block.count; i++) gen_stmt(g, block->u.block.stmts[i]);
}

void ir_generate(const AstNode *program, const SymTab *st, IRProgram *out) {
    /* User variables first, so IR variable index == semantic symbol id.
       Shadowed variables share a source name: make display names unique. */
    for (int i = 0; i < st->count; i++) {
        char name[96];
        snprintf(name, sizeof(name), "%s", st->vars[i].name);
        for (int k = 2; name_in_use(out, name); k++)
            snprintf(name, sizeof(name), "%s_%d", st->vars[i].name, k);
        add_var(out, name, st->vars[i].type, 0);
    }
    out->user_var_count = st->count;

    Gen g = { out, 0 };
    gen_block(&g, program);
}
