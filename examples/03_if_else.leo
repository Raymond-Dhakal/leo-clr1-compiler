# IF / ELSE / END, including a nested IF. Reads one integer.
INT x
INPUT x

IF (x > 5) THEN
    PRINTN x
ELSE
    PRINTN 0
END

# ELSE is optional
IF (x MOD 2 == 0) THEN
    PRINTN 1000
END

# nested conditions
IF (x >= 90) THEN
    PRINTN 4
ELSE
    IF (x >= 70) THEN
        IF (x >= 80) THEN
            PRINTN 3
        ELSE
            PRINTN 2
        END
    ELSE
        PRINTN 1
    END
END
