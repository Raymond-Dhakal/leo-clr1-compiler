INT i <- 1
WHILE (i <= 3) DO
    INT j <- 1
    INT line
    REPEAT
        line <- line * 10 + i * j
        j <- j + 1
    UNTIL (j > 3)
    PRINTN line
    i <- i + 1
END
