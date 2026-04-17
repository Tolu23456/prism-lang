# Prism Standard Library

All library modules live in `lib/`. Import them with:

```prism
import "lib/module_name"
```

---

## `lib/functional.pr` — Functional Programming

Higher-order utilities inspired by Haskell, Scala, and Elixir.

```prism
import "lib/functional"

# compose: right-to-left function composition
let transform = compose(x => x * 2, x => x + 10)
output(transform(5))   # (5 + 10) * 2 = 30

# pipe: left-to-right
let process = pipe(x => x + 10, x => x * 2)
output(process(5))     # (5 + 10) * 2 = 30

# curry: convert f(a,b) → f(a)(b)
let curried_add = curry(fn(a, b) { return a + b })
let add5 = curried_add(5)
output(add5(3))        # 8

# partial application
let double = partial(fn(factor, x) { return factor * x }, 2)
output(double(7))      # 14

# memoize: cache results
let fib = memoize(fn(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
})

# once: call-once wrapper
let init = once(fn() { output "initialized" })

# Other: identity, constant, negate, flip, apply, tap, juxt,
#         zipWith, scanLeft, groupByKey, countWhere,
#         takeWhile, dropWhile, partition, flatMap, iterate
```

---

## `lib/collections.pr` — Data Structures

```prism
import "lib/collections"

# Stack (LIFO)
let s = Stack()
s.push(1); s.push(2)
output(s.pop())     # 2

# Queue (FIFO)
let q = Queue()
q.enqueue(1); q.enqueue(2)
output(q.dequeue()) # 1

# PriorityQueue
let pq = PriorityQueue()
pq.add(30, "low"); pq.add(10, "high")
output(pq.poll())   # "high"

# LRUCache
let lru = LRUCache(3)
lru.put("a", 1); lru.put("b", 2)
output(lru.get("a"))

# OrderedMap, MultiMap, BiMap, Trie, Graph also available
```

---

## `lib/strings.pr` — String Utilities

```prism
import "lib/strings"

capitalize("hello")         # "Hello"
titleCase("hello world")    # "Hello World"
camelCase("hello world")    # "helloWorld"
snakeCase("helloWorld")     # "hello_world"
kebabCase("hello world")    # "hello-world"
swapCase("Hello")           # "hELLO"

padLeft("42", 6, "0")       # "000042"
padRight("hi", 5)           # "hi   "
padCenter("hi", 7)          # "  hi   "

isBlank("  ")               # true
isEmpty("")                 # true
isNumeric("3.14")           # true
isAlpha("abc")              # true
isPalindrome("racecar")     # true

reverse("hello")            # "olleh"
truncate("long text", 8)    # "long ..."
stripLeft("  hi", " ")      # "hi"
slugify("Hello World!")     # "hello-world"
levenshtein("kitten","sitting") # 3
wrapWords("long text", 20)  # word-wrapped string
```

---

## `lib/math.pr` — Extended Math

```prism
import "lib/math"

# Constants
output(math.PI)       # 3.14159...
output(math.E)        # 2.71828...
output(math.INF)      # infinity
output(math.NAN)      # NaN

# Number theory
gcd(12, 8)            # 4
lcm(12, 8)            # 24
isPrime(17)           # true
primes(20)            # [2, 3, 5, 7, 11, 13, 17, 19]
fibonacci(10)         # [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]
factorial(5)          # 120

# Statistics
mean([1,2,3,4,5])     # 3
median([1,2,3,4,5])   # 3
stddev([1,2,3,4,5])   # ~1.414
variance([2,4,6])     # 2.67

# Numeric
clamp(x, lo, hi)
lerp(a, b, t)
map_range(x, in_lo, in_hi, out_lo, out_hi)
```

---

## `lib/iter.pr` — Iterators & Generators

```prism
import "lib/iter"

# map, filter, reduce
map([1,2,3], x => x * 2)          # [2, 4, 6]
filter([1,2,3,4], x => x % 2 == 0) # [2, 4]
reduce([1,2,3], fn(a,b){return a+b}, 0)  # 6

# take / drop / chunk
take([1,2,3,4,5], 3)    # [1, 2, 3]
drop([1,2,3,4,5], 2)    # [3, 4, 5]
chunk([1..10], 3)        # [[1,2,3],[4,5,6],[7,8,9],[10]]

# flatten, unique, zip, product
unique([1,1,2,2,3])     # [1, 2, 3]
zip([1,2],[3,4])         # [[1,3],[2,4]]
```

---

## `lib/json.pr` — JSON Serializer

```prism
import "lib/json"

let obj = {"name": "Alice", "scores": [98, 87, 92]}
let text = json.stringify(obj)           # serialize
let parsed = json.parse(text)            # deserialize
```

---

## `lib/random.pr` — Random Numbers

```prism
import "lib/random"

random_int(1, 100)       # integer in [1, 100]
random_float()           # float in [0, 1)
random_choice([1,2,3])   # random element
shuffle([1,2,3,4,5])     # shuffled copy
```

---

## `lib/datetime.pr` — Date & Time

```prism
import "lib/datetime"

let now = datetime.now()
output(now.year)
output(now.month)
output(now.day)
output(now.format("%Y-%m-%d"))
```

---

## `lib/io.pr` — File I/O

```prism
import "lib/io"

let content = io.read("file.txt")
io.write("out.txt", content)
io.append("log.txt", "new line\n")
let exists = io.exists("file.txt")
let lines = io.readlines("file.txt")
```

---

## `lib/csv.pr` — CSV Parsing

```prism
import "lib/csv"

let table = csv.parse("a,b,c\n1,2,3\n4,5,6")
let text  = csv.stringify(table)
```

---

## `lib/testing.pr` — Unit Testing

```prism
import "lib/testing"

let t = TestSuite("my tests")
t.test("addition", fn() {
    assert_eq(1 + 1, 2)
})
t.test("strings", fn() {
    assert(starts("hello", "he"))
})
t.run()
```

---

## `lib/vector.pr` — 2D/3D Vectors

```prism
import "lib/vector"

let v1 = Vec2(3, 4)
let v2 = Vec2(1, 0)
output(v1.length())        # 5
output(v1.dot(v2))         # 3
output(v1.add(v2).x)       # 4

let v3 = Vec3(1, 0, 0)
let v4 = Vec3(0, 1, 0)
output(v3.cross(v4).z)     # 1
```

---

## `lib/matrix.pr` — Matrix Operations

```prism
import "lib/matrix"

let m = Matrix([[1,2],[3,4]])
let n = Matrix([[5,6],[7,8]])
output(m.add(n))
output(m.mul(n))
output(m.transpose())
output(m.det())
```

---

## `lib/crypto.pr` — Hashing Utilities

```prism
import "lib/crypto"

crypto.fnv1a("hello")     # 32-bit FNV-1a hash string
crypto.djb2("hello")      # djb2 hash
crypto.crc32("hello")     # CRC-32
```

---

## `lib/event.pr` — Event Emitter

```prism
import "lib/event"

let emitter = EventEmitter()
emitter.on("click", fn(data) { output "clicked: {data}" })
emitter.emit("click", "button1")
```

---

## `lib/perf.pr` — Performance Utilities

```prism
import "lib/perf"

let timer = perf.timer()
timer.start()
# ... code to measure ...
output(f"elapsed: {timer.elapsed()}ms")
```

---

## `lib/async.pr` — Async Patterns

```prism
import "lib/async"

# Promise-style (synchronous simulation)
let p = Promise(fn(resolve, reject) {
    resolve(42)
})
p.then(fn(v) { output v })
```

---

## `lib/html.pr` — HTML Generation

```prism
import "lib/html"

let doc = html.tag("html", [
    html.tag("body", [
        html.tag("h1", [], "Hello Prism"),
        html.tag("p",  [], "A paragraph")
    ])
])
output(html.render(doc))
```

---

## `lib/gui.pr` — High-level GUI

```prism
import "lib/gui"

gui_window("My App", 800, 600)
gui_label("Enter name:")
gui_input("name")
gui_button("Submit", fn() { output "submitted" })
gui_run()
```

---

## `lib/secrets.pr` — Secret Storage

```prism
import "lib/secrets"

secrets.set("api_key", "abc123")
let key = secrets.get("api_key")
```

---

## `lib/universe.pr` — Meta-Programming

```prism
import "lib/universe"

# Reflection, introspection, and meta-programming utilities
```
