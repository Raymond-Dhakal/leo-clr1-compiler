/*
 * nova_ast.c - AST construction helpers, destructor and pretty printer.
 */
#include <string.h>
#include "nova_ast.h"
#include "nova_util.h"
#include "xalloc.h"

AstNode *ast_new(AstKind kind, int line, int col) {
    AstNode *n = (AstNode *)xcalloc(1, sizeof(AstNode));
    n->kind = kind;
    n->line = line;
    n->col = col;
    n->type = TYPE_UNKNOWN;
    return n;
}

void ast_block_append(AstNode *block, AstNode *stmt) {
    NOVA_GROW(block->u.block.stmts, block->u.block.count, block->u.block.capacity);
    block->u.block.stmts[block->u.block.count++] = stmt;
}

void ast_free(AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < n->u.block.count; i++) ast_free(n->u.block.stmts[i]);
            free(n->u.block.stmts);
            break;
        case AST_DECL:   free(n->u.decl.name);   ast_free(n->u.decl.init);  break;
        case AST_ASSIGN: free(n->u.assign.name); ast_free(n->u.assign.value); break;
        case AST_IF:
            ast_free(n->u.if_stmt.cond);
            ast_free(n->u.if_stmt.then_block);
            ast_free(n->u.if_stmt.else_block);
            break;
        case AST_WHILE:
        case AST_REPEAT:
            ast_free(n->u.loop.cond);
            ast_free(n->u.loop.body);
            break;
        case AST_FOR:
            free(n->u.for_stmt.name);
            ast_free(n->u.for_stmt.start); ast_free(n->u.for_stmt.limit);
            ast_free(n->u.for_stmt.step);  ast_free(n->u.for_stmt.body);
            break;
        case AST_INPUT:  free(n->u.input.name); free(n->u.input.prompt); break;
        case AST_PRINT:
            for (int i = 0; i < n->u.block.count; i++) ast_free(n->u.block.stmts[i]);
            free(n->u.block.stmts);
            break;
        case AST_STR_LIT: free(n->u.str); break;
        case AST_BINARY: ast_free(n->u.binary.left); ast_free(n->u.binary.right); break;
        case AST_UNARY:  ast_free(n->u.unary.operand); break;
        case AST_VAR:    free(n->u.var.name); break;
        case AST_INT_LIT: case AST_FLOAT_LIT: case AST_BOOL_LIT: break;
    }
    free(n);
}

const char *data_type_name(DataType t) {
    switch (t) {
        case TYPE_INT:   return "INT";
        case TYPE_FLOAT: return "FLOAT";
        case TYPE_BOOL:  return "BOOL";
        case TYPE_ERROR: return "<error>";
        default:         return "<unknown>";
    }
}

const char *binop_text(BinOp op) {
    switch (op) {
        case OP_ADD: return "+";   case OP_SUB: return "~";   case OP_MUL: return "*";
        case OP_DIV: return "/";   case OP_MOD: return "MOD"; case OP_POW: return "^";
        case OP_LT:  return "<";   case OP_GT:  return ">";   case OP_LE:  return "<=";
        case OP_GE:  return ">=";  case OP_EQ:  return "==";  case OP_NE:  return "!=";
        case OP_AND: return "AND"; case OP_OR:  return "OR";
    }
    return "?";
}

const char *unop_text(UnOp op) { return op == UOP_NEG ? "~" : "NOT"; }

/* ---------- pretty printer ---------- */

static void indent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", out);
}

static void type_suffix(const AstNode *n, FILE *out, int show_types) {
    if (show_types && n->type != TYPE_UNKNOWN) fprintf(out, " : %s", data_type_name(n->type));
}

static void print_node(const AstNode *n, FILE *out, int depth, int st);

static void print_labeled(const char *label, const AstNode *child, FILE *out, int depth, int st) {
    indent(out, depth);
    fprintf(out, "%s\n", label);
    print_node(child, out, depth + 1, st);
}

static void print_node(const AstNode *n, FILE *out, int depth, int st) {
    if (!n) return;
    indent(out, depth);
    char num[64];
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            fputs(n->kind == AST_PROGRAM ? "Program\n" : "Block\n", out);
            for (int i = 0; i < n->u.block.count; i++) print_node(n->u.block.stmts[i], out, depth + 1, st);
            break;
        case AST_DECL:
            fprintf(out, "Decl %s %s\n", data_type_name(n->u.decl.decl_type), n->u.decl.name);
            if (n->u.decl.init) print_node(n->u.decl.init, out, depth + 1, st);
            break;
        case AST_ASSIGN:
            fprintf(out, "Assign %s\n", n->u.assign.name);
            print_node(n->u.assign.value, out, depth + 1, st);
            break;
        case AST_IF:
            fputs("If\n", out);
            print_labeled("Cond", n->u.if_stmt.cond, out, depth + 1, st);
            print_labeled("Then", n->u.if_stmt.then_block, out, depth + 1, st);
            if (n->u.if_stmt.else_block) print_labeled("Else", n->u.if_stmt.else_block, out, depth + 1, st);
            break;
        case AST_WHILE:
            fputs("While\n", out);
            print_labeled("Cond", n->u.loop.cond, out, depth + 1, st);
            print_labeled("Body", n->u.loop.body, out, depth + 1, st);
            break;
        case AST_REPEAT:
            fputs("Repeat\n", out);
            print_labeled("Body", n->u.loop.body, out, depth + 1, st);
            print_labeled("Until", n->u.loop.cond, out, depth + 1, st);
            break;
        case AST_FOR:
            if (n->u.for_stmt.has_decl)
                fprintf(out, "For %s %s\n", data_type_name(n->u.for_stmt.decl_type), n->u.for_stmt.name);
            else
                fprintf(out, "For %s\n", n->u.for_stmt.name);
            print_labeled("From", n->u.for_stmt.start, out, depth + 1, st);
            print_labeled("To", n->u.for_stmt.limit, out, depth + 1, st);
            if (n->u.for_stmt.step) print_labeled("Step", n->u.for_stmt.step, out, depth + 1, st);
            print_labeled("Body", n->u.for_stmt.body, out, depth + 1, st);
            break;
        case AST_INPUT:
            if (n->u.input.prompt) {
                StrBuf q; strbuf_init(&q);
                strbuf_append_quoted(&q, n->u.input.prompt, 0);
                fprintf(out, "Input %s prompt %s\n", n->u.input.name, q.data);
                strbuf_free(&q);
            } else {
                fprintf(out, "Input %s\n", n->u.input.name);
            }
            break;
        case AST_PRINT:
            fputs("Print\n", out);
            for (int i = 0; i < n->u.block.count; i++) print_node(n->u.block.stmts[i], out, depth + 1, st);
            break;
        case AST_STR_LIT: {
            StrBuf q; strbuf_init(&q);
            strbuf_append_quoted(&q, n->u.str, 0);
            fprintf(out, "Str %s\n", q.data);
            strbuf_free(&q);
            break;
        }
        case AST_BINARY:
            fprintf(out, "Binary %s", binop_text(n->u.binary.op));
            type_suffix(n, out, st);
            fputc('\n', out);
            print_node(n->u.binary.left, out, depth + 1, st);
            print_node(n->u.binary.right, out, depth + 1, st);
            break;
        case AST_UNARY:
            fprintf(out, "Unary %s", unop_text(n->u.unary.op));
            type_suffix(n, out, st);
            fputc('\n', out);
            print_node(n->u.unary.operand, out, depth + 1, st);
            break;
        case AST_VAR:
            fprintf(out, "Var %s", n->u.var.name);
            type_suffix(n, out, st);
            fputc('\n', out);
            break;
        case AST_INT_LIT:
            fprintf(out, "Int %lld", n->u.ival);
            type_suffix(n, out, st);
            fputc('\n', out);
            break;
        case AST_FLOAT_LIT:
            nova_fmt_double_display(n->u.fval, num, sizeof(num));
            fprintf(out, "Float %s", num);
            type_suffix(n, out, st);
            fputc('\n', out);
            break;
        case AST_BOOL_LIT:
            fprintf(out, "Bool %s", n->u.bval ? "TRUE" : "FALSE");
            type_suffix(n, out, st);
            fputc('\n', out);
            break;
    }
}

void ast_print(const AstNode *n, FILE *out, int show_types) {
    print_node(n, out, 0, show_types);
}

int ast_const_number(const AstNode *n, int *is_float, long long *iv, double *fv) {
    if (n->kind == AST_INT_LIT)   { *is_float = 0; *iv = n->u.ival; *fv = (double)n->u.ival; return 1; }
    if (n->kind == AST_FLOAT_LIT) { *is_float = 1; *iv = 0; *fv = n->u.fval; return 1; }
    if (n->kind == AST_UNARY && n->u.unary.op == UOP_NEG) {
        if (!ast_const_number(n->u.unary.operand, is_float, iv, fv)) return 0;
        *iv = (long long)(0ULL - (unsigned long long)*iv);   /* wraps like INT negation */
        *fv = -*fv;
        return 1;
    }
    return 0;
}
