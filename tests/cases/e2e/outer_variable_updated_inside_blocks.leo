INT total
INT i <- 1
WHILE (i <= 4) DO
    IF (i MOD 2 == 0) THEN
        total <- total + i
    END
    i <- i + 1
END
PRINTN total
