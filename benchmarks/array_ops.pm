# Benchmark: Array operations — build, sequential read, slice — 100,000 elements
# Measures array growth (add), index read loop, and slice allocation.

let items = []
let i = 0
while i < 100000 {
    items.add(i)
    i += 1
}

let total = 0
let j = 0
while j < 100000 {
    total += items[j]
    j += 1
}

let sl = items[0:1000]
output(total)
output(len(sl))
