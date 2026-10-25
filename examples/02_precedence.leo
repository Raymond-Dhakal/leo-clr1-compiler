# Operator precedence, associativity and exponentiation.
# Highest to lowest: ( )  |  NOT, unary ~  |  ^ (right assoc)  |  * / MOD  |  + ~  |  < > <= >=  |  == !=  |  AND  |  OR
INT r

r <- 2 + 3 * 4
PRINTN r                 # 14

r <- (2 + 3) * 4
PRINTN r                 # 20

r <- 2 ^ 3 ^ 2
PRINTN r                 # ^ is right associative: 2 ^ (3 ^ 2) = 512

r <- 20 ~ 5 ~ 3
PRINTN r                 # subtraction is left associative: (20 ~ 5) ~ 3 = 12

r <- 100 / 10 / 5
PRINTN r                 # (100 / 10) / 5 = 2

r <- 2 * 3 ^ 2
PRINTN r                 # 2 * (3 ^ 2) = 18

r <- ~2 ^ 2
PRINTN r                 # unary ~ binds tighter than ^ : (~2) ^ 2 = 4

r <- ~(2 ^ 2)
PRINTN r                 # -4

r <- 17 MOD 5 * 2
PRINTN r                 # (17 MOD 5) * 2 = 4

PRINTN 7 / 2             # INT / INT is integer division: 3
PRINTN 7 / 2.0           # 3.5
PRINTN 2 ^ 0.5           # a FLOAT operand makes ^ use pow(): 1.41421
