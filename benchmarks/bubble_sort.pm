# Benchmark: Bubble Sort — 800 elements (worst case: reversed order)
# Measures array read/write under O(n^2) comparisons and swaps.

let nums = []
let i = 800
while i > 0 {
    nums.add(i)
    i -= 1
}

let size = len(nums)
let outer = 0
while outer < size - 1 {
    let inner = 0
    while inner < size - outer - 1 {
        if nums[inner] > nums[inner + 1] {
            let tmp = nums[inner]
            nums[inner] = nums[inner + 1]
            nums[inner + 1] = tmp
        }
        inner += 1
    }
    outer += 1
}

output(nums[0])
output(nums[size - 1])
