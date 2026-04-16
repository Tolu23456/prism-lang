# Benchmark: Sieve of Eratosthenes up to 500_000
# Measures array indexing, boolean assignment, nested loops.

let limit = 500000
let sieve = []
let i = 0
while i <= limit {
    sieve.add(true)
    i += 1
}
sieve[0] = false
sieve[1] = false

let p = 2
while p * p <= limit {
    if sieve[p] {
        let j = p * p
        while j <= limit {
            sieve[j] = false
            j += p
        }
    }
    p += 1
}

let count = 0
let k = 2
while k <= limit {
    if sieve[k] { count += 1 }
    k += 1
}
output(count)
