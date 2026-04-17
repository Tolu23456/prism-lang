# test_control.pm — control flow

# if / elif / else
let x = 85
let grade = ""
if x >= 90 {
    grade = "A"
} elif x >= 80 {
    grade = "B"
} elif x >= 70 {
    grade = "C"
} else {
    grade = "F"
}
assert_eq(grade, "B", "if/elif/else")

# ternary-style with inline if
let sign = "pos"
if x < 0 { sign = "neg" }
assert_eq(sign, "pos", "if no-else")

# while + break
let count = 0
while count < 10 {
    if count == 5 { break }
    count += 1
}
assert_eq(count, 5, "while break")

# while + continue
let evens = []
let n = 0
while n < 10 {
    n += 1
    if n % 2 != 0 { continue }
    evens.add(n)
}
assert_eq(evens, [2, 4, 6, 8, 10], "while continue")

# for loop
let sum = 0
for i in [1, 2, 3, 4, 5] {
    sum += i
}
assert_eq(sum, 15, "for loop sum")

# for with break
let found = -1
for item in [10, 20, 30, 40] {
    if item == 30 { found = item; break }
}
assert_eq(found, 30, "for break")

# for over string characters
let chars = []
for ch in "abc" {
    chars.add(ch)
}
assert_eq(chars, ["a", "b", "c"], "for over string")

# nested loops
let pairs = []
for i in [1, 2] {
    for j in [3, 4] {
        pairs.add([i, j])
    }
}
assert_eq(len(pairs), 4, "nested for len")

# logical operators
assert_eq(true && true,  true,  "&& tt")
assert_eq(true && false, false, "&& tf")
assert_eq(false || true, true,  "|| ft")
assert_eq(!false,        true,  "! false")

# comparison
assert(1 < 2,   "1 < 2")
assert(2 <= 2,  "2 <= 2")
assert(3 > 2,   "3 > 2")
assert(3 >= 3,  "3 >= 3")
assert(1 == 1,  "1 == 1")
assert(1 != 2,  "1 != 2")

output("[PASS] test_control")
