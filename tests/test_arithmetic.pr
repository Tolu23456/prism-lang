# test_arithmetic.pm — arithmetic operators

assert_eq(2 + 3,    5,    "int add")
assert_eq(10 - 4,   6,    "int sub")
assert_eq(3 * 4,    12,   "int mul")
assert_eq(10 % 3,   1,    "int mod")
assert_eq(2 ** 8,   256,  "int pow")
assert_eq(-5,       -5,   "unary neg")

assert_eq(1.5 + 0.5, 2.0, "float add")
assert_eq(3.0 * 2.0, 6.0, "float mul")

assert_eq(0xFF, 255,  "hex literal")
assert_eq(0b1010, 10, "binary literal")

let x = 10
x += 5
assert_eq(x, 15, "+= aug assign")
x -= 3
assert_eq(x, 12, "-= aug assign")
x *= 2
assert_eq(x, 24, "*= aug assign")

assert_eq(5 & 3,  1,  "bitwise and")
assert_eq(5 | 3,  7,  "bitwise or")
assert_eq(5 ^ 3,  6,  "bitwise xor")
assert_eq(~0,     -1, "bitwise not")

output("[PASS] test_arithmetic")
