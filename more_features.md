# Prism — More Features

---

## 0. Unique Prism Features

Ideas that don't exist (or don't exist in this form) in any mainstream language.

---

### 0.1 `repeat` Block

Run a block exactly N times without needing a loop variable.

```prism
repeat 5 {
    output "hello"
}

let board = repeat 8 { repeat 8 { 0 } }  # 8x8 grid of zeros
```

No index variable cluttering scope. If you need the index, use `for i in 0..n`.

---

### 0.2 Quantifier Keywords — `every`, `any`, `none`

Math-style quantifiers as first-class expressions over collections.

```prism
let nums = [2, 4, 6, 8]

every x in nums: x % 2 == 0   # → true  (all even)
any   x in nums: x > 5        # → true  (at least one)
none  x in nums: x < 0        # → true  (no negatives)

every student in class: student.grade >= 50   # readable predicate
```

Works on arrays, dicts, and `seq`. The `:` separates variable from condition.
Lazy — stops as soon as result is determined.

---

### 0.3 `between` Operator

Readable range check without double comparisons.

```prism
x between 1 and 10       # equivalent to x >= 1 and x <= 10
x between 0.0 and 1.0
age between 18 and 65

# Exclusive bounds with parens:
x between (0 and 10)     # 0 < x < 10 (exclusive)
```

---

### 0.4 `tally` Expression

Count occurrences of each unique value — returns a dict.

```prism
tally [1, 2, 1, 3, 2, 1]        # → {1: 3, 2: 2, 3: 1}
tally ["a", "b", "a", "c", "b"] # → {"a": 2, "b": 2, "c": 1}
tally words in sentence          # count word frequencies in a string
```

Also works as a method: `arr.tally()`.
Returns entries sorted by count descending by default.

---

### 0.5 `where` Bindings in Expressions

Define local names inline, right where you need them — no temp variables.

```prism
output area where area = width * height

let result = (a + b) / c
    where a = sensor_x * 2,
          b = sensor_y * 2,
          c = scale_factor

# In return statements:
return disc where disc = b**2 - 4*a*c
```

`where` bindings are scoped to the single expression they attach to.

---

### 0.6 Aggregator Expressions — `sum`, `avg`, `count`, `product`

Aggregate over a collection in one readable line.

```prism
sum     x in 1..100             # → 5050
avg     x in scores             # → mean of scores array
count   x in nums if x > 0      # → how many positives
product x in 1..10              # → 10! = 3628800
sum     x in students: x.grade  # field access in aggregation
```

These are expression-level keywords, not just method calls.

---

### 0.7 `swap` Statement

Swap two variables with no temporary.

```prism
swap a, b
swap arr[0], arr[-1]    # swap first and last element
swap obj.x, obj.y
```

Works on any assignable target — variables, array indices, object fields.

---

### 0.8 `inspect` Keyword

Like `output` but always prints the expression text AND its value — for
debugging without removing code.

```prism
inspect x + y         # prints:  x + y = 42
inspect arr.len()     # prints:  arr.len() = 7
inspect user.name     # prints:  user.name = "Alice"
```

`inspect` lines can be left in code — they disappear in `--release` builds
automatically. No need to search-and-remove debug prints before shipping.

---

### 0.9 Named Loop Labels — Multi-level `break` and `next`

Break or continue an outer loop by name.

```prism
outer: for row in grid {
    for col in row {
        if col == target {
            break outer      # exits the outer for loop entirely
        }
    }
}

search: for i in 0..n {
    for j in 0..n {
        if found { next search }   # skip to next iteration of outer loop
    }
}
```

---

### 0.10 `~=` Fuzzy Match Operator

Case-insensitive and pattern-flexible string comparison.

```prism
"Hello" ~= "hello"         # → true  (case-insensitive)
"  hello  " ~= "hello"     # → true  (ignores surrounding whitespace)
"café" ~= "cafe"           # → true  (accent-insensitive, best-effort)
```

Useful for user input, search, config matching. Distinct from `==` (exact)
and `contains` (substring). Does not require regex.

---

### 0.11 `cascade` Operator (`..`)

Call multiple methods on the same object without repeating the variable name.
Returns the original object, not the method result.

```prism
let list = []
    ..push(1)
    ..push(2)
    ..push(3)
    ..reverse()

# Equivalent to:
let list = []
list.push(1)
list.push(2)
list.push(3)
list.reverse()
```

Works on any object. The `..` at the start of a new line binds to the last
named value on the previous line.

---

### 0.12 `benchmark` Block

Built-in performance measurement — no imports required.

```prism
benchmark "sort 10k items" {
    let arr = 1..10000
    arr:
}

# Output:
# [benchmark] sort 10k items: 1.23ms (avg over 100 runs)
```

The runtime auto-runs the block multiple times and reports min/max/avg.
In `--release` builds the block still runs once but timing output is suppressed
unless `--bench` flag is passed.

---

### 0.13 `require` / `ensure` Function Contracts

Declare preconditions and postconditions directly on a function.
Failed contracts produce clear, located error messages.

```prism
func divide(a, b)
    require b != 0, "divisor must not be zero"
    require a is int and b is int
    ensure result is float
{
    return a / b
}

func sqrt(x)
    require x >= 0, "cannot take sqrt of negative number"
    ensure result >= 0
{
    return x ** 0.5
}
```

`require` checks run before the function body.
`ensure` checks run after, with `result` bound to the return value.
Both are stripped in `--release` builds (like assert).

---

### 0.14 `probe` — Structured Debug Snapshots

Capture a labeled snapshot of multiple values at once, printed as a table.

```prism
probe "loop state" { i, total, arr.len() }

# Output:
# ── probe: loop state (line 14) ──────────────
#   i         = 3
#   total     = 42
#   arr.len() = 10
# ─────────────────────────────────────────────
```

Like `inspect` but for multiple values at once, with a label.
Completely silent in `--release` builds.

---

### 0.15 `notebook` Mode

Run a `.pr` file with `prism --notebook file.pr` — every expression at the
top level automatically prints its value, like a REPL or Jupyter notebook.

```prism
# file: explore.pr
1 + 1           # prints: 2
"hello".upper() # prints: HELLO
1..5            # prints: [1, 2, 3, 4, 5]
```

No `output` calls needed. Great for exploration and data scripts.
Assignments are silent; bare expressions are printed with their source text.

---

### 0.16 Unit-Aware Number Literals

Numbers can carry a unit tag that is preserved through operations.

```prism
let d = 100km
let t = 2h
let speed = d / t    # → 50.0 km/h  (unit inferred)

let angle = 90deg
let rad   = angle.to_rad()   # → 1.5707...

let delay = 500ms
sleep(delay)          # functions that take time accept ms/s/us units

output 1_000_000 as currency   # → "$1,000,000.00"
output 3600s as time           # → "1h 0m 0s"
```

Units are: `km`, `m`, `cm`, `mm`, `kg`, `g`, `ms`, `s`, `min`, `h`,
`deg`, `rad`, `px`, `%`.
Arithmetic between compatible units auto-converts.
Incompatible unit operations produce a runtime error.

---

### 0.17 `given` / `otherwise` — Readable Conditional Assignment

A more expressive alternative to ternary for assignment contexts.

```prism
let label = "pass" given score >= 50 otherwise "fail"

let msg = "good morning"  given hour < 12
        | "good afternoon" given hour < 17
        | "good evening"
```

The `|` chains multiple conditions like a pattern ladder.
The last `|` line without `given` is the default.

---

### 0.18 `track` — Change Tracking on Variables

Mark a variable as tracked; Prism records every value it takes.

```prism
track let score = 0

score = 10
score = 25
score = 18

output score.history     # → [0, 10, 25, 18]
output score.peak        # → 25
output score.changes     # → 3
```

Useful for debugging state machines, game logic, and simulations.
`track` adds a thin wrapper; disabled in `--release`.

---

### 0.19 String Multiplication & Repetition

```prism
"ha" * 3          # → "hahaha"
"-" * 40          # → "----------------------------------------"
[0] * 10          # → [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
```

`*` on strings and arrays means repeat N times. Clean, no method call needed.

---

### 0.20 `format as` — Human Output Modes

```prism
output 1234567 as number     # → "1,234,567"
output 0.753   as percent    # → "75.3%"
output 3600    as duration   # → "1h 0m 0s"
output 1500    as filesize   # → "1.5 KB"
output "hello" as title      # → "Hello"
output names   as table      # prints array of dicts as an ASCII table
```

`as` after `output` selects a human-friendly formatter.
No imports needed — all formatters are built into the language.

---

## 1. List Modifiers — Postfix `:` Syntax

A concise postfix syntax for sorting, filtering, and transforming lists and
sequences. A colon after any list or `seq` applies a modifier.

### Sorting

```prism
[1, 2, 4, 3, 5]:        # sort ascending  → [1, 2, 3, 4, 5]
[1, 2, 4, 3, 5]:d       # sort descending → [5, 4, 3, 2, 1]
["banana", "apple", "cherry"]:   # → ["apple", "banana", "cherry"]
```

### Numeric filters

```prism
[1, 2, 3, 4, 5, 6]:e    # even numbers only  → [2, 4, 6]
[1, 2, 3, 4, 5, 6]:o    # odd numbers only   → [1, 3, 5]
[1, 2, 3, 4, 5, 6]:p    # prime numbers only → [2, 3, 5]
[-3, -1, 0, 2, 4]:+     # positives only     → [2, 4]
[-3, -1, 0, 2, 4]:-     # negatives only     → [-3, -1]
```

### Structure modifiers

```prism
[3, 1, 2, 1, 3]:u       # unique, preserve order → [3, 1, 2]
[3, 1, 2, 1, 3]:        # sort             → [1, 1, 2, 3, 3]
[3, 1, 2, 1, 3]:u:      # unique then sort → [1, 2, 3]
[1, 2, 3, 4, 5]:r       # reverse          → [5, 4, 3, 2, 1]
[1, null, 2, null, 3]:n # remove nulls     → [1, 2, 3]
[[1,2],[3,4]]:f         # flatten one level → [1, 2, 3, 4]
```

### Works with `seq` too

```prism
1..50:p                  # primes from 1 to 50  → [2, 3, 5, 7, 11, ...]
1..20:e                  # evens 1..20           → [2, 4, 6, 8, 10, 12, 14, 16, 18, 20]
1..20:o                  # odds 1..20            → [1, 3, 5, 7, 9, 11, 13, 15, 17, 19]
```

### Custom filter with a function

```prism
[1, 2, 3, 4, 5]:(fn x { x > 3 })   # → [4, 5]
[1, 2, 3, 4, 5]:(fn x { x ** 2 })  # treated as map when fn returns non-bool
```

When the function returns a `bool` it acts as a **filter**.
When it returns any other type it acts as a **map**.

### Chaining modifiers

Modifiers can be chained left to right:

```prism
[5, 3, 1, 4, 2, 1]:u:       # unique → sort  → [1, 2, 3, 4, 5]
1..100:p:r                   # primes reversed → [97, 89, 83, ...]
[4, -1, 2, -3, 0]:+:         # positives → sorted → [2, 4]
```

### Full modifier reference

| Modifier | Meaning                         |
|----------|---------------------------------|
| `:`      | Sort ascending (natural order)  |
| `:d`     | Sort descending                 |
| `:r`     | Reverse order                   |
| `:u`     | Unique — remove duplicates      |
| `:e`     | Even numbers only               |
| `:o`     | Odd numbers only                |
| `:p`     | Prime numbers only              |
| `:+`     | Positive numbers only (> 0)     |
| `:-`     | Negative numbers only (< 0)     |
| `:n`     | Remove null values              |
| `:f`     | Flatten one level of nesting    |
| `:(fn)`  | Custom filter or map function   |

### Design notes

- Modifiers are evaluated lazily when applied to a `seq`; eagerly on arrays.
- `:p`, `:e`, `:o` only operate on numeric elements; non-numeric elements are
  passed through unchanged (or can emit a warning).
- `:` alone (bare colon, no letter) always means sort ascending.
- Modifier chains apply left to right: `:u:` = unique first, then sort.

---

## 2. `seq` — Syntax-based Sequences

`seq` is the literal/expression form of range, created using the `..` operator.
It is distinct from `range()` (function-call form). `seq` produces a lazy
sequence that materializes into a list when indexed, sliced, or assigned.

### Syntax

```prism
1..10          # seq from 1 to 10 inclusive → [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
1..10..2       # with step → [1, 3, 5, 7, 9]
10..1..-1      # reverse / countdown → [10, 9, 8, 7, 6, 5, 4, 3, 2, 1]
0.0..1.0..0.1  # float sequences → [0.0, 0.1, 0.2, ..., 1.0]
'a'..'z'       # character ranges → ['a', 'b', ..., 'z']
'A'..'Z'       # uppercase character range
```

### Core behavior

- `1..10` — inclusive on both ends
- `1..10..2` — third `..` component is the step
- Step can be negative for countdown sequences
- Float step supported for float start/stop
- Character ranges supported for single-character strings

### Indexing & slicing

```prism
(1..10)[0]       # → 1
(1..10)[9]       # → 10
(1..10)[2..5]    # slice with another seq → [3, 4, 5, 6]
(1..10)[-1]      # last element → 10
```

### Methods

```prism
let s = 1..10
s.len()          # number of elements → 10
s.sum()          # sum of all elements → 55
s.min()          # smallest element → 1
s.max()          # largest element → 10
s.reverse()      # returns a new reversed seq
s.contains(5)    # membership check → true
s.to_array()     # explicit conversion to array
s.map(fn x { x * 2 })       # transform each element
s.filter(fn x { x % 2 == 0 }) # keep matching elements
s.reduce(fn acc, x { acc + x }, 0)  # fold
s.first()        # first element
s.last()         # last element
s.step()         # step value used
s.take(n)        # first n elements as a new seq
s.drop(n)        # skip first n elements
s.zip(other)     # pair with another seq/array → [(1,'a'), (2,'b'), ...]
s.enumerate()    # pair with index → [(0,1), (1,2), ...]
```

### Lazy vs eager

- `seq` is **lazy by default** — no memory allocated until iterated
- Materializes automatically when indexed, sliced, or passed to `to_array()`
- `for x in 1..1000000` never builds a million-element array

### Destructuring

```prism
let [a, b, c] = 1..3      # a=1, b=2, c=3
let [first, ...rest] = 1..5
```

### For-in iteration

```prism
for x in 1..100 {
    output x
}

for ch in 'a'..'z' {
    output ch
}
```

### Infinite sequences (future)

```prism
let naturals = 0..     # open-ended, must use .take() or for with break
```

---

## 2. String Improvements

### Multi-line strings

```prism
let text = """
    Hello,
    World!
"""
```

### Raw strings (no escape processing)

```prism
let path = r"C:\Users\name\file.txt"
let pattern = r"\d+\.\d+"
```

### String methods (missing from current stdlib)

```prism
s.starts_with("prefix")
s.ends_with("suffix")
s.strip()           # trim whitespace both ends
s.lstrip()          # trim left
s.rstrip()          # trim right
s.pad_left(n, ch)   # left-pad to width n with character ch
s.pad_right(n, ch)
s.center(n, ch)
s.count("sub")      # count occurrences of substring
s.find("sub")       # index of first occurrence, or -1
s.rfind("sub")      # index of last occurrence
s.replace("old", "new", limit)  # limit: max replacements
s.split_lines()     # split on \n / \r\n
s.is_digit()        # true if all chars are digits
s.is_alpha()        # true if all chars are letters
s.is_alnum()
s.encode("utf8")    # → bytes array
```

---

## 3. Array Improvements

### Spread operator

```prism
let a = [1, 2, 3]
let b = [0, ...a, 4]    # → [0, 1, 2, 3, 4]
```

### Array comprehensions (distinct from todo.md list)

```prism
let squares = [x ** 2 for x in 1..10]
let evens   = [x for x in 1..20 if x % 2 == 0]
```

### Missing array methods

```prism
arr.flatten()           # flatten one level of nesting
arr.flat_map(fn)        # map then flatten
arr.unique()            # deduplicate, preserving order
arr.chunk(n)            # split into sub-arrays of size n → [[1,2],[3,4],...]
arr.zip(other)          # pair with another array
arr.enumerate()         # pair with index
arr.rotate(n)           # rotate elements by n positions
arr.shuffle()           # randomize order (returns new array)
arr.sample()            # pick a random element
arr.count(x)            # count occurrences of x
arr.group_by(fn)        # dict keyed by fn(element)
arr.partition(fn)       # → (matching, not_matching)
arr.take_while(fn)      # elements until fn returns false
arr.drop_while(fn)      # skip elements until fn returns false
arr.sum()               # sum all numeric elements
arr.product()           # product of all numeric elements
arr.min_by(fn)          # element where fn(el) is smallest
arr.max_by(fn)          # element where fn(el) is largest
arr.flat()              # fully flatten nested arrays
```

---

## 4. Dict Improvements

### Dict spread / merge

```prism
let defaults = {color: "red", size: 10}
let custom   = {size: 20, weight: 5}
let merged   = {...defaults, ...custom}   # custom wins → {color:"red", size:20, weight:5}
```

### Dict comprehensions

```prism
let squared = {k: v**2 for k, v in scores}
```

### Missing dict methods

```prism
d.keys()              # array of keys
d.values()            # array of values
d.items()             # array of [key, value] pairs
d.get(key, default)   # return default if key absent
d.pop(key)            # remove and return value
d.merge(other)        # return new merged dict
d.filter(fn)          # keep entries where fn(k,v) is true
d.map_values(fn)      # transform values
d.invert()            # swap keys and values
d.has(key)            # bool membership check
```

---

## 5. `pipe` Operator (`|>`)

Pass a value through a chain of functions without intermediate variables.

```prism
let result = [1, 2, 3, 4, 5]
    |> filter(fn x { x % 2 == 0 })
    |> map(fn x { x * 10 })
    |> sum()

# → 60
```

Works with any single-argument function. The left side becomes the first
argument of the right side.

---

## 6. Pattern Matching (`match`)

Already in todo.md but with additional ideas:

```prism
match value {
    case 0           { output "zero" }
    case 1..10       { output "small" }       # seq range case
    case int if value > 100  { output "big" }
    case str         { output "a string: " + value }
    case [x, y]      { output "pair: " + x + ", " + y }  # destructure
    case {x, y}      { output "point: " + x + ", " + y }  # dict destructure
    case null        { output "nothing" }
    case _           { output "other" }
}
```

---

## 7. `when` Expression (inline conditional)

A single-expression conditional that can be used anywhere a value is expected.

```prism
let label = when x > 0 { "positive" } else { "non-positive" }
output when ready { "go" } else { "wait" }
```

Different from `if` — `when` is always an expression, always has a value.

---

## 8. Type Annotations (optional, non-enforced)

Allow annotating variables and function parameters for documentation and
future tooling (LSP, type checker). The runtime ignores them.

```prism
let x: int = 5
let name: str = "Alice"

func add(a: int, b: int) -> int {
    return a + b
}

func greet(name: str, times: int = 1) -> str {
    return name.repeat(times)
}
```

---

## 9. `defer` Statement

Run a statement at the end of the current scope, regardless of how it exits.

```prism
func read_data(path) {
    let f = open(path)
    defer f.close()        # always called when read_data returns

    let data = f.read()
    return data.parse()
}
```

Multiple `defer` statements execute in reverse (LIFO) order.

---

## 10. Immutable / Frozen Values

```prism
let config = freeze({host: "localhost", port: 8080})
config.host = "other"   # runtime error: value is frozen
```

`freeze()` makes a value deeply immutable. Useful for configuration objects
and constants that are structured values rather than primitives.

---

## 11. `format` / Template Strings Improvement

Current f-strings: `f"Hello {name}"`. Proposed additions:

```prism
# Format specifiers
f"{pi:.2f}"        # → "3.14"
f"{n:05d}"         # → "00042" for n=42
f"{x:>10}"         # right-align in 10-wide field
f"{x:<10}"         # left-align
f"{x:^10}"         # center

# Named format function
format("{name} is {age} years old", {name: "Alice", age: 30})
```

---

## 12. First-class `error` Type

Instead of just strings, errors should be first-class values.

```prism
let e = error("something went wrong", code: 404)
e.message    # "something went wrong"
e.code       # 404
e.type       # "Error"

throw error("not found", type: "NotFoundError")

try {
    risky()
} catch e if e.type == "NotFoundError" {
    output "handled"
} catch e {
    throw e   # re-throw
}
```

---

## 13. `test` Blocks (built-in unit testing)

Simple testing without an external framework.

```prism
test "addition works" {
    assert 1 + 1 == 2
    assert_eq add(3, 4), 7
}

test "string methods" {
    assert "hello".upper() == "HELLO"
    assert "  hi  ".strip() == "hi"
}
```

Run with `prism --test file.pr`. Reports pass/fail per block. No external
library required.

---

## 14. `memo` / Automatic Memoization

```prism
memo func fib(n) {
    if n <= 1 { return n }
    return fib(n-1) + fib(n-2)
}
```

`memo` prefix auto-wraps the function with a cache keyed on arguments.
First call computes, subsequent calls with the same args return cached result.

---

## 15. Numeric Improvements

```prism
# Underscores in numeric literals for readability
let million  = 1_000_000
let hex_mask = 0xFF_00_FF

# BigInt for arbitrary precision (future)
let huge = 99999999999999999999n

# Explicit integer vs float division
10 / 3    # → 3.333... (float by default)
10 // 3   # → 3 (floor divide, integer result)
10 %  3   # → 1 (modulo)
```

---

## 16. `output` Improvements

```prism
output "hello", "world"          # multiple args, space-separated → "hello world"
output "hello", "world", sep="-" # → "hello-world"
output "hello", end=""           # no trailing newline
output items, sep=", "           # join array elements
```

Making `output` consistent and flexible removes the need for `print` entirely.
