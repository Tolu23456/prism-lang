# Benchmark: Fibonacci (recursive) — fib(32)
# Classic tree recursion: 2^32 ≈ 4 billion calls in a naive count,
# actually 2*fib(33)-1 calls ≈ 7.8 million.

func fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

let result = fib(32)
output(result)
