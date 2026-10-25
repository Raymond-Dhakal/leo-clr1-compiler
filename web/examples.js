// Generated from ../examples/*.leo by tools/make_web.py. Do not edit by hand.
export const EXAMPLES = [
 {
  "id": "01_arithmetic",
  "title": "Arithmetic operators",
  "source": "# Arithmetic with the custom operators: ~ is subtraction, ^ is power, MOD is remainder.\nINT a <- 10\nINT b <- 5\nINT sum\nINT difference\nINT product\nINT quotient\nINT power\nINT remainder\n\nsum <- a + b\ndifference <- a ~ b\nproduct <- a * b\nquotient <- a / b\npower <- a ^ 2\nremainder <- a MOD 3\n\nPRINTN sum\nPRINTN difference\nPRINTN product\nPRINTN quotient\nPRINTN power\nPRINTN remainder\n",
  "stdin": ""
 },
 {
  "id": "02_precedence",
  "title": "Precedence and associativity",
  "source": "# Operator precedence, associativity and exponentiation.\n# Highest to lowest: ( )  |  NOT, unary ~  |  ^ (right assoc)  |  * / MOD  |  + ~  |  < > <= >=  |  == !=  |  AND  |  OR\nINT r\n\nr <- 2 + 3 * 4\nPRINTN r                 # 14\n\nr <- (2 + 3) * 4\nPRINTN r                 # 20\n\nr <- 2 ^ 3 ^ 2\nPRINTN r                 # ^ is right associative: 2 ^ (3 ^ 2) = 512\n\nr <- 20 ~ 5 ~ 3\nPRINTN r                 # subtraction is left associative: (20 ~ 5) ~ 3 = 12\n\nr <- 100 / 10 / 5\nPRINTN r                 # (100 / 10) / 5 = 2\n\nr <- 2 * 3 ^ 2\nPRINTN r                 # 2 * (3 ^ 2) = 18\n\nr <- ~2 ^ 2\nPRINTN r                 # unary ~ binds tighter than ^ : (~2) ^ 2 = 4\n\nr <- ~(2 ^ 2)\nPRINTN r                 # -4\n\nr <- 17 MOD 5 * 2\nPRINTN r                 # (17 MOD 5) * 2 = 4\n\nPRINTN 7 / 2             # INT / INT is integer division: 3\nPRINTN 7 / 2.0           # 3.5\nPRINTN 2 ^ 0.5           # a FLOAT operand makes ^ use pow(): 1.41421\n",
  "stdin": ""
 },
 {
  "id": "03_if_else",
  "title": "IF / ELSE",
  "source": "# IF / ELSE / END, including a nested IF. Reads one integer.\nINT x\nINPUT x\n\nIF (x > 5) THEN\n    PRINTN x\nELSE\n    PRINTN 0\nEND\n\n# ELSE is optional\nIF (x MOD 2 == 0) THEN\n    PRINTN 1000\nEND\n\n# nested conditions\nIF (x >= 90) THEN\n    PRINTN 4\nELSE\n    IF (x >= 70) THEN\n        IF (x >= 80) THEN\n            PRINTN 3\n        ELSE\n            PRINTN 2\n        END\n    ELSE\n        PRINTN 1\n    END\nEND\n",
  "stdin": ""
 },
 {
  "id": "04_while_loop",
  "title": "WHILE loop",
  "source": "# WHILE tests its condition before every iteration.\nINT i <- 1\nINT total <- 0\nWHILE (i <= 5) DO\n    total <- total + i * i\n    PRINTN total\n    i <- i + 1\nEND\n\n# the body never runs when the condition is false from the start\nINT n <- 10\nWHILE (n < 5) DO\n    PRINTN 999\nEND\nPRINTN n\n",
  "stdin": ""
 },
 {
  "id": "05_repeat_until",
  "title": "REPEAT UNTIL",
  "source": "# REPEAT ... UNTIL runs the body first and stops when the condition becomes true.\nINT n <- 100\nREPEAT\n    PRINTN n\n    n <- n ~ 30\nUNTIL (n < 0)\n\n# the body executes at least once, even though the condition is true at once\nINT k <- 10\nREPEAT\n    PRINTN k\nUNTIL (k > 5)\n",
  "stdin": ""
 },
 {
  "id": "06_logical",
  "title": "Logical operators",
  "source": "# AND, OR, NOT on BOOL values, and short-circuit evaluation.\nBOOL p <- TRUE\nBOOL q <- FALSE\nPRINTN p AND q\nPRINTN p OR q\nPRINTN NOT p\nPRINTN NOT (p AND q)\nPRINTN 3 < 4 AND 4 < 5 OR FALSE\n\n# AND stops at the first FALSE, so 10 / x is never evaluated when x is 0\nINT x\nINPUT x\nIF (x != 0 AND 10 / x > 1) THEN\n    PRINTN 1\nELSE\n    PRINTN 2\nEND\n",
  "stdin": ""
 },
 {
  "id": "07_input_output",
  "title": "INPUT and PRINT",
  "source": "# INPUT reads a value of the variable's type; PRINT writes a value and a newline.\nINT a\nINT b\nFLOAT avg\nINPUT a\nINPUT b\nPRINTN a + b\navg <- (a + b) / 2.0\nPRINTN avg\nPRINTN a > b\n",
  "stdin": "3 8"
 },
 {
  "id": "08_combined",
  "title": "Factorial, prime, gcd",
  "source": "# Combines declarations, INPUT, loops, conditions and logic.\n# For the number n it prints n!, whether n is prime, and gcd(n, 12).\nINT n\nINPUT n\n\nINT fact <- 1\nINT i <- 2\nWHILE (i <= n) DO\n    fact <- fact * i\n    i <- i + 1\nEND\nPRINTN fact\n\nBOOL prime <- n > 1\nINT d <- 2\nWHILE (d * d <= n AND prime) DO\n    IF (n MOD d == 0) THEN\n        prime <- FALSE\n    END\n    d <- d + 1\nEND\nPRINTN prime\n\nINT x <- n\nINT y <- 12\nREPEAT\n    INT r <- x MOD y\n    x <- y\n    y <- r\nUNTIL (y == 0)\nPRINTN x\n",
  "stdin": "12"
 },
 {
  "id": "09_for_loops",
  "title": "FOR loops",
  "source": "# FOR loops: counting up, counting down, custom steps, FLOAT loops, nesting.\n# Forms:  FOR i <- start TO end DO ... END\n#         FOR i <- start TO end STEP s DO ... END\n#         FOR (INT i <- 10 TO 1 STEP ~3) DO ... END     (declares i; parentheses optional)\n# The three values are evaluated once. STEP defaults to 1; a negative STEP counts down.\n# If the range is empty (for example 5 TO 1 with a positive step) the body never runs.\n\nINT i\nFOR i <- 1 TO 5 DO\n    PRINTN \"up\", i\nEND\n\nFOR (INT j <- 10 TO 1 STEP ~3)\nDO\n    PRINTN \"down\", j\nEND\n\nFLOAT f\nFOR f <- 0 TO 1 STEP 0.25 DO\n    PRINTN \"float\", f\nEND\n\nFOR INT row <- 1 TO 3 DO\n    FOR INT col <- 1 TO 3 DO\n        PRINTN row, \"x\", col, \"=\", row * col\n    END\nEND\n\nINT total\nFOR INT k <- 1 TO 100 DO\n    total <- total + k\nEND\nPRINTN \"sum 1..100 =\", total\n\nFOR INT never <- 5 TO 1 DO\n    PRINTN \"not printed\"\nEND\nPRINTN \"after the empty loop\"\n",
  "stdin": ""
 },
 {
  "id": "10_prompts_and_print",
  "title": "Prompts and PRINT lists",
  "source": "# INPUT with a prompt and PRINT with several items.\n#   INPUT \"prompt\", x     shows the prompt, then reads x\n#   INPUT x               reads x without a prompt\n#   PRINT \"text\", expr    items are separated by a space; strings take \\n \\t \\\" \\\\\nINT n\nFLOAT rate\nBOOL verbose\nINPUT \"How many numbers? \", n\nINPUT \"Interest rate (percent)? \", rate\nINPUT \"Verbose (TRUE/FALSE)? \", verbose\n\nINT sum\nFOR INT i <- 1 TO n DO\n    sum <- sum + i\n    IF (verbose) THEN\n        PRINTN \"running total after\", i, \"is\", sum\n    END\nEND\nPRINTN \"sum =\", sum\nPRINTN \"interest on the sum:\", sum * rate / 100\nPRINTN \"tab\\tseparated\\tand a \\\"quoted\\\" word\"\n",
  "stdin": "4 5 TRUE"
 }
];
