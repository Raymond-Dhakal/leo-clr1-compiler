# Leo Language Specification

Leo is a small imperative language. Source files use the extension `.leo`.
This document is the reference for what the compiler `leoc` accepts and what
the generated program does. The machine readable grammar is
`language/grammar.txt`; the compiler loads that file at start up and builds a
canonical CLR(1) parser from it (856 states, 0 conflicts).

## 1. Lexical structure

| Item | Rule |
|---|---|
| Whitespace | spaces, tabs and newlines separate tokens; layout carries no meaning |
| Comments | `#` to the end of the line |
| Identifiers | a letter or `_`, then letters, digits or `_`; case sensitive |
| Keywords | `INT FLOAT BOOL IF THEN ELSE END WHILE DO REPEAT UNTIL INPUT PRINT PRINTN AND OR NOT MOD TRUE FALSE` (upper case only, reserved) |
| Line-ending print token | `PRINTN` (prints its items and ends the line; it can also be used alone to print a blank line) |
| Integer literal | one or more digits, for example `42` |
| Float literal | digits, a dot, digits, for example `3.14` (a leading dot, a trailing dot and exponents are rejected) |
| String literal | text in double quotes, only in `PRINT` and `INPUT`; escapes `\n`, `\t`, `\\` and `\"` |
| Statement terminator | none; a statement ends where the next one starts |

Malformed input is reported with line and column. Examples: an unknown
character, `=` where `<-` or `==` was meant, `-` (Leo subtracts with `~`),
`12abc`, `1.2.3`, `;`, character literals, an unterminated string, an unknown escape, and an integer literal
that does not fit in 64 bits.

## 2. Types

| Type | Meaning | Default value | Run time representation |
|---|---|---|---|
| `INT` | 64 bit signed integer, wraps on overflow | `0` | `long long` |
| `FLOAT` | double precision floating point | `0.0` | `double`, printed with `%g` |
| `BOOL` | `TRUE` or `FALSE` | `FALSE` | `int`, printed as `TRUE` or `FALSE` |

Rules:

* Every variable must be declared before use. A declaration without an
  initialiser gives the default value above.
* The only implicit conversion is `INT` to `FLOAT` (widening). Assigning a
  `FLOAT` to an `INT` variable is an error. `BOOL` never converts to or from a
  number.
* An arithmetic operator with one `INT` and one `FLOAT` operand works in
  `FLOAT`.
* `INT / INT` is integer division, truncating toward zero. Assigning it to a
  `FLOAT` variable converts the already truncated result.

## 3. Statements

```
Type id                         declaration, value is the default
Type id <- Expr                 declaration with initialiser
id <- Expr                      assignment
IF ( Expr ) THEN stmts END
IF ( Expr ) THEN stmts ELSE stmts END
WHILE ( Expr ) DO stmts END
REPEAT stmts UNTIL ( Expr )     body runs at least once, stops when Expr is TRUE
FOR [Type] id <- Expr TO Expr [STEP Expr] DO stmts END
FOR ( [Type] id <- Expr TO Expr [STEP Expr] ) DO stmts END
INPUT id                        read one value from standard input
INPUT "prompt", id              print the prompt (no newline), then read a value
PRINT item {, item}              items are expressions or strings; separated by one space, no newline
PRINTN [item {, item}]           same output, then a newline; no items prints a blank line
```

Details:

* `FOR` counts from the initial value to the final value, both inclusive. The
  step defaults to 1. A positive step counts up, a negative step counts down
  (`FOR i <- 10 TO 1 STEP ~1`). If the start is already past the limit in the
  direction of the step, the body does not run; the direction is never
  reversed automatically.
* The limit and step are evaluated once, before the first iteration. Changing
  their variables inside the body does not affect the loop.
* The loop variable is `INT` or `FLOAT`. With `FOR INT i <- ...` it is a new
  variable local to the loop; without a type it must already be declared.
  Bounds are checked before the new name enters scope. A `FLOAT` bound cannot
  feed an `INT` variable.
* A constant step of zero is a compile time error. A step that is zero at run
  time stops the program with `FOR step is zero`.
* The increment cannot wrap around: a loop whose next value would overflow
  `INT` ends instead of repeating forever.
* The prompt of `INPUT "text", x` is flushed before the read, so it is visible
  while the program waits. `PRINT "a", x, "b"` prints `a 5 b` without ending
  the line; `PRINTN "a", x, "b"` prints the same text and ends the line.

* Conditions of `IF`, `WHILE` and `REPEAT ... UNTIL` must have type `BOOL`.
  `IF (n)` with an `INT` is an error; write `IF (n != 0)`.
* Each of `THEN`, `ELSE`, `DO` and `REPEAT` bodies is its own scope. An inner
  declaration may shadow an outer one. Declaring the same name twice in one
  scope is an error. A name is not visible before its declaration finishes,
  so `INT x <- x + 1` is an error.
* A declaration inside a loop body runs on every iteration, so its variable is
  reset to the initialiser (or the default value) each time.
* `INPUT` reads a whitespace separated token. `INT` and `FLOAT` use the usual
  decimal forms. `BOOL` accepts `TRUE`, `FALSE`, `true`, `false`, `1`, `0`.
  Anything else is a run time error.

## 4. Operators and precedence

From the highest precedence to the lowest. `~` is subtraction when it has two
operands and negation when it has one.

| Level | Operators | Associativity | Operand types | Result |
|---|---|---|---|---|
| 1 | `( )` | none | any | same |
| 2 | `NOT`, unary `~` | prefix, right | `NOT`: BOOL; `~`: INT or FLOAT | BOOL; same as operand |
| 3 | `^` | right | INT or FLOAT | INT if both INT, else FLOAT |
| 4 | `*`  `/`  `MOD` | left | `MOD`: INT only; others INT or FLOAT | INT if both INT, else FLOAT |
| 5 | `+`  `~` | left | INT or FLOAT | INT if both INT, else FLOAT |
| 6 | `<`  `>`  `<=`  `>=` | left | INT or FLOAT | BOOL |
| 7 | `==`  `!=` | left | both numeric, or both BOOL | BOOL |
| 8 | `AND` | left | BOOL | BOOL |
| 9 | `OR` | left | BOOL | BOOL |

The assignment symbol is `<-`. It is a statement, not an operator, and has no
value.

Consequences worth knowing:

* `2 ^ 3 ^ 2` is `2 ^ (3 ^ 2)` = 512.
* `20 ~ 5 ~ 3` is `(20 ~ 5) ~ 3` = 12.
* Unary `~` binds tighter than `^`, so `~2 ^ 2` is `(~2) ^ 2` = 4, while
  `~(2 ^ 2)` is -4.
* `AND` and `OR` short circuit: the right operand is not evaluated when the
  left one decides the result. `FALSE AND 1 / 0 > 1` does not fail.
* `INT ^ INT` is exact integer exponentiation with wrap around. A negative
  exponent is a run time error; use a `FLOAT` base for that. If either operand
  is `FLOAT` the C `pow()` function is used.

## 5. Run time errors

A generated program stops with exit status 1 and a message on standard error
of the form `runtime error (line N): ...` for:

* `INT` division or `MOD` by zero, and `FLOAT` division by zero
* a negative exponent in an `INT` power
* an `INPUT` value that cannot be read or parsed

When the divisor is a constant zero the compiler also prints a warning at
compile time. The compiler never folds or deletes an operation that could trap,
so the error still happens when the code is executed.

## 6. Compile time diagnostics

Every message has the form `file:line:column: kind: text`, followed by the
source line and a caret. Lexical, syntax and semantic phases each report all
the errors they find in their own phase, and later phases do not run after an
earlier failure. Syntax errors list the tokens that would have been accepted
at that point.

## 7. Complete example

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

With input `5` this prints `120`.
