# AND, OR, NOT on BOOL values, and short-circuit evaluation.
BOOL p <- TRUE
BOOL q <- FALSE
PRINTN p AND q
PRINTN p OR q
PRINTN NOT p
PRINTN NOT (p AND q)
PRINTN 3 < 4 AND 4 < 5 OR FALSE

# AND stops at the first FALSE, so 10 / x is never evaluated when x is 0
INT x
INPUT x
IF (x != 0 AND 10 / x > 1) THEN
    PRINTN 1
ELSE
    PRINTN 2
END
