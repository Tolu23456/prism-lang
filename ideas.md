# Prism — Syntax Feature Ideas

---

## 1. Destructuring Assignment

Unpack arrays, dicts, and tuples directly into variables.

```prism
let [a, b, c] = [1, 2, 3]

let [first, ...rest] = [10, 20, 30, 40]
# first = 10, rest = [20, 30, 40]

let {name, age} = {name: "Alice", age: 30}

let [x, _, z] = point   # _ discards middle value
```

Works in `for` loops too:

```prism
for [key, val] in scores.items() {
    output f"{key}: {val}"
}
```

---

## 2. Optional Chaining (`?.`)

Safely access a property or call a method on a value that might be null.
Returns null instead of erroring.

```prism
let city = user?.address?.city        # null if user or address is null
let len  = response?.body?.len()      # null if response or body is null
let first = arr?.get(0)               # null if arr is null
```

Combine with `??` for a safe default:

```prism
let name = user?.profile?.display_name ?? "Anonymous"
```

---

## 3. `unless` Statement

The inverse of `if` — runs the block when the condition is **false**.
Reads more naturally than `if not ...`.

```prism
unless user.is_logged_in {
    redirect("/login")
}

unless arr.len() > 0 {
    return null
}
```

Also works as a postfix modifier on a single statement:

```prism
return null unless ready
output "ok" unless silent
```

---

## 4. `until` Loop

Runs while the condition is false — the inverse of `while`.

```prism
until queue.is_empty() {
    process(queue.pop())
}

until score >= 100 {
    score += roll()
}
```

---

## 5. String Slice Syntax

Index strings with ranges, just like arrays.

```prism
let s = "hello world"
s[0..4]       # → "hello"
s[6..]        # → "world"
s[-5..]       # → "world"
s[..5]        # → "hello"
s[::2]        # → "hlowrd"  (every other character)
```

---

## 6. `match` Expression (inline form)

Already a statement — also allow as an expression that yields a value.

```prism
let label = match score {
    when 90..100 { "A" }
    when 80..89  { "B" }
    when 70..79  { "C" }
    else         { "F" }
}

output match day {
    when "Sat", "Sun" { "weekend" }
    else              { "weekday" }
}
```

---

## 7. Default Parameter Values

Functions can declare fallback values for missing arguments.

```prism
func greet(name, greeting = "Hello") {
    output f"{greeting}, {name}!"
}

greet("Alice")            # → Hello, Alice!
greet("Bob", "Hi")        # → Hi, Bob!

func connect(host, port = 8080, tls = false) {
    ...
}
connect("localhost")
connect("example.com", 443, true)
```

---

## 8. Named Arguments

Call a function by naming its parameters, in any order.

```prism
func make_rect(width, height, color = "black") { ... }

make_rect(width: 100, height: 50)
make_rect(color: "red", height: 200, width: 150)
make_rect(100, 50, color: "blue")
```

---

## 9. `for` with Index

Iterate with both element and index without calling `enumerate()` manually.

```prism
for i, val in items {
    output f"{i}: {val}"
}

for i, ch in "hello" {
    output f"[{i}] = {ch}"
}
```

---

## 10. Spread Operator in Function Calls

Pass an array as positional arguments using `...`.

```prism
let args = [1, 2, 3]
add(...args)            # same as add(1, 2, 3)

let parts = ["Alice", 30]
greet(...parts)

output max(...values)
```

Also works when building arrays:

```prism
let combined = [...list_a, ...list_b]
let with_zero = [0, ...existing]
```

---

## 11. `const` Keyword

Declare a variable that cannot be reassigned after creation.

```prism
const PI = 3.14159265
const MAX_SIZE = 1024
const APP_NAME = "Prism"

PI = 3   # error: cannot reassign const
```

Const works at any scope — top-level, inside functions, inside loops.

---

## 12. Walrus / Assign-and-Test (`:=`)

Assign a value and test it in the same expression — useful in `while` and `if`.

```prism
while line := file.read_line() {
    process(line)
}

if m := text.match(pattern) {
    output m.group(0)
}
```

---

## 13. Multi-line String Interpolation

F-strings that can span multiple lines with full expressions inside.

```prism
let msg = f"""
    Hello, {user.name}!
    You have {inbox.count()} unread messages.
    Last login: {user.last_login.format("YYYY-MM-DD")}
"""
```

Leading whitespace is stripped to the level of the closing `"""`.

---

## 14. `with` Statement (Resource Management)

Open a resource, use it, and guarantee cleanup — even if an error occurs.

```prism
with open("data.txt") as f {
    let content = f.read()
}
# f.close() called automatically

with db.transaction() as tx {
    tx.insert(row)
    tx.insert(row2)
}
# tx.commit() or tx.rollback() on error
```

---

## 15. Operator Overloading

Let classes define behavior for built-in operators.

```prism
class Vec2 {
    let x, y

    func new(x, y) { self.x = x; self.y = y }

    op + (other) { return Vec2(self.x + other.x, self.y + other.y) }
    op - (other) { return Vec2(self.x - other.x, self.y - other.y) }
    op * (s)     { return Vec2(self.x * s, self.y * s) }
    op == (other) { return self.x == other.x and self.y == other.y }
    op str ()    { return f"({self.x}, {self.y})" }
}

let a = Vec2(1, 2)
let b = Vec2(3, 4)
output a + b    # → (4, 6)
output a * 3    # → (3, 6)
output a == b   # → false
```

---

## 16. `enum` Type

Define a fixed set of named values.

```prism
enum Direction { North, South, East, West }
enum Color     { Red, Green, Blue }

let dir = Direction.North

match dir {
    when Direction.North { output "going up" }
    when Direction.South { output "going down" }
    else                 { output "going sideways" }
}

output dir is Direction   # → true
```

Enums with values:

```prism
enum Status {
    OK    = 200
    NotFound = 404
    Error = 500
}

output Status.OK.value    # → 200
```

---

## 17. `interface` / Protocol Declarations

Describe the methods a value must have, without enforcing inheritance.

```prism
interface Drawable {
    func draw()
    func resize(w, h)
}

interface Serializable {
    func to_json() -> str
    func from_json(s) -> self
}

class Circle implements Drawable {
    func draw()      { ... }
    func resize(w,h) { ... }
}

func render(shape: Drawable) {
    shape.draw()
}
```

Duck-typed at runtime: any object with the required methods satisfies the interface.

---

## 18. Generator Functions (`yield`)

Functions that produce a sequence of values lazily, one at a time.

```prism
func count_up(start, stop) {
    let i = start
    while i <= stop {
        yield i
        i += 1
    }
}

for n in count_up(1, 10) {
    output n
}

func fibonacci() {
    let a, b = 0, 1
    loop {
        yield a
        a, b = b, a + b
    }
}

let fibs = fibonacci().take(10)   # [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]
```

---

## 19. `async` / `await`

Write asynchronous code that looks synchronous.

```prism
async func fetch_data(url) {
    let res = await http.get(url)
    return res.json()
}

async func main() {
    let data = await fetch_data("https://api.example.com/users")
    for user in data {
        output user.name
    }
}

run(main())
```

Multiple concurrent requests:

```prism
let [a, b, c] = await all([
    fetch_data(url1),
    fetch_data(url2),
    fetch_data(url3),
])
```

---

## 20. `class` Improvements

### 20a. Property Getters and Setters

```prism
class Temperature {
    let _celsius

    prop celsius {
        get { return self._celsius }
        set(v) { self._celsius = v }
    }

    prop fahrenheit {
        get { return self._celsius * 9/5 + 32 }
        set(v) { self._celsius = (v - 32) * 5/9 }
    }
}

let t = Temperature()
t.celsius = 100
output t.fahrenheit   # → 212.0
t.fahrenheit = 32
output t.celsius      # → 0.0
```

### 20b. Static Methods and Fields

```prism
class Counter {
    static let count = 0

    func new() {
        Counter.count += 1
        self.id = Counter.count
    }

    static func reset() {
        Counter.count = 0
    }
}

let a = Counter()
let b = Counter()
output Counter.count    # → 2
Counter.reset()
output Counter.count    # → 0
```

### 20c. `super` Keyword

```prism
class Animal {
    func new(name) { self.name = name }
    func speak()   { output f"{self.name} makes a sound" }
}

class Dog extends Animal {
    func new(name, breed) {
        super.new(name)
        self.breed = breed
    }
    func speak() {
        super.speak()
        output f"{self.name} also barks"
    }
}
```

---

## 21. `import` Improvements

### 21a. Import without quotes

```prism
import math
import fs
import os
```

### 21b. Import with alias

```prism
import math as m
import collections as col

m.sqrt(16)
col.deque()
```

### 21c. Selective import

```prism
import {sqrt, pow, pi} from math
import {read, write}   from fs

sqrt(9)   # no prefix needed
```

---

## 22. Type Checking with `is`

Runtime type checks with readable syntax.

```prism
output 42 is int         # → true
output "hi" is str       # → true
output [1,2] is array    # → true
output null is null      # → true

func process(val) {
    if val is int   { output "integer: " + str(val) }
    if val is str   { output "string: "  + val }
    if val is array { output "array of " + str(val.len()) }
}
```

Also `is not`:

```prism
if x is not null {
    use(x)
}
```

---

## 23. Chained String Methods (Fluent API)

Allow method calls to chain across lines for readability.

```prism
let result = raw_input
    .strip()
    .lower()
    .replace(",", "")
    .split(" ")
    .filter(fn w { w.len() > 3 })
    .join(", ")
```

---

## 24. `loop` — Infinite Loop

A cleaner alternative to `while true { }`.

```prism
loop {
    let input = read_line()
    if input == "quit" { break }
    process(input)
}
```

---

## 25. Array/Dict Shorthand Literals

### Short property names (when variable name matches key)

```prism
let name = "Alice"
let age  = 30

let user = {name, age}    # same as {name: name, age: age}
```

### Computed property keys

```prism
let key = "color"
let style = {[key]: "red"}   # → {color: "red"}
```
