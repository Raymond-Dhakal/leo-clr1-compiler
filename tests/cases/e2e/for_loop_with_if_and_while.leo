INT c
FOR INT i <- 1 TO 6 DO
    IF (i MOD 2 == 0) THEN
        INT k <- i
        WHILE (k > 0) DO
            c <- c + 1
            k <- k ~ 2
        END
    END
END
PRINTN c
