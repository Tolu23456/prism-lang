# Prism Language Guide

Prism is a dynamic, interpreted programming language with a clean syntax and
first-class functions. It compiles to bytecode and executes on a register-based VM.

---

## Table of Contents
1. [Hello, World!](#hello-world)
2. [Variables and Constants](#variables-and-constants)
3. [Types](#types)
4. [Operators](#operators)
5. [Control Flow](#control-flow)
6. [Functions](#functions)
7. [Collections](#collections)
8. [Strings](#strings)
9. [Standard Library](#standard-library)
10. [PSS Stylesheets](#pss-stylesheets)
11. [XGUI (Native GUI)](#xgui-native-gui)

---

## Hello, World!

```prism
output("Hello, World!")
```

Or use the `print` shorthand:

```prism
print("Hello, Prism!")
```

---

## Variables and Constants

```prism
let name = "Alice"     # mutable variable
const PI = 3.14159     # immutable constant (cannot be reassigned)

name = "Bob"           # OK — let is mutable
# PI = 3.0            # Error — const cannot be reassigned
```

---

## Types

Prism is dynamically typed. Core types are:

| Type      | Example                     | Notes                         |
|-----------|-----------------------------|-------------------------------|
| `int`     | `42`, `-7`, `0xFF`, `0b101` | 64-bit integer                |
| `float`   | `3.14`, `-0.5`, `1e10`      | 64-bit IEEE 754 double        |
| `bool`    | `true`, `false`, `unknown`  | Three-valued: Kleene logic     |
| `str`     | `"hello"`, `f"hi {name}"`   | UTF-8 string                  |
| `null`    | `null`                      | Absence of value               |
| `array`   | `[1, 2, 3]`                 | Ordered, mutable               |
| `dict`    | `{"key": "val"}`            | Hash map                       |
| `set`     | `{1, 2, 3}`                 | Unordered unique values        |
| `tuple`   | `(1, 2, 3)`                 | Immutable ordered collection   |
| `func`    | `func(x) { return x+1 }`   | First-class function           |
| `complex` | `complex(1, 2.5)` → `1+2.5j` | Complex number               |

### Type checking

```prism
let x = 42
if x is int   { print("integer") }
if x is float { print("float") }
```

---

## Operators

### Arithmetic
```prism
10 + 3    # 13
10 - 3    # 7
10 * 3    # 30
10 / 3    # 3.333...  (float division)
10 // 3   # 3         (floor division — integer result)
10 % 3    # 1         (modulo)
2 ** 8    # 256.0     (exponentiation)
```

### Comparison
```prism
x == y    # equal
x != y    # not equal
x < y     # less than
x <= y    # less or equal
x > y     # greater than
x >= y    # greater or equal
```

### Chain Comparisons (Python-style)
```prism
1 < x < 10          # equivalent to (1 < x) && (x < 10)
0 <= score <= 100   # valid range check
a < b <= c < d      # arbitrary chain length
```

### Logical
```prism
x && y    # short-circuit AND
x || y    # short-circuit OR
!x        # logical NOT
```

### Bitwise
```prism
a & b    # AND
a | b    # OR
a ^ b    # XOR
~a       # NOT (complement)
a << n   # left shift
a >> n   # right shift
```

### Null Coalescing
```prism
let val = maybe_null ?? "default"   # returns "default" if maybe_null is null
```

### Pipe Operator
```prism
let result = [1,2,3] |> len        # pipes left value as first argument
```

---

## Control Flow

### If / elif / else
```prism
if score >= 90      { grade = "A" }
elif score >= 80    { grade = "B" }
elif score >= 70    { grade = "C" }
else                { grade = "F" }
```

### While loop
```prism
let i = 0
while i < 10 {
    print(i)
    i += 1
}
```

### For-in loop
```prism
for fruit in ["apple", "banana", "cherry"] {
    print(fruit)
}

for ch in "hello" {    # iterates over characters
    print(ch)
}

for key in myDict {    # iterates over dict keys
    print(key, "->", myDict[key])
}
```

### Break and Continue
```prism
let i = 0
while i < 10 {
    if i == 5 { break }
    if i % 2 == 0 { i += 1; continue }
    print(i)
    i += 1
}
```

---

## Functions

### Named function
```prism
func greet(name) {
    return f"Hello, {name}!"
}
print(greet("World"))
```

### Default parameters
```prism
func greet(name, greeting) {
    if greeting == null { greeting = "Hello" }
    return f"{greeting}, {name}!"
}
```

### Anonymous function (lambda)
```prism
let square = func(x) { return x * x }
let doubled = func(x) { return x * 2 }
print(square(5))   # 25
```

### Closures
```prism
func make_counter(start) {
    let count = start
    func increment() {
        count += 1
        return count
    }
    return increment
}
let counter = make_counter(0)
print(counter())   # 1
print(counter())   # 2
```

### Recursion
```prism
func fibonacci(n) {
    if n < 2 { return n }
    return fibonacci(n-1) + fibonacci(n-2)
}
```

---

## Collections

### Arrays
```prism
let arr = [1, 2, 3, 4, 5]
arr.add(6)              # append
arr.pop()               # remove last → returns 6
arr.insert(0, 0)        # insert 0 at index 0
arr.remove(3)           # remove first occurrence of 3
arr.sort()              # sort in-place
let sub = arr[1:3]      # slice → [2, 3]
let rev = arr[::-1]     # reverse slice
```

### Dicts
```prism
let person = {
    "name": "Alice",
    "age": 30,
    "city": "Paris"
}
person["email"] = "alice@example.com"
person.keys()    # → ["name", "age", "city", "email"]
person.values()
person.items()   # → [("name","Alice"), ...]
person.get("phone", "N/A")  # safe get with default
```

### Sets
```prism
let s1 = {1, 2, 3, 4}
let s2 = {3, 4, 5, 6}
let union = s1 | s2        # {1,2,3,4,5,6}
let inter = s1 & s2        # {3,4}
let diff  = s1 ^ s2        # symmetric difference
s1.add(7)
s1.discard(1)
```

### Tuples
```prism
let point = (10, 20)
let x = point[0]   # 10
let y = point[1]   # 20
```

---

## Strings

### Interpolation (f-strings)
```prism
let name = "World"
let n    = 42
print(f"Hello, {name}! Answer: {n}")
```

### Methods
```prism
"hello".upper()           # "HELLO"
"WORLD".lower()           # "world"
"  spaces  ".strip()      # "spaces"
"hello world".split(" ")  # ["hello", "world"]
",".join(["a","b","c"])   # "a,b,c"
"hello".startswith("hel") # true
"hello".endswith("llo")   # true
"hello".replace("l","r")  # "herro"
"hello".find("ll")        # 2
```

### Built-in string functions
```prism
upper("hello")            # "HELLO"
lower("WORLD")            # "world"
trim("  hi  ")            # "hi"
split("a,b,c", ",")       # ["a","b","c"]
join(",", ["a","b","c"])  # "a,b,c"
contains("hello", "ell")  # true
startsWith("hello", "hel") # true
endsWith("hello", "llo")   # true
indexOf("hello", "l")      # 2
replace("hello", "l", "r") # "herro"
len("hello")               # 5
```

---

## Standard Library

Import with `import "lib/<module>"`:

```prism
import "lib/math"
import "lib/strings"
import "lib/collections"
import "lib/random"
import "lib/datetime"
import "lib/json"
import "lib/fs"
import "lib/crypto"
```

### Math functions (always available)
```prism
abs(-5)          # 5
sqrt(16)         # 4.0
floor(3.9)       # 3
ceil(3.1)        # 4
round(3.567, 2)  # 3.57
min(1, 2, 3)     # 1
max(1, 2, 3)     # 3
pow(2, 10)       # 1024.0
sin(PI / 2)      # 1.0
log(E)           # 1.0
```

---

## PSS Stylesheets

Prism Style Sheets (PSS) are CSS-like files for styling XGUI windows.
Link them at the top of your program:

```prism
link style.pss
link theme.pss, design.pss   # multiple files
```

**Example `style.pss`:**
```pss
window {
    background: #1a1a2e;
    font: "Helvetica";
    font_size: 14;
    padding: 16;
}

button {
    background: #4f8ef7;
    foreground: #ffffff;
    radius: 6;
    padding: 8;
}
```

---

## XGUI (Native GUI)

XGUI provides a native X11 windowed GUI:

```prism
xgui_init(800, 600, "My App")

while xgui_running() {
    xgui_begin()
    xgui_title("Dashboard")
    xgui_label("Hello from Prism!")
    if xgui_button("Click Me") {
        print("Button clicked!")
    }
    let val = xgui_slider("speed", 0.0, 100.0, 50.0)
    xgui_progress(int(val), 100)
    xgui_end()
}
xgui_close()
```

---

*See also: [stdlib_reference.md](stdlib_reference.md), [vm_internals.md](vm_internals.md)*
