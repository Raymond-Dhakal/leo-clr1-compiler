# REPEAT ... UNTIL runs the body first and stops when the condition becomes true.
INT n <- 100
REPEAT
    PRINTN n
    n <- n ~ 30
UNTIL (n < 0)

# the body executes at least once, even though the condition is true at once
INT k <- 10
REPEAT
    PRINTN k
UNTIL (k > 5)
