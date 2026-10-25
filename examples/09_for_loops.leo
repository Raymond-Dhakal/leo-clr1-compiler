# FOR loops: counting up, counting down, custom steps, FLOAT loops, nesting.
# Forms:  FOR i <- start TO end DO ... END
#         FOR i <- start TO end STEP s DO ... END
#         FOR (INT i <- 10 TO 1 STEP ~3) DO ... END     (declares i; parentheses optional)
# The three values are evaluated once. STEP defaults to 1; a negative STEP counts down.
# If the range is empty (for example 5 TO 1 with a positive step) the body never runs.

INT i
FOR i <- 1 TO 5 DO
    PRINTN "up", i
END

FOR (INT j <- 10 TO 1 STEP ~3)
DO
    PRINTN "down", j
END

FLOAT f
FOR f <- 0 TO 1 STEP 0.25 DO
    PRINTN "float", f
END

FOR INT row <- 1 TO 3 DO
    FOR INT col <- 1 TO 3 DO
        PRINTN row, "x", col, "=", row * col
    END
END

INT total
FOR INT k <- 1 TO 100 DO
    total <- total + k
END
PRINTN "sum 1..100 =", total

FOR INT never <- 5 TO 1 DO
    PRINTN "not printed"
END
PRINTN "after the empty loop"
