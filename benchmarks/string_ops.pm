# Benchmark: String operations — split, join, upper, contains — 50,000 iterations
# Measures string method dispatch and allocation throughput.

let words = ["alpha", "bravo", "charlie", "delta", "echo"]
let i = 0
let count = 0
while i < 50000 {
    let s = "-".join(words)
    let parts = s.split("-")
    let u = parts[0].upper()
    if u.startswith("A") {
        count += 1
    }
    i += 1
}
output(count)
