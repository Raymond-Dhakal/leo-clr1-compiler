/*
 * leoc.c - the Leo compiler driver (entry point of the `leoc` program).
 *
 * Pipeline:
 *   source.leo -> [1] lexer -> [2] CLR(1) parser + AST -> [3] semantic analysis
 *             -> [4] IR -> [5] optimizer -> [6] C code -> GCC -> executable
 *
 * The driver stops at the first phase that reports errors and says which
 * phase failed. Exit codes:
 *   0 success   1 usage / file error   2 lexical error   3 syntax error
 *   4 semantic error   5 grammar/generator problem   6 C compilation failed
 * (with --run the exit code is the exit code of the compiled program).
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nova_build.h"
#include "nova_codegen.h"
#include "nova_diag.h"
#include "nova_interp.h"
#include "nova_ir.h"
#include "nova_lexer.h"
#include "nova_opt.h"
#include "nova_parse.h"
#include "nova_semantic.h"
#include "xalloc.h"

#ifndef LEO_DEFAULT_GRAMMAR
#define LEO_DEFAULT_GRAMMAR "language/grammar.txt"
#endif

enum { EXIT_OK = 0, EXIT_USAGE = 1, EXIT_LEX = 2, EXIT_SYNTAX = 3, EXIT_SEMANTIC = 4,
       EXIT_GRAMMAR = 5, EXIT_CC = 6 };

typedef struct {
    const char *input;
    const char *grammar;        /* --grammar */
    const char *outdir;
    const char *exe;            /* -o */
    const char *cc;
    int tokens, ast, typed_ast, symbols, ir, opt_ir, emit_c;
    int build, run, interp, markers, interactive_input, no_opt, trace, quiet;
    int dump_grammar, dump_first, dump_states, dump_table;
} Options;

static void usage(FILE *out) {
    fputs(
"Leo compiler (CLR(1) front end, C back end)\n"
"\n"
"Usage: leoc <input.leo> [options]\n"
"\n"
"Default (no options): run all six phases and build output/<name> with GCC.\n"
"\n"
"Inspect a phase (prints the result and stops after that phase):\n"
"  --tokens        phase 1  token list\n"
"  --ast           phase 2  abstract syntax tree\n"
"  --typed-ast     phase 3  AST annotated with types\n"
"  --symbols       phase 3  symbol table\n"
"  --ir            phase 4  intermediate code (three-address)\n"
"  --opt-ir        phase 5  IR before and after optimization, with statistics\n"
"  --emit-c        phase 6  generate C, print it, do not call GCC\n"
"\n"
"Build control:\n"
"  --build         force the full pipeline (also after the options above)\n"
"  --run           build, then run the program (exit code = program's)\n"
"  --interp        run the optimized IR with the built in interpreter (no GCC needed)\n"
"  --interactive-input  pause the interpreter when INPUT needs another value\n"
"  --no-opt        skip the optimizer\n"
"  -o <file>       executable path (default <outdir>/<name>)\n"
"  --outdir <dir>  directory for the generated .c and the executable (default output)\n"
"  --cc <compiler> C compiler to invoke (default gcc)\n"
"  -q, --quiet     no per-phase status lines\n"
"\n"
"CLR(1) generator internals (printed, then compilation continues):\n"
"  --grammar <file>  grammar to use (default: language/grammar.txt)\n"
"  --dump-grammar --dump-first --dump-states --dump-table\n"
"  --trace           shift/reduce trace of the parse\n"
"\n"
"  -h, --help      this text\n", out);
}

/* ------------------------------------------------------------------ */

static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    char *buf = (char *)xmalloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; buf = (char *)xrealloc(buf, cap); }
    }
    fclose(f);
    buf[len] = '\0';
    *len_out = len;
    return buf;
}

static int file_readable(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Finds language/grammar.txt: explicit option, $LEO_GRAMMAR, next to the
   executable, the current directory, then the path baked in at build time. */
static const char *locate_grammar(const Options *o, const char *argv0, char *buf, size_t n) {
    if (o->grammar) return o->grammar;
    const char *env = getenv("LEO_GRAMMAR");
    if (env && *env) return env;

    const char *slash = strrchr(argv0, '/');
    if (slash) {
        size_t dl = (size_t)(slash - argv0);
        snprintf(buf, n, "%.*s/language/grammar.txt", (int)dl, argv0);
        if (file_readable(buf)) return buf;
        snprintf(buf, n, "%.*s/../language/grammar.txt", (int)dl, argv0);
        if (file_readable(buf)) return buf;
    }
    if (file_readable("language/grammar.txt")) return "language/grammar.txt";
    return LEO_DEFAULT_GRAMMAR;
}

static void status(const Options *o, int step, const char *name, const char *state, const char *fmt, ...) {
    if (o->quiet) return;
    char detail[256] = "";
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof(detail), fmt, ap);
        va_end(ap);
    }
    if (step > 0) fprintf(stderr, "[%d/6] %-26s %-7s %s\n", step, name, state, detail);
    else          fprintf(stderr, "      %-26s %-7s %s\n", name, state, detail);
}

/* Section delimiter for the web page (only with --markers). */
static void marker(const Options *o, const char *name) {
    if (o->markers) printf("\n@@LEO-SECTION %s@@\n", name);
}

static const char *base_of(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* "examples/hello.leo" -> "hello" */
static void base_name(const char *path, char *buf, size_t n) {
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    const char *bs = strrchr(s, '\\');
    if (bs) s = bs + 1;
    snprintf(buf, n, "%s", s);
    char *dot = strrchr(buf, '.');
    if (dot && dot != buf) *dot = '\0';
    if (buf[0] == '\0') snprintf(buf, n, "a");
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    Options o;
    memset(&o, 0, sizeof(o));
    o.outdir = "output";
    o.cc = "gcc";

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return EXIT_OK; }
        else if (!strcmp(a, "--tokens")) o.tokens = 1;
        else if (!strcmp(a, "--ast")) o.ast = 1;
        else if (!strcmp(a, "--typed-ast")) o.typed_ast = 1;
        else if (!strcmp(a, "--symbols")) o.symbols = 1;
        else if (!strcmp(a, "--ir")) o.ir = 1;
        else if (!strcmp(a, "--opt-ir")) o.opt_ir = 1;
        else if (!strcmp(a, "--emit-c")) o.emit_c = 1;
        else if (!strcmp(a, "--build")) o.build = 1;
        else if (!strcmp(a, "--run")) o.run = 1;
        else if (!strcmp(a, "--interp")) o.interp = 1;
        else if (!strcmp(a, "--interactive-input")) o.interactive_input = o.interp = 1;
        else if (!strcmp(a, "--markers")) o.markers = 1;   /* used by the web page: delimit each output section */
        else if (!strcmp(a, "--no-opt")) o.no_opt = 1;
        else if (!strcmp(a, "--trace")) o.trace = 1;
        else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) o.quiet = 1;
        else if (!strcmp(a, "--dump-grammar")) o.dump_grammar = 1;
        else if (!strcmp(a, "--dump-first")) o.dump_first = 1;
        else if (!strcmp(a, "--dump-states")) o.dump_states = 1;
        else if (!strcmp(a, "--dump-table")) o.dump_table = 1;
        else if (!strcmp(a, "-o") || !strcmp(a, "--outdir") || !strcmp(a, "--cc") || !strcmp(a, "--grammar")) {
            if (i + 1 >= argc) { fprintf(stderr, "leoc: option '%s' needs an argument\n", a); return EXIT_USAGE; }
            const char *v = argv[++i];
            if (!strcmp(a, "-o")) o.exe = v;
            else if (!strcmp(a, "--outdir")) o.outdir = v;
            else if (!strcmp(a, "--cc")) o.cc = v;
            else o.grammar = v;
        }
        else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "leoc: unknown option '%s' (try --help)\n", a);
            return EXIT_USAGE;
        }
        else if (!o.input) o.input = a;
        else { fprintf(stderr, "leoc: more than one input file given ('%s' and '%s')\n", o.input, a); return EXIT_USAGE; }
    }
    if (!o.input) { usage(stderr); return EXIT_USAGE; }

    /* How far does the pipeline run? */
    int stop = 6;
    int want_build = o.build || o.run || o.interp;
    int inspecting = o.tokens || o.ast || o.typed_ast || o.symbols || o.ir || o.opt_ir || o.emit_c;
    if (inspecting && !want_build) {
        stop = 0;
        if (o.tokens) stop = 1;
        if (o.ast && stop < 2) stop = 2;
        if ((o.typed_ast || o.symbols) && stop < 3) stop = 3;
        if (o.ir && stop < 4) stop = 4;
        if (o.opt_ir && stop < 5) stop = 5;
        if (o.emit_c) stop = 6;
    }
    int do_gcc = !(o.emit_c && !want_build);

    int rc = EXIT_OK;
    size_t src_len = 0;
    char *source = read_file(o.input, &src_len);
    if (!source) {
        fprintf(stderr, "leoc: cannot read '%s'\n", o.input);
        return EXIT_USAGE;
    }
    if (strlen(source) != src_len) {
        fprintf(stderr, "leoc: '%s' contains a NUL byte; is it a text file?\n", o.input);
        free(source);
        return EXIT_USAGE;
    }
    const char *text = source;
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text += 3;

    DiagList diags;
    diag_init(&diags, o.input, text);
    TokenList tokens;
    token_list_init(&tokens);
    LeoFrontend *fe = NULL;
    AstNode *ast = NULL;
    SymTab symtab;
    symtab_init(&symtab);
    IRProgram ir;
    ir_init(&ir);
    StrBuf csrc;
    strbuf_init(&csrc);

    if (!o.quiet) fprintf(stderr, "leoc: compiling %s\n", o.input);

    /* ---------------- [1] lexical analysis ---------------- */
    int lex_errors = nova_lex(text, &tokens, &diags);
    if (o.tokens) { marker(&o, "tokens"); token_list_print(&tokens, stdout); }
    if (lex_errors) {
        diag_print(&diags, stderr);
        status(&o, 1, "Lexical analysis", "FAILED", "%d error%s", lex_errors, lex_errors == 1 ? "" : "s");
        rc = EXIT_LEX;
        goto done;
    }
    status(&o, 1, "Lexical analysis", "OK", "%d tokens", tokens.count - 1);
    if (stop == 1) goto done;

    /* ---------------- [2] syntax analysis (CLR(1)) ---------------- */
    {
        char gbuf[1024], err[512] = "";
        const char *gpath = locate_grammar(&o, argv[0], gbuf, sizeof(gbuf));
        fe = nova_frontend_build(gpath, err, sizeof(err));
        if (!fe) {
            fprintf(stderr, "leoc: %s\n", err);
            status(&o, 2, "Syntax analysis (CLR(1))", "FAILED", "grammar problem");
            rc = EXIT_GRAMMAR;
            goto done;
        }
        if (o.dump_grammar) grammar_print(fe->grammar);
        if (o.dump_first)   first_sets_print(fe->grammar, fe->first);
        if (o.dump_states)  canonical_print(fe->canonical);
        if (o.dump_table)   parsing_table_print(fe->table);
        if (fe->conflicts->count > 0) {
            conflict_list_print(fe->grammar, fe->conflicts);
            fprintf(stderr, "leoc: the language grammar '%s' is not CLR(1); refusing to compile\n", gpath);
            status(&o, 2, "Syntax analysis (CLR(1))", "FAILED", "%d grammar conflicts", fe->conflicts->count);
            rc = EXIT_GRAMMAR;
            goto done;
        }
        ast = nova_parse(fe, &tokens, &diags, o.trace);
        if (!ast) {
            diag_print(&diags, stderr);
            status(&o, 2, "Syntax analysis (CLR(1))", "FAILED", "syntax error");
            rc = EXIT_SYNTAX;
            goto done;
        }
        status(&o, 2, "Syntax analysis (CLR(1))", "OK", "%d LR(1) states, %d conflicts",
               fe->canonical->count, fe->conflicts->count);
        if (o.ast) { marker(&o, "ast"); ast_print(ast, stdout, 0); }
        if (stop == 2) goto done;
    }

    /* ---------------- [3] semantic analysis ---------------- */
    {
        int errors = nova_analyze(ast, &symtab, &diags);
        if (diags.count) diag_print(&diags, stderr);
        if (errors) {
            status(&o, 3, "Semantic analysis", "FAILED", "%d error%s", errors, errors == 1 ? "" : "s");
            rc = EXIT_SEMANTIC;
            goto done;
        }
        status(&o, 3, "Semantic analysis", "OK", "%d variable%s%s", symtab.count, symtab.count == 1 ? "" : "s",
               diags.warning_count ? " (with warnings)" : "");
        if (o.typed_ast) ast_print(ast, stdout, 1);
        if (o.symbols) symtab_print(&symtab, stdout);
        if (stop == 3) goto done;
    }

    /* ---------------- [4] intermediate code ---------------- */
    ir_generate(ast, &symtab, &ir);
    status(&o, 4, "Intermediate code", "OK", "%d instructions", ir.count);
    if (o.ir) { marker(&o, "ir"); ir_print(&ir, stdout); }
    if (stop == 4) goto done;

    /* ---------------- [5] optimization ---------------- */
    if (o.no_opt) {
        status(&o, 5, "Optimization", "SKIPPED", "--no-opt");
        if (o.opt_ir) { marker(&o, "opt-ir"); printf("=== IR (optimizer disabled) ===\n"); ir_print(&ir, stdout); }
    } else {
        if (o.opt_ir) { marker(&o, "opt-ir"); printf("=== IR before optimization ===\n"); ir_print(&ir, stdout); }
        OptStats stats;
        opt_run(&ir, &stats);
        status(&o, 5, "Optimization", "OK", "%d -> %d instructions, %d round%s",
               stats.before, stats.after, stats.rounds, stats.rounds == 1 ? "" : "s");
        if (o.opt_ir) {
            printf("\n=== IR after optimization ===\n");
            ir_print(&ir, stdout);
            printf("\n");
            opt_stats_print(&stats, stdout);
        }
    }
    if (stop == 5) goto done;

    /* ---------------- [6] interpreter (no files, no GCC) ---------------- */
    if (o.interp) {
        if (o.emit_c) {
            marker(&o, "c");
            codegen_c(&ir, base_of(o.input), &csrc);
            fputs(csrc.data, stdout);
        }
        status(&o, 6, "Execution", "OK", "interpreter");
        marker(&o, "run");
        fflush(stdout);
        rc = nova_interpret(&ir, stdin, stdout, o.interactive_input);
        if (rc == 2 && o.interactive_input) {
            status(&o, 6, "Execution", "WAITING", "input required");
            fprintf(stderr, "@@LEO-NEED-INPUT@@\n");
        }
        goto done;
    }

    /* ---------------- [6] C code generation + GCC ---------------- */
    {
        char base[256], cpath[1024], exepath[1024];
        base_name(o.input, base, sizeof(base));
        snprintf(cpath, sizeof(cpath), "%s/%s.c", o.outdir, base);
        if (o.exe) snprintf(exepath, sizeof(exepath), "%s", o.exe);
        else nova_exe_path(exepath, sizeof(exepath), o.outdir, base);

        if (nova_same_file(o.input, cpath) || nova_same_file(o.input, exepath)) {
            fprintf(stderr, "leoc: refusing to overwrite the source file '%s'; choose another --outdir\n", o.input);
            rc = EXIT_USAGE;
            goto done;
        }
        if (nova_mkdirs(o.outdir) != 0) {
            fprintf(stderr, "leoc: cannot create output directory '%s'\n", o.outdir);
            rc = EXIT_USAGE;
            goto done;
        }
        if (o.exe) {                       /* make sure the directory of -o exists too */
            char dir[1024];
            snprintf(dir, sizeof(dir), "%s", o.exe);
            char *sl = strrchr(dir, '/');
            if (sl && sl != dir) { *sl = '\0'; nova_mkdirs(dir); }
        }

        char sname[256];
        base_name(o.input, sname, sizeof(sname));
        const char *shown = strrchr(o.input, '/') ? strrchr(o.input, '/') + 1 : o.input;
        codegen_c(&ir, shown, &csrc);
        if (nova_write_file(cpath, csrc.data, csrc.len) != 0) {
            fprintf(stderr, "leoc: cannot write '%s'\n", cpath);
            rc = EXIT_USAGE;
            goto done;
        }
        status(&o, 6, "C code generation", "OK", "%s", cpath);
        if (o.emit_c) fputs(csrc.data, stdout);

        if (do_gcc) {
            char *cc_argv[] = { (char *)o.cc, "-O2", "-fwrapv", "-o", exepath, cpath, "-lm", NULL };
            int cc_rc = nova_spawn(cc_argv);
            if (cc_rc != 0) {
                if (cc_rc == 127 || cc_rc < 0)
                    fprintf(stderr, "leoc: could not run the C compiler '%s' (is it installed and on PATH?)\n", o.cc);
                status(&o, 0, o.cc, "FAILED", "exit status %d", cc_rc);
                rc = EXIT_CC;
                goto done;
            }
            status(&o, 0, o.cc, "OK", "%s", exepath);

            if (o.run) {
                if (!o.quiet) fprintf(stderr, "--- running %s ---\n", exepath);
                char runpath[1100];
                if (strchr(exepath, '/') || strchr(exepath, '\\')) snprintf(runpath, sizeof(runpath), "%s", exepath);
                else snprintf(runpath, sizeof(runpath), "./%s", exepath);
                char *run_argv[] = { runpath, NULL };
                int prog_rc = nova_spawn(run_argv);
                rc = (prog_rc < 0) ? EXIT_USAGE : prog_rc;
            }
        }
    }

done:
    strbuf_free(&csrc);
    ir_free(&ir);
    symtab_free(&symtab);
    ast_free(ast);
    nova_frontend_free(fe);
    token_list_free(&tokens);
    diag_free(&diags);
    free(source);
    return rc;
}
