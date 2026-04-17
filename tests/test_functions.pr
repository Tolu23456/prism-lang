# test_functions.pm — functions, recursion, closures

# Basic function
func add(a, b) { return a + b }
assert_eq(add(3, 4), 7, "basic function")

# Default-style call
func greet(name) { return f"Hello, {name}!" }
assert_eq(greet("Alice"), "Hello, Alice!", "string return")

# Recursion
func factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
assert_eq(factorial(5),  120,     "factorial(5)")
assert_eq(factorial(10), 3628800, "factorial(10)")

# Fibonacci
func fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
assert_eq(fib(10), 55, "fib(10)")

# Higher-order: function taking another function
func double(x) { return x * 2 }
func apply_twice(f, x) { return f(f(x)) }
assert_eq(apply_twice(double, 3), 12, "apply_twice")

# Named function used as value
func square(x) { return x * x }
assert_eq(square(6), 36, "square function")

# Multiple return paths
func abs_val(n) {
    if n < 0 { return -n }
    return n
}
assert_eq(abs_val(-7), 7, "abs negative")
assert_eq(abs_val(3),  3, "abs positive")

# Functions as arguments
func apply(f, val) { return f(val) }
assert_eq(apply(square, 4), 16, "function as argument")

# Accumulator using outer scope
let total = 0
func accumulate(n) { total += n }
accumulate(10)
accumulate(20)
accumulate(5)
assert_eq(total, 35, "accumulator side-effect")

output("[PASS] test_functions")
