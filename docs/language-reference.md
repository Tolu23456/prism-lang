# Prism Language Reference

## Comments

```prism
# single-line comment
/* multi-line
   comment */
```

## Variables & Constants

```prism
let x = 10          # mutable variable
const MAX = 100     # immutable constant
x := 20             # walrus: declare + assign in current scope
```

## Data Types

### Numbers

```prism
let i = 42          # int
let f = 3.14        # float
let h = 0xFF        # hex literal → 255
let b = 0b1010      # binary → 10
let c = 3 + 4i      # complex number
```

### Strings

```prism
let s  = "hello"
let fs = f"value is {1 + 2}"     # f-string interpolation
let rs = r"raw \n no escape"     # raw string
let ts = """multi
line"""                           # triple-quoted
```

Auto-interpolation: `"{expr}"` works without `f` prefix.

String operations: `+` concatenation, `*` repetition.

### Booleans

```prism
let a = true
let b = false
let c = not a       # false
```

### Arrays

```prism
let arr = [1, 2, 3]
push(arr, 4)            # [1, 2, 3, 4]
let x = pop(arr)        # x = 4, arr = [1, 2, 3]
let n = len(arr)        # 3
let s = arr[0]          # 1
arr[0] = 99             # mutation
let sub = arr[1..2]     # slice via range
```

Typed arrays: `arr[int]`, `arr[float]`, `arr[str]`.

### Dictionaries

```prism
let d = {"name": "Alice", "age": 30}
let v = d["name"]       # "Alice"
d["city"] = "NYC"       # add/update
let exists = has(d, "name")   # true
let ks = keys(d)        # ["name", "age", "city"]
let vs = values(d)      # ["Alice", 30, "NYC"]
let pairs = items(d)    # [["name","Alice"], ["age",30], ...]
let empty = dict()      # empty dict
```

### Sets

```prism
let s = {1, 2, 3}
push(s, 4)
let has3 = has(s, 3)    # true
let empty = set()       # empty set
```

### Tuples

```prism
let t = (1, "two", 3.0)
let x = t[0]            # 1
```

### Null & Unknown

```prism
let x = null
let y = unknown
```

## Operators

### Arithmetic

```prism
+  -  *  /  //  %  **
```

`//` = integer division, `**` = power.

### Comparison

```prism
==  !=  <  >  <=  >=
```

### Logical

```prism
and  or  not
```

### Bitwise

```prism
&  |  ^  ~  <<  >>
```

### String

```prism
"a" + "b"   # concatenation → "ab"
```

### Null Coalescing

```prism
let x = value ?? default
```

### Ternary

```prism
let result = condition ? a : b
```

### Pipe

```prism
value |> func        # passes value as first arg to func
```

### Type Operators

```prism
x is int             # type check → bool
x is not str
```

### Membership

```prism
3 in [1, 2, 3]       # true
"hi" in {"hi", "lo"} # true
"x" not in ["a"]     # true
```

## Control Flow

### If / Elif / Else

```prism
if x > 0 {
    output "positive"
} elif x < 0 {
    output "negative"
} else {
    output "zero"
}
```

### While

```prism
let i = 0
while i < 10 {
    output i
    i += 1
}
```

### For-In (iteration)

```prism
for item in [1, 2, 3] { output item }

for i in range(0, 10) { output i }

for i, v in enumerate([10, 20, 30]) {
    output f"{i}: {v}"
}

for k, v in items(dict) { output f"{k} = {v}" }
```

### Repeat

```prism
repeat 5 {
    output "five times"
}
```

### Break / Continue

```prism
while true {
    if done { break }
    if skip { continue }
}
```

### Match / When

```prism
match x {
    when 1  { output "one" }
    when 2  { output "two" }
    else    { output "other" }
}
```

## Functions

```prism
func add(a, b) {
    return a + b
}

func greet(name = "World") {
    output "Hello, {name}!"
}

func sum_all(...nums) {
    let total = 0
    for n in nums { total += n }
    return total
}
```

### Anonymous Functions

```prism
let double = fn(x) { return x * 2 }
let square = x => x * x           # arrow (single param)
let add    = fn(a, b) { return a + b }
```

### Arrow Functions (block body)

```prism
let process = x => {
    let result = x * 2
    return result + 1
}
```

### Closures

```prism
func make_adder(n) {
    return x => x + n
}
let add5 = make_adder(5)
output(add5(3))    # 8
```

### Spread in Calls

```prism
let args = [1, 2, 3]
func sum3(a, b, c) { return a + b + c }
output(sum3(...args))   # 6
```

## Classes

```prism
class Animal {
    func init(name, sound) {
        self.name  = name
        self.sound = sound
    }
    func speak() {
        output "{self.name} says {self.sound}"
    }
}

class Dog extends Animal {
    func init(name) {
        super.init(name, "woof")
    }
    func fetch() {
        output "{self.name} fetches!"
    }
}

let d = new Dog("Rex")
d.speak()
d.fetch()
```

## Structs

```prism
struct Point { x, y }

let p = new Point(3.0, 4.0)
output(p.x)    # 3
```

## Error Handling

```prism
try {
    let x = int("not a number")
} catch (e) {
    output "error: {e}"
}

throw "something went wrong"
```

## Imports

```prism
import "lib/math"          # resolves to lib/math.pr
import "lib/functional"
import "mymodule"          # tries mymodule, mymodule.pr, lib/mymodule, lib/mymodule.pr
```

## Range Literals

```prism
let r = 1..10           # range 1 to 10 (inclusive)
let r2 = 0..100 step 2  # with step

for i in 1..5 { output i }
```

## Type Checks

```prism
if x is int   { output "integer" }
if x is str   { output "string"  }
if x is array { output "array"   }
type(x)                 # → "int", "string", "array", ...
```

## String Methods (via global functions)

```prism
upper("hello")      # "HELLO"
lower("WORLD")      # "world"
trim("  hi  ")      # "hi"
split("a,b", ",")   # ["a", "b"]
join(", ", arr)     # "a, b"
chars("abc")        # ["a", "b", "c"]
ord("A")            # 65
chr(65)             # "A"
slice(s, 0, 5)      # substring
starts(s, "he")     # bool
ends(s, "lo")       # bool
contains(s, "ell")  # bool
replace(s, "a", "b")
```
