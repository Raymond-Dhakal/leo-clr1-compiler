# WHILE tests its condition before every iteration.
INT i <- 1
INT total <- 0
WHILE (i <= 5) DO
    total <- total + i * i
    PRINTN total
    i <- i + 1
END

# the body never runs when the condition is false from the start
INT n <- 10
WHILE (n < 5) DO
    PRINTN 999
END
PRINTN n
