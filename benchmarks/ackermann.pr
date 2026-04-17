# Benchmark: Ackermann function — ack(3, 5) × 100 iterations
# ack(3,5) = 253, makes ~10,307 recursive calls per invocation.
# Max stack depth ~125 frames — within VM_FRAME_MAX = 256.
# Total: ~1,030,700 recursive function calls across all iterations.

func ack(m, n) {
    if m == 0 { return n + 1 }
    if n == 0 { return ack(m - 1, 1) }
    return ack(m - 1, ack(m, n - 1))
}

let i = 0
let total = 0
while i < 100 {
    total += ack(3, 5)
    i += 1
}
output(total)
