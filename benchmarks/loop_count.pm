# Benchmark: tight counting loop — 5,000,000 iterations
# Measures the raw per-iteration cost of the interpreter.

let i = 0
let n = 5000000
while i < n {
    i += 1
}
output(i)
