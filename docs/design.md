# Design Notes

## Problem statement

Given an arbitrary context-free grammar (with epsilon productions
allowed) and an input string over that grammar's terminals, determine
whether the string is derivable from the grammar's start symbol, using
the Canonical LR(1) parsing method — i.e. build the full canonical
collection of LR(1) item sets (not the LALR or SLR approximations),
derive ACTION/GOTO tables from it, report any shift-reduce or
reduce-reduce conflicts, and drive a standard LR shift-reduce parser
over the input.

## Scope

In scope:
- Context-free grammars with epsilon productions, expressed in the
  whitespace-token format described in `README.md`.
- Full Canonical LR(1) construction: LR(1) items carry a genuine
  single-terminal lookahead computed via `FIRST(beta a)` in CLOSURE,
  not derived from `FOLLOW` sets.
- Conflict detection (shift-reduce, reduce-reduce) with diagnostic
  reporting of state, lookahead, and both competing actions.
- A working shift-reduce-goto-accept parser with a verbose trace mode.

Out of scope (for this iteration):
- Grammar transformations (left-recursion elimination, left-factoring)
  — the input grammar is used as given.
- Error recovery / resynchronization after a syntax error (the parser
  reports the error and stops).
- LALR(1) table compression. This project is specifically about CLR(1);
  states are never merged across different lookahead sets.

## Why Canonical LR(1) and not LALR/SLR

SLR(1) computes reduce lookaheads from `FOLLOW(A)` for the whole
grammar, which is often too permissive (introduces reduce-reduce
conflicts that a context-sensitive lookahead would avoid). LALR(1)
merges canonical LR(1) states that share the same *core* (ignoring
lookahead), trading table size for a small risk of new reduce-reduce
conflicts not present in the canonical construction. Canonical LR(1),
implemented here, keeps every `(core, lookahead)` combination as a
logically distinct item and never merges states with different
lookahead sets, at the cost of a larger state count. `closure.c` and
`goto.c` are written specifically against this definition — see the
comments at the top of each file.

## Key algorithms

### FIRST sets + nullability (`first.c`)

Standard fixed-point iteration:
- `FIRST(terminal) = {terminal}`.
- A symbol is `nullable` if it has an `EPSILON` production, or a
  production whose entire RHS is nullable.
- `FIRST(A)` accumulates `FIRST(X1)`, and if `X1` is nullable also
  `FIRST(X2)`, and so on, until a non-nullable symbol is hit or the
  RHS is exhausted (in which case `A` is nullable too).
- `first_of_sequence(seq, trailing_lookahead)` implements
  `FIRST(beta a)`: it's exactly the same walk, except if the entire
  sequence `beta` is nullable, `trailing_lookahead` itself is added to
  the result (this is precisely the "beta could vanish so the actual
  lookahead becomes a" case for LR(1) closure).

### CLOSURE (`closure.c`)

For `[A -> alpha . B beta, a]` with `B` a non-terminal, add
`[B -> . gamma, b]` for every production `B -> gamma` and every
`b in FIRST(beta a)` (computed via `first_of_sequence`). Iterated to a
fixed point. Implemented as a worklist over "passes": each pass only
scans items present at the start of that pass, so newly discovered
items are processed on the next pass — this keeps the closure
terminating and correct without rescanning the whole set on every
single insertion.

### GOTO (`goto.c`)

`GOTO(I, X)`: for every item in `I` with `X` immediately after the
dot, advance the dot past `X`; then close the result. Delegates
entirely to `closure_compute` rather than re-implementing lookahead
propagation.

### Canonical collection (`canonical.c`)

Breadth-first worklist starting from
`I0 = CLOSURE({[S' -> . S, $]})`. For each discovered state and each
distinct symbol appearing after a dot in it, computes `GOTO` and either
attaches a transition to an existing state with an identical item set
or creates a new state.

### Parsing table + conflicts (`parsing_table.c`, `conflict.c`)

- Every terminal transition out of state `i` on `a` becomes
  `ACTION[i][a] = shift j`.
- Every complete item `[A -> alpha ., a]` (other than the augmented
  production) becomes `ACTION[i][a] = reduce A -> alpha`.
- The complete augmented item `[S' -> S ., $]` becomes
  `ACTION[i][$] = accept`.
- Every non-terminal transition out of state `i` on `A` becomes
  `GOTO[i][A] = j`.
- If a cell would be assigned two different actions, the first one
  set is kept and the attempt is recorded as a `Conflict` instead of
  overwriting — so the table is always deterministic and every
  conflict is visible in the report.

### Parser (`parser.c`)

Textbook table-driven LR algorithm: a state stack and a symbol stack
advanced in lock-step, consulting `ACTION`/`GOTO` until `accept` or an
undefined action (`ERROR`) is reached.

## Test/edge case coverage

- `simple_grammar.txt` — sanity check against the well-known 10-state
  canonical collection for `S -> A A`, `A -> aA | b`.
- `expression_grammar.txt` — conflict-free grammar exercising
  left-recursion, nested parentheses, and operator precedence encoded
  structurally (not via precedence declarations).
- `epsilon_grammar.txt` — exercises nullability and epsilon-production
  reduction (`A -> EPSILON`).
- `conflict_grammar.txt` — deliberately ambiguous dangling-else style
  grammar to exercise shift-reduce conflict detection/reporting.
- `tests/valid_inputs/`, `tests/invalid_inputs/`, `tests/edge_cases/`
  — sample strings, including malformed input (double operators,
  unbalanced parentheses, juxtaposed terminals) and edge cases
  (deeply nested parentheses, minimal single-token input).

# Leo compiler design

The sections above describe the original CLR(1) generator, which is still the
parsing engine. This part describes what was built on top of it.

## Pipeline

```
source.leo
  nova_lexer.c      tokens (line, column)
  nova_parse.c      grammar.txt -> FIRST -> canonical LR(1) -> tables, then LR driver
                    with hooks; the AST is built in reduce callbacks
  nova_semantic.c   scoped symbol table, types, diagnostics
  nova_ir.c         typed three address code
  nova_opt.c        basic blocks, dataflow, rewrites, dead code removal
  nova_codegen.c    C source
  nova_build.c      GCC through fork and execvp
```

## Using the generator without changing it

`parser_run` keeps its exact behaviour and trace. A shared `parser_core` adds
optional `ParserHooks` (`shift`, `reduce`, `discard`) that carry a semantic
value per stack entry, so the same tables drive both the original tool and the
compiler. A reduce hook receives the production and the values of its right
hand side and returns the new value (an AST node). Actions are bound to
productions by the production text, and `nova_frontend_build` fails with exit
status 5 if a production has no action, if a grammar terminal has no lexer
token, or if the table has any conflict.

## Grammar and precedence

Operator levels are separate non terminals, from `OrExpr` down to `Unary`.
Left associative levels use left recursion. `PowExpr -> Unary ^ PowExpr` is
right recursive, and `Unary` sits below `^`, which makes `~2 ^ 2` equal
`(~2) ^ 2`. The result is a conflict free canonical LR(1) automaton with 856
states.

### FOR, INPUT prompts and PRINT lists

`FOR ForHead DO StmtList END` where the head is `( ForSpec )` or `ForSpec`, and
a spec is `ForVar <- Expr TO Expr [STEP Expr]`. The loop variable is an existing
name or a new one introduced by `Type id`. Direction comes from the sign of the
step: a positive step counts up and tests `<=`, a negative step counts down and
tests `>=`. There is no automatic reversal when the bounds are out of order, so
`FOR i <- 5 TO 1` runs zero times. Strings are tokens (`strlit`) that appear
only in `PRINT` and `INPUT`, so they never reach the expression grammar.

The limit and the step are evaluated once, into temporaries. A constant step has
a known direction, so one comparison is emitted. A non constant step gets a
`CHECK_STEP` (run time error when zero) and a branch on its sign. A constant
zero step is a semantic error. The increment `i + step` is guarded against
`INT` wrap around: the loop ends when the sum would overflow, so a loop that
reaches the limit of the `INT` range terminates. The guard is omitted when it
provably cannot fire.

New IR operations: `WRITE x` (print without a newline), `CHECK_STEP s`, and
string operands held in a pool on the program. The optimizer treats both as
uses of their operand.

## Interpreter and web build

`nova_interp.c` executes the IR directly and reproduces the semantics of the C
helpers (wrapping, checked division and power, `%g`, identical error text). The
test suite runs every end to end program through GCC, through GCC without the
optimizer, and through the interpreter, and all three must agree.

`web/` holds a static page. `leoc` is compiled to WebAssembly with wasi-sdk
(`tools/build_wasm.sh`; `nova_build.c` has stubs because a browser cannot start
GCC). A Web Worker runs it through a small WASI shim, passing `--interp` and
`--markers`, which prints section markers that the page splits into the
Output, Tokens, AST, IR, Optimized IR and C tabs.

## Semantic analysis

A stack of scopes maps names to symbols. A name may shadow an outer one but
may not be declared twice in one scope, and a declaration's initialiser is
checked before the name enters scope. Expression checking returns a type, with
an error type that suppresses follow up errors. `INT` widens to `FLOAT`;
nothing else converts. Conditions must be `BOOL`.

## Intermediate code

Instructions are `dst = a op b`, `dst = op a`, `dst = a`, `INPUT`, `PRINT`,
`LABEL`, `GOTO`, `IF_FALSE`, `IF_TRUE`. Operands are typed variables,
temporaries or constants. Shadowed variables get unique names (`x`, `x_2`).
`AND` and `OR` are lowered to jumps so the right operand is skipped when
unneeded. A declaration inside a loop emits its initialisation again, so it
resets each iteration.

## Optimizer

The code is cut into basic blocks. Passes run in rounds until no change:

1. Constant propagation and folding over the control flow graph. The lattice
   is TOP, CONST, NAC; blocks merge with a meet; the entry state holds the
   zero default values. Folding follows Leo semantics (wrapping `INT`, exact
   `FLOAT`).
2. Algebraic identities, only those exact in the type: `x + 0`, `x * 1`,
   `x - x`, `x ^ 0`, `x MOD 1` for `INT`; `f * 1.0`, `f / 1.0`, `f ^ 1` for
   `FLOAT`. `f * 0.0` and `f + 0.0` are left alone because of NaN and signed
   zero.
3. Local copy propagation.
4. Branches on constants become jumps or disappear; unreachable code and unused
   labels are removed.
5. Dead code elimination from backward liveness, with forwarding of single use
   temporaries.

Operations that can fail at run time (division, `MOD`, negative integer
exponent) are never folded, moved or removed, so errors and their line
numbers survive optimization.

## Code generation

`INT` is `long long`, compiled with `-fwrapv`. `FLOAT` is `double` printed with
`%g`. `BOOL` is `int` printed as `TRUE` or `FALSE`. Leo variables become
`v_<name>` so they cannot clash with C keywords or library names; temporaries
are renumbered above any user name that looks like one. Helpers for checked
division, `MOD`, integer power, `INPUT` and failure reporting are emitted only
when used. Each IR instruction appears as a comment before its C statement.
GCC is run as `gcc -O2 -fwrapv -o exe file.c -lm` without a shell.

## Testing strategy

Golden output tests per phase, a driver and back end group, end to end
programs built and run in both optimization modes, and the original generator
checks. A random program generator with a separate reference interpreter
compares the optimized build, the unoptimized build and the expected output.
