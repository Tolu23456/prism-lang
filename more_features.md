# Prism — More Features

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
