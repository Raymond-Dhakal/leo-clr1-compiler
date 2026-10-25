# Arithmetic with the custom operators: ~ is subtraction, ^ is power, MOD is remainder.
INT a <- 10
INT b <- 5
INT sum
INT difference
INT product
INT quotient
INT power
INT remainder

sum <- a + b
difference <- a ~ b
product <- a * b
quotient <- a / b
power <- a ^ 2
remainder <- a MOD 3

PRINTN sum
PRINTN difference
PRINTN product
PRINTN quotient
PRINTN power
PRINTN remainder
