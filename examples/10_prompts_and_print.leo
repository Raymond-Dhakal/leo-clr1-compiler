# INPUT with a prompt and PRINT with several items.
#   INPUT "prompt", x     shows the prompt, then reads x
#   INPUT x               reads x without a prompt
#   PRINT "text", expr    items are separated by a space; strings take \n \t \" \\
INT n
FLOAT rate
BOOL verbose
INPUT "How many numbers? ", n
INPUT "Interest rate (percent)? ", rate
INPUT "Verbose (TRUE/FALSE)? ", verbose

INT sum
FOR INT i <- 1 TO n DO
    sum <- sum + i
    IF (verbose) THEN
        PRINTN "running total after", i, "is", sum
    END
END
PRINTN "sum =", sum
PRINTN "interest on the sum:", sum * rate / 100
PRINTN "tab\tseparated\tand a \"quoted\" word"
