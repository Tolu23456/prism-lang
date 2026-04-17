# Functions & Closures in Prism

## Function Declaration

```prism
func add(a, b) {
    return a + b
}
```

Named function declarations hoist to the current scope before any code runs.

## Default Parameters

```prism
func greet(name = "World", punctuation = "!") {
    output "Hello, {name}{punctuation}"
}
greet()              # Hello, World!
greet("Alice")       # Hello, Alice!
greet("Bob", ".")    # Hello, Bob.
```

## Variadic Parameters (`...`)

```prism
func sum(...nums) {
    let total = 0
    for n in nums { total += n }
    return total
}
output(sum(1, 2, 3, 4, 5))   # 15
```

The variadic parameter must be the last parameter:

```prism
func log(level, ...messages) {
    for msg in messages {
        output "[{level}] {msg}"
    }
}
log("INFO", "started", "running", "done")
```

## Anonymous Functions

```prism
let double = fn(x) { return x * 2 }
let square = x => x * x          # arrow shorthand (single param)
```

### Arrow Functions

Single-expression body (implicit return):
```prism
let triple   = x => x * 3
let identity = x => x
let always42 = x => 42
```

Block body (explicit return):
```prism
let process = x => {
    let step1 = x * 2
    let step2 = step1 + 1
    return step2
}
```

Multi-param `fn` with arrow body:
```prism
let add = fn(a, b) => a + b
```

## Closures

Functions capture their enclosing scope and keep it alive:

```prism
func make_counter(start = 0) {
    let count = start
    return {
        "inc":   fn()    { count += 1; return count },
        "dec":   fn()    { count -= 1; return count },
        "reset": fn()    { count = start; return count },
        "get":   fn()    { return count }
    }
}

let c = make_counter(10)
output(c["inc"]())    # 11
output(c["inc"]())    # 12
output(c["dec"]())    # 11
output(c["reset"]())  # 10
```

Closures over mutable state:

```prism
func make_adder(n) {
    return x => x + n
}
let add5  = make_adder(5)
let add10 = make_adder(10)
output(add5(3))     # 8
output(add10(3))    # 13
```

## Higher-Order Functions

```prism
func map(lst, f) {
    let result = []
    for item in lst { push(result, f(item)) }
    return result
}

func filter(lst, pred) {
    let result = []
    for item in lst { if pred(item) { push(result, item) } }
    return result
}

func reduce(lst, f, init) {
    let acc = init
    for item in lst { acc = f(acc, item) }
    return acc
}

let nums   = [1, 2, 3, 4, 5]
let doubled = map(nums, x => x * 2)             # [2, 4, 6, 8, 10]
let evens   = filter(nums, x => x % 2 == 0)    # [2, 4]
let total   = reduce(nums, fn(a,b){return a+b}, 0) # 15
```

## Spread in Calls

Pass an array as individual arguments with `...`:

```prism
func sum3(a, b, c) { return a + b + c }
let args = [1, 2, 3]
output(sum3(...args))   # 6
```

Combine regular and spread args:

```prism
output(sum3(1, ...[2, 3]))   # 6
```

## Recursion

```prism
func factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
output(factorial(10))   # 3628800

func fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

## Memoization Pattern

```prism
func memoize(f) {
    let cache = {"_stub": null}
    return fn(...args) {
        let key = str(args)
        if has(cache, key) { return cache[key] }
        let result = f(...args)
        cache[key] = result
        return result
    }
}

let memo_fib = memoize(fn(n) {
    if n <= 1 { return n }
    return memo_fib(n - 1) + memo_fib(n - 2)
})
output(memo_fib(35))   # fast due to caching
```

## Compose & Pipe

```prism
func compose(...fns) {
    return fn(x) {
        let result = x
        let i = len(fns) - 1
        while i >= 0 {
            result = fns[i](result)
            i -= 1
        }
        return result
    }
}

func pipe(...fns) {
    return fn(x) {
        let result = x
        for f in fns { result = f(result) }
        return result
    }
}

let transform = compose(x => x * 2, x => x + 3)
output(transform(5))   # (5 + 3) * 2 = 16

let process = pipe(x => x + 3, x => x * 2)
output(process(5))     # (5 + 3) * 2 = 16
```

## Partial Application & Currying

```prism
func partial(f, ...bound) {
    return fn(...rest) {
        let args = []
        for b in bound { push(args, b) }
        for r in rest  { push(args, r) }
        return f(...args)
    }
}

func curry(f) {
    return fn(a) { return fn(b) { return f(a, b) } }
}

let mul = fn(a, b) { return a * b }
let double  = partial(mul, 2)
let triple  = partial(mul, 3)
let curried = curry(mul)
let times5  = curried(5)

output(double(7))      # 14
output(triple(7))      # 21
output(times5(6))      # 30
```
