# Benchmark: Recursive sum 1..200 — called 5,000 times
# Measures function-call overhead: 5,000 × 200 = 1,000,000 recursive calls total.
# (VM_FRAME_MAX = 256; depth-200 is safely within limit)

func rsum(n) {
    if n <= 0 { return 0 }
    return n + rsum(n - 1)
}

let i = 0
let total = 0
while i < 5000 {
    total += rsum(200)
    i += 1
}
output(total)
