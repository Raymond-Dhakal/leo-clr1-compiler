INT n
INPUT n
INT steps
WHILE (n != 1) DO
    IF (n MOD 2 == 0) THEN
        n <- n / 2
    ELSE
        n <- 3 * n + 1
    END
    steps <- steps + 1
END
PRINTN steps
