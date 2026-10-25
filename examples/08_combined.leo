# Combines declarations, INPUT, loops, conditions and logic.
# For the number n it prints n!, whether n is prime, and gcd(n, 12).
INT n
INPUT n

INT fact <- 1
INT i <- 2
WHILE (i <= n) DO
    fact <- fact * i
    i <- i + 1
END
PRINTN fact

BOOL prime <- n > 1
INT d <- 2
WHILE (d * d <= n AND prime) DO
    IF (n MOD d == 0) THEN
        prime <- FALSE
    END
    d <- d + 1
END
PRINTN prime

INT x <- n
INT y <- 12
REPEAT
    INT r <- x MOD y
    x <- y
    y <- r
UNTIL (y == 0)
PRINTN x
