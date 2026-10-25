# Leo Compiler

A six phase compiler for the Leo language (`.leo`), built on a Canonical LR(1)
parser generator written in C. `leoc` turns a Leo program into a native
executable: lexer, CLR(1) parser, semantic analysis, three address code,
optimizer, C code generation, then GCC.

**GitHub repository:** <https://github.com/Raymond-Dhakal/leo-clr1-compiler>

The CLR(1) core written by the team (grammar loader, FIRST sets, closure and
goto, canonical collection, ACTION and GOTO tables, conflict detection, LR
driver) is unchanged in behaviour and is what parses Leo. The parser is not
hard coded: `leoc` reads `language/grammar.txt`, builds the canonical
collection and the tables when it starts (856 states, about 0.4 s), and refuses
to compile if that grammar has conflicts.

## Team

| Member | Registration | Responsibilities and main files |
|---|---|---|
| Gurutheja Kavuturi | 24BEL0086 | CLR(1): grammar engine and original lexer (`grammar.c`, `lexer.c`). Leo compiler: lexical analysis and semantic analysis (`src/compiler/nova_lexer.c`, `src/compiler/nova_semantic.c`). |
| Ephraim Immanuel | 24BEL0091 | CLR(1): FIRST sets and lookahead (`first.c`). Leo compiler: intermediate-code generation (`src/compiler/nova_ir.c`). |
| Goyal Hemant Anil | 24BYB0059 | CLR(1): LR(1) items, closure, and GOTO (`lr1_item.c`, `closure.c`, `goto.c`). Leo compiler: code optimization (`src/compiler/nova_opt.c`). |
| Raymond Dhakal | 24BEL0100 | CLR(1): canonical collection, tables, conflicts, parser, and driver (`state.c`, `canonical.c`, `parsing_table.c`, `conflict.c`, `parser.c`, `main.c`). Leo compiler/product: C code generation and build (`src/compiler/nova_codegen.c`, `src/compiler/nova_build.c`), web frontend/playground (`web/`), and RAG project Q&A (`rag/`). |

## Build

Requires GCC (or any C11 compiler) and `make`. CMake is also supported.

```bash
make            # builds ./leoc and ./clr_parser
make test       # builds, then runs the whole test suite

# or with CMake
cmake -S . -B build && cmake --build build
cmake --build build --target check
```

## Use

```bash
./leoc examples/08_combined.leo                 # all six phases, builds output/08_combined
echo 12 | ./output/08_combined                  # run it
./leoc examples/08_combined.leo --run < in.txt  # build and run

./leoc prog.leo --tokens      # phase 1: token list
./leoc prog.leo --ast         # phase 2: syntax tree
./leoc prog.leo --typed-ast   # phase 3: tree annotated with types
./leoc prog.leo --symbols     # phase 3: symbol table
./leoc prog.leo --ir          # phase 4: three address code
./leoc prog.leo --opt-ir      # phase 5: IR before and after, with statistics
./leoc prog.leo --emit-c      # phase 6: print the generated C, do not call GCC
./leoc prog.leo --build       # force the full pipeline
```

Other options: `--no-opt`, `--run`, `-o <file>`, `--outdir <dir>` (default
`output`), `--cc <compiler>`, `-q`, `--grammar <file>`, and generator
internals `--dump-grammar --dump-first --dump-states --dump-table --trace`.
Run `./leoc --help` for the full list.

## Ask questions about the project

The read-only project Q&A page uses local retrieval plus Groq for answers.
Install the Groq client with `python3 -m pip install -r requirements.txt`, add
`GROQ_API_KEY` to the root `.env` file, and run `make serve`, then open
`http://localhost:8000/ask.html`. The same server continues to host the
playground at `http://localhost:8000/`. See `rag/README.md` for details.

Behaviour that is deliberate:

* The source file is never modified. Generated files go to `--outdir`.
* The program is never run unless `--run` is given.
* GCC is started with `fork` and `execvp` and an argument vector, never through
  a shell string, so file names cannot inject commands.
* A phase that fails stops the pipeline. The failing phase is shown as
  `FAILED` and the process exit status says which one.

| Exit status | Meaning |
|---|---|
| 0 | success (with `--run`, the program's own status) |
| 1 | usage or file error |
| 2 | lexical error |
| 3 | syntax error |
| 4 | semantic error |
| 5 | grammar problem (conflicts or missing file) |
| 6 | C compiler failed |

## The language in brief

Full reference: `language/language_spec.md`. Grammar: `language/grammar.txt`.

```
INT n
INPUT n
INT fact <- 1
INT i <- 2
WHILE (i <= n) DO
    fact <- fact * i
    i <- i + 1
END
PRINTN fact
```

* Types `INT` (64 bit, wraps), `FLOAT` (double), `BOOL`. Only `INT` to `FLOAT`
  converts implicitly.
* Statements: declaration, `<-` assignment, `IF ... THEN ... [ELSE ...] END`,
  `WHILE (...) DO ... END`, `REPEAT ... UNTIL (...)`, `FOR`, `INPUT`, `PRINT`.
* Comments start with `#`.

```
FOR INT i <- 1 TO 10 STEP 3 DO PRINT "i =", i END     # counts up: 1 4 7 10
FOR (j <- 5 TO 1 STEP ~2) DO PRINT j END               # counts down: 5 3 1 (j declared earlier)
INPUT "How many? ", n                                  # prompt, then read
INPUT n                                                # plain read
PRINTN "total", n * 2, TRUE                             # items joined by one space
```

A positive step counts up and a negative step counts down; the direction is
never reversed automatically. `PRINT` and `INPUT` are the only places a string
may appear. `PRINT` continues on the current line; `PRINTN` ends the line.

Operators, highest precedence first:

| Level | Operators | Associativity |
|---|---|---|
| 1 | `( )` | |
| 2 | `NOT`, unary `~` | right |
| 3 | `^` | right |
| 4 | `*` `/` `MOD` | left |
| 5 | `+` `~` (subtraction) | left |
| 6 | `<` `>` `<=` `>=` | left |
| 7 | `==` `!=` | left |
| 8 | `AND` | left |
| 9 | `OR` | left |

Assignment is `<-`. Precedence and associativity are encoded in the grammar
itself (one non terminal per level), so the CLR(1) tables enforce them with no
conflicts and no precedence declarations.

## Two ways to run Leo

| Mode | How | Backend |
|---|---|---|
| Terminal | `./leoc prog.leo --run` | GCC builds a native executable |
| Terminal, no GCC | `./leoc prog.leo --interp` | built in IR interpreter |
| Web page | open the Leo Playground (`web/`) | the same `leoc`, compiled to WebAssembly, using the interpreter |

### Web playground

`web/` is a static site: write code, press Run (Ctrl+Enter), read the output,
and inspect the Tokens, AST, IR, Optimized IR and C tabs. You can prefill values
in the input box below the editor, or enter them as the program pauses for each
`INPUT` in the output panel.
A run that does not finish in 15 seconds is stopped; the Stop button does it
sooner. Programs are kept in the browser, and Copy link shares one.

Try it locally (the page needs http, it will not work from `file://`):

```bash
make serve          # then open http://localhost:8000
```

Host it on GitHub Pages: push the repository, then in Settings, Pages, set the
source to GitHub Actions. `.github/workflows/pages.yml` publishes the `web`
folder on every push to `main`. The compiled `web/leo.wasm` is committed, so
no build runs in CI. To rebuild it after changing the compiler:
`WASI_SDK=/path/to/wasi-sdk-25.0 make wasm` (also refreshes `web/grammar.txt`
and `web/examples.js`; `make web` refreshes only those two).

## The six phases

| Phase | Module | Output |
|---|---|---|
| 1 Lexical analysis | `src/compiler/nova_lexer.c` | tokens with line and column, lexical errors |
| 2 Syntax analysis | `src/compiler/nova_parse.c` plus the CLR(1) core in `src/` | AST built in reduce actions, syntax errors listing expected tokens |
| 3 Semantic analysis | `src/compiler/nova_semantic.c` | scoped symbol table, types, all errors reported |
| 4 Intermediate code | `src/compiler/nova_ir.c` | typed three address code with labels and jumps |
| 5 Optimization | `src/compiler/nova_opt.c` | optimized IR and statistics |
| 6 Code generation | `src/compiler/nova_codegen.c`, `nova_build.c` | C source, then a native executable via GCC |

### Methods used, mapped to the syllabus

| Syllabus module | Method used in Leo | Scope and syllabus topics not implemented |
|---|---|---|
| 1. Introduction and lexical analysis | A hand-written, longest-match scanner recognizes lexemes and emits tokens with attributes such as line and column. | The scanner does not use the direct regular-expression-to-DFA method or Lex. LLVM is not used; Leo has its own frontend and emits C for GCC. |
| 2. Syntax analysis | Table-driven Canonical LR(1) shift-reduce parsing. CLOSURE propagates lookaheads with `FIRST(βa)`; GOTO builds the canonical collection and ACTION/GOTO tables. Parser reductions build the AST. | This project uses CLR(1), not LL(1), SLR(1), or LALR(1). |
| 3. Semantic analysis | A post-parse AST traversal implements syntax-directed checks using nested symbol-table scopes, type rules, and source-located diagnostics. | Semantic checking is a separate AST pass; it is not evaluated as a formal L-attributed definition during parsing. |
| 4. Intermediate code generation | The AST is lowered to typed three-address code with temporaries. Branches and labels represent conditionals and loops; short-circuit Boolean operators become control flow. | Labels are allocated directly during emission; a separate backpatching pass is not used. Switch-case translation is not part of Leo. |
| 5. Code optimization | Machine-independent optimization over a control-flow graph: basic blocks, forward constant propagation/folding, safe algebraic identities, local copy propagation, branch cleanup, and backward liveness for dead-code elimination. | The current optimizer does not implement DAG-based basic-block optimization or loop-specific transformations such as loop-invariant code motion. |
| 6. Code generation | Leo IR is emitted as C; GCC then performs native machine-code generation and its own register allocation. Runtime helpers preserve Leo's input, arithmetic, and error behavior. | Leo does not implement its own target-machine register allocator, next-use analysis, or activation-record layout. |
| 7. Parallelism | Not implemented in the current compiler. | Automatic parallelization, cache/vectorization optimization, instruction scheduling, software pipelining, and SSA are outside the current scope. |

The driver is `src/compiler/leoc.c`. Design notes are in `docs/design.md`.

### Optimizer

Works on a control flow graph of basic blocks and repeats until nothing changes:
constant propagation and folding (a forward dataflow analysis, so values that
differ on two branches are not folded and values that agree are), algebraic
identities, copy propagation, resolution of constant branches, removal of
unreachable code and unused labels, and dead code elimination from backward
liveness. It never folds, moves or deletes an operation that can fail at run
time (division, `MOD`, negative integer exponent), so the program behaves the
same with and without `--no-opt`. Only identities that are exact for floating
point are used, so `f * 0.0` and `f + 0.0` are kept.

## Example: end to end

`examples/03_if_else.leo` (first part):

```
INT x
INPUT x
IF (x > 5) THEN
    PRINTN x
ELSE
    PRINTN 0
END
```

`./leoc examples/03_if_else.leo --ir` gives

```
    INPUT x
    t1 = x > 5
    IF_FALSE t1 GOTO L1
    PRINTN x
    GOTO L2
L1:
    PRINTN 0
L2:
```

and `--emit-c` gives, for the same part,

```c
    // INPUT x
    nv_input_int(&v_x, "x", 3);
    // t1 = x > 5
    t1 = (v_x > 5LL);
    // IF_FALSE t1 GOTO L1
    if (!t1) goto L1;
    // PRINT x
    printf("%lld\n", v_x);
    // GOTO L2
    goto L2;
L1:;
    // PRINT 0
    printf("%lld\n", 0LL);
L2:;
```

Constant folding, `examples/02_precedence.leo` with `--opt-ir`. Before, the
first statement is `t1 = 3 * 4`, `t2 = 2 + t1`, `r = t2`, `PRINT r`. After:

```
    PRINTN 14
    PRINTN 20
    PRINTN 512
    ...
    instructions                : 42 -> 12
```

The full listing and statistics are printed by the command itself.

### Examples directory

| File | Shows |
|---|---|
| `01_arithmetic.leo` | all arithmetic operators |
| `02_precedence.leo` | precedence, associativity, `^`, unary `~` |
| `03_if_else.leo` | IF, optional ELSE, nesting |
| `04_while_loop.leo` | WHILE |
| `05_repeat_until.leo` | REPEAT UNTIL |
| `06_logical.leo` | AND, OR, NOT, short circuit |
| `07_input_output.leo` | INPUT and PRINT of every type |
| `08_combined.leo` | factorial, primality, gcd |
| `09_for_loops.leo` | FOR up, down, nested, expression bounds |
| `10_prompts_and_print.leo` | INPUT prompts, PRINT lists with strings |
| `errors/*.leo` | one lexical, two syntax, one semantic error file |

Sample diagnostics:

```
examples/errors/semantic_errors.leo:4:1: semantic error: cannot assign a FLOAT value to INT variable 'count' (FLOAT to INT narrowing is not implicit)
     4 | count <- ratio
       | ^
```

## Tests

`make test` runs `tests/run_tests.sh`. Nothing is reported as passed unless it
was executed. Each test is a `.leo` file with sidecar files (`.out` exact
stdout, `.outhas` required lines, `.err` required diagnostics, `.in` input,
`.flags`, `.code` expected exit status). Programs in the `e2e` group are built
with GCC and run twice, optimized and with `--no-opt`, and both must give the
expected output.

Latest result on this tree: **500 passed, 0 failed**.

| Group | Passed | Covers |
|---|---|---|
| lexer | 26 | every token kind, strings and escapes, positions, comments, malformed input |
| parser | 57 | valid forms, FOR and PRINT lists, precedence trees, syntax errors |
| semantic | 49 | types, scopes, FOR rules, all error kinds, symbol table |
| ir | 28 | each statement and operator, FOR lowering, labels, short circuit |
| opt | 41 | folding, identities, branches, dead code, traps preserved |
| backend | 22 | emitted C, driver flags, exit status of `--run` |
| e2e | 255 | 85 programs, each run optimized, unoptimized and through `--interp` |
| generator | 22 | original CLR(1) tool, the Leo grammar (0 conflicts, 856 states), web grammar copy |

Beyond the suite, a random program generator with an independent reference
interpreter (now covering FOR, print lists and prompts) was used for
differential testing of the optimized build, the unoptimized build and the
interpreter. The suite also passes under AddressSanitizer and
UndefinedBehaviorSanitizer with leak checking.

## Repository layout

```
include/            headers (original generator plus nova_*.h)
src/                original CLR(1) generator modules
src/compiler/       Leo phases and the leoc driver
language/           grammar.txt, language_spec.md
examples/           sample programs, errors/ holds invalid ones
tests/              run_tests.sh and cases/<group>/
tests/grammars/     original generator test grammars
web/                Leo Playground (static site, leo.wasm)
tools/              make_web.py, build_wasm.sh
.github/workflows/  GitHub Pages deployment
docs/               design.md, clr_generator.md (the original generator manual)
```

## The standalone generator

`./clr_parser <grammar_file> [input_string] [-v]` still works as before on any
grammar in the whitespace token format. See `docs/clr_generator.md`.

## Limitations

* One `INT` size (64 bit) and no arrays or functions. Strings exist only as `PRINT` and `INPUT` text, not as values.
* In the web page each submitted `INPUT` value restarts the deterministic interpreter from the beginning with all values entered so far; parser tables are rebuilt on each run (about 0.5 s).
* The parser reports the first syntax error and stops; there is no recovery.
* `leoc` needs GCC (or the compiler given with `--cc`) on the path for the
  last phase; every earlier phase works without it.
* Table construction runs on every invocation (about 0.4 s). It is not cached.

## Differential fuzzing

`tests/fuzz/check.sh SEED DIR` generates one random valid program with
`tests/fuzz/gen.py`, which also computes the expected output with its own
interpreter, builds it with and without the optimizer, runs it through `--interp` and compares. Example:
`seq 1 100 | xargs -P4 -I{} tests/fuzz/check.sh {} /tmp/fuzz`. It needs
Python 3 and is not part of `make test`.
