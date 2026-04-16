# Benchmark: Dictionary insert + lookup — 10,000 entries
# Measures hash-map write and read throughput.
# Note: {} creates an empty set; seed with one entry so the type is VAL_DICT.

let d = {"_seed": 0}
let i = 0
while i < 10000 {
    let key = str(i)
    d[key] = i * 2
    i += 1
}

let total = 0
let j = 0
while j < 10000 {
    let key = str(j)
    total += d[key]
    j += 1
}
output(total)
