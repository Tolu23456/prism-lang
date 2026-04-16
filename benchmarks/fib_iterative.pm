# Benchmark: Fibonacci (iterative) — fib(2_000_000 steps)
# Measures raw loop + variable-update throughput.

func fib_iter(n) {
    let a = 0
    let b = 1
    let i = 0
    while i < n {
        let tmp = a + b
        a = b
        b = tmp
        i += 1
    }
    return a
}

let result = fib_iter(2000000)
output("done")
