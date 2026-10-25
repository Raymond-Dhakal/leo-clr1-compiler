# CLR(1) Parser Generator (original component)

Design and Implementation of a CLR Parser Generator Using Canonical LR(1)
Item Sets, Lookahead Propagation, Closure-GOTO Construction and Conflict
Analysis.

A working Canonical LR(1) parser generator in C: give it a context-free
grammar, and it computes FIRST sets, builds the canonical collection of
LR(1) item sets (full CLR(1), not SLR or LALR), constructs ACTION/GOTO
parsing tables, detects shift-reduce and reduce-reduce conflicts, and
parses an input string against the result.

## Team

| Member | Registration | Module |
|---|---|---|
| Gurutheja Kavuturi | 24BEL0086 | Grammar Engine & Lexer (`grammar.c`, `lexer.c`) |
| Ephraim Immanuel | 24BEL0091 | FIRST/FOLLOW & Lookahead (`first.c`) |
| Goyal Hemant Anil | 24BYB0059 | LR(1) Items, Closure & GOTO (`lr1_item.c`, `closure.c`, `goto.c`) |
| Raymond Dhakal | 24BEL0100 | Canonical Collection, Parsing Tables, Conflicts, Integration (`state.c`, `canonical.c`, `parsing_table.c`, `conflict.c`, `parser.c`, `main.c`) |

## Building

Requires CMake >= 3.16 and a C11 compiler.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

This produces the `clr_parser` executable. (It has also been verified to
compile cleanly with plain gcc: `gcc -Wall -Wextra -Iinclude -o clr_parser src/*.c`.)

## Running

```bash
./clr_parser <grammar_file> [input_string] [-v]
```

- `<grammar_file>` — path to a grammar written in the format below.
- `[input_string]` — optional, whitespace-separated terminal tokens to
  parse (e.g. `"id + id * id"`). If omitted, the tool still prints the
  grammar, FIRST sets, canonical collection, and parsing table.
- `-v` — print a full shift/reduce/goto/accept trace during parsing.

Example:

```bash
./clr_parser tests/grammars/expression_grammar.txt "id + id * id" -v
```

See `examples/sample_output.txt` for a full captured run.

## Grammar input format

```
%start E
E -> E + T | T
T -> T * F | F
F -> ( E ) | id
```

Rules:

- Tokens are whitespace-separated. There is no single-character
  shorthand — every terminal/non-terminal is a space-delimited word
  (so `id`, `+`, `(`, `)` are each their own token).
- `%start X` declares the grammar's start symbol `X`. Required exactly
  once per file.
- `->` separates a production's LHS from its RHS.
- `|` separates alternative right-hand sides for the same LHS.
- `EPSILON` denotes an empty production body, e.g. `A -> EPSILON`.
- `$` (end-of-input) is reserved and added automatically — it must
  never appear in a grammar file.
- Any symbol that never appears as the LHS of a production is
  automatically classified as a **terminal**. Any symbol that does
  appear as an LHS is a **non-terminal**.
- Lines beginning with `#` and blank lines are ignored (comments).
- The tool automatically augments the grammar with `S' -> S` before
  building the canonical collection, where `S` is the declared start
  symbol.

Input strings to be parsed use the same whitespace-separated terminal
tokens, e.g. `id + id * id` or `if id then id else id`.

## Pipeline / architecture

```
Grammar file
   │  grammar.c (Member 1)
   ▼
Grammar (symbols, productions, augmented grammar)
   │  first.c (Member 2)
   ▼
FIRST sets + nullability
   │  lr1_item.c / closure.c / goto.c (Member 3)
   ▼
CLOSURE(I), GOTO(I, X)  — full LR(1), lookaheads via FIRST(beta a)
   │  state.c / canonical.c (Member 4)
   ▼
Canonical collection C = {I0, I1, ...} with transitions
   │  parsing_table.c + conflict.c (Member 4)
   ▼
ACTION[state][terminal], GOTO[state][nonterminal], conflict log
   │  parser.c (Member 4)
   ▼
Shift-reduce-goto-accept parse of the input string
```

Module dependency is strictly top-to-bottom in that diagram; there are
no circular includes. Every module reuses the `Symbol`/`Production`/
`Grammar` integer-ID representation defined once in `grammar.h`.

## Shared data structures

All defined once and reused everywhere (see the corresponding header
for the authoritative definition):

- `Symbol`, `Production`, `Grammar` — `include/grammar.h`
- `SymbolSet`, `FirstSetsTable` — `include/first.h`
- `LR1Item` — `include/lr1_item.h`
- `ItemSet` — `include/closure.h`
- `Transition`, `State` — `include/state.h`
- `CanonicalCollection` — `include/canonical.h`
- `ParsingAction`, `Conflict`, `ConflictList` — `include/conflict.h`
- `ParsingTable` — `include/parsing_table.h`

## Working independently on branches

Because every module only depends on the headers of the modules
*above* it in the pipeline (see the dependency diagram), each member
can implement and test their `.c` file against the already-frozen
headers without needing the other members' implementations to exist
yet — a stub/mock returning dummy data is enough to unblock the next
layer. Suggested branch-per-member workflow:

1. `main` always contains the current agreed-upon headers in
   `include/` — changes to shared headers should be discussed by the
   team before merging, since every other branch depends on them.
2. Each member works on `feature/<name>-<module>` (e.g.
   `feature/ephraim-first-sets`) and only edits their own `.c` file(s)
   plus, if genuinely necessary, their own header.
3. `tests/grammars/*.txt` are shared fixtures; add new ones instead of
   editing existing ones so other members' tests don't silently change.
4. Merge into `main` via pull request once a module compiles against
   the shared headers and passes its own small demo/test.

## Test grammars

| File | Purpose |
|---|---|
| `tests/grammars/simple_grammar.txt` | Minimal grammar (`S -> A A`, `A -> aA \| b`) for sanity-checking closure/GOTO/canonical construction (10 canonical states, well-known result). |
| `tests/grammars/expression_grammar.txt` | Classic left-recursive arithmetic expression grammar. Unambiguous CLR(1) grammar — zero conflicts. |
| `tests/grammars/epsilon_grammar.txt` | Exercises an `EPSILON` production and nullability propagation. |
| `tests/grammars/conflict_grammar.txt` | Dangling-else style grammar that deliberately produces a shift-reduce conflict, to exercise `conflict.c`. |

`tests/valid_inputs/`, `tests/invalid_inputs/`, and `tests/edge_cases/`
contain sample input strings (one per line) to run against the
expression grammar.

## Current status (Review 1)

Implemented and verified:

- Grammar loading, augmentation, terminal/non-terminal classification.
- FIRST sets + nullability via fixed-point iteration.
- LR(1) items with explicit lookahead (`[A -> alpha . beta, a]`).
- Full Canonical LR(1) CLOSURE (lookaheads via `FIRST(beta a)`, not the
  LALR/SLR `FOLLOW(B)` shortcut) and GOTO.
- Canonical collection construction with state merging.
- ACTION/GOTO table construction, including accept action.
- Shift-reduce and reduce-reduce conflict detection and reporting
  (conflicting actions are never silently overwritten).
- Table-driven shift/reduce/goto/accept parser with a verbose trace
  mode.

Verified against the standard textbook example grammars (`S -> A A`,
`A -> aA | b` produces the well-known 10-state canonical collection;
the arithmetic expression grammar is conflict-free; the dangling-else
grammar correctly reports exactly one shift-reduce conflict).
