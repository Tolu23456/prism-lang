# Prism

Prism is a general-purpose, dynamically-typed programming language built in C.
Source files use the `.pr` extension.

## Highlights

- Written entirely in C — no external language runtimes or third-party libraries
- Tree-walking interpreter (feature-complete) and stack-based bytecode VM
- x86-64 / AArch64 trace JIT with hot-loop detection, trace recording, and native code generation
- Specialized integer opcodes (`OP_ADD_INT`, `OP_LT_INT`, …) skip type checks in hot paths
- AOT transpiler: emit `.c` or LLVM IR from Prism source
- Closures with correct reference-counted environments (no dangling pointers)
- Variadic functions (`...args`), spread in calls (`f(...arr)`), arrow functions (`x => expr`)
- f-strings / auto-interpolation: `"Hello, {name}!"`
- Full class system with inheritance, `super`, and `self`
- Structs for lightweight value types
- Pattern matching via `match`/`when`
- Generational GC with adaptive policy, string interning, and immortal singletons
- Built-in source formatter (`--format`, `--format-write`)
- PSS (Prism StyleSheet) theming engine — 40+ widget types, CSS variables, `var()`, `rgb()`
- X11-native GUI (`xgui_*`) and PGUI high-level toolkit
- Bytecode cache (`.pmc` files) and `--emit-bytecode`
- Memory diagnostics: `--mem-report`, allocation hotspot tracking
- Sublime Text syntax file + `.pr` file icon included

## Build

```bash
make                # optimised build (-O3) → ./prism
make lto-simple     # LTO build (~15% faster) → ./prism-lto
make pgo-train      # instrument + train
make pgo-use        # PGO-optimised build → ./prism-pgo-opt
make sanitize       # ASan + UBSan build → ./prism-san
make test           # run full test suite
make bench          # quick benchmark suite
make install        # install to PREFIX (default /usr/local)
```

Requirements: `gcc`, `make`, `libm`.
X11/Xft/Fontconfig headers are optional (enables `xgui_*`).

## Run

```bash
./prism program.pr              # tree-walking interpreter
./prism --vm program.pr         # bytecode VM (faster for numeric code)
./prism --emit-bytecode prog.pr # compile → prog.pmc
./prism prog.pmc                # run from bytecode cache
./prism --format prog.pr        # pretty-print formatted source
./prism --format-write prog.pr  # format in-place
./prism --bench --vm prog.pr    # VM with timing output
./prism --mem-report prog.pr    # memory diagnostics
./prism --version               # version, build date, X11 support
```

## Hello World

```prism
output "Hello, World!"
```

## Features at a Glance

```prism
# Variables
let name  = "Alice"
const PI  = 3.14159265358979

# String interpolation (auto)
output "Hello, {name}!"

# Arrow functions
let square = x => x * x
let double = x => { return x * 2 }

# Variadic functions
func sum(...nums) {
    return reduce(nums, fn(a, b) { return a + b }, 0)
}
output(sum(1, 2, 3, 4, 5))   # 15

# Spread
let args = [1, 2, 3]
output(sum(...args))          # 6

# Closures
func make_adder(n) { return x => x + n }
let add5 = make_adder(5)
output(add5(3))               # 8

# Classes
class Animal {
    func init(name, sound) {
        self.name  = name
        self.sound = sound
    }
    func speak() { output "{self.name} says {self.sound}!" }
}

class Dog extends Animal {
    func init(name) { super.init(name, "woof") }
}

new Dog("Rex").speak()        # Rex says woof!

# Pattern matching
func classify(n) {
    match true {
        when n < 0   { return "negative" }
        when n == 0  { return "zero" }
        when n < 100 { return "small" }
        else          { return "large" }
    }
}

# Error handling
try {
    throw "oops"
} catch (e) {
    output "caught: {e}"
}
```

## Performance

The bytecode VM uses:
- **Computed-goto dispatch** — direct-threaded, no central switch overhead
- **Tagged integers** — all small integers are unboxed `uintptr_t` values (no heap, no GC pressure)
- **Specialized opcodes** — `OP_ADD_INT`, `OP_LT_INT`, `OP_INC_LOCAL`, etc. skip type checks
- **Trace JIT** — hot loops compile to native x86-64 / AArch64 after 10 iterations
- **LTO + PGO** — link-time and profile-guided optimisation targets for maximum throughput

| Benchmark | Interpreter | VM (-O3) | Notes |
|-----------|-------------|----------|-------|
| `fib(32)` recursive | baseline | ~4× faster | JIT not applicable (recursion) |
| loop 5M iterations | baseline | ~10× faster | JIT fires, native code |
| Ackermann(3,9) | baseline | ~4× faster | Pure recursion |
| Sieve(1M) | baseline | ~5× faster | JIT loop with mod |

See [`docs/performance.md`](docs/performance.md) and [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md) for detailed analysis.

## Importing Modules

```prism
import "lib/math"          # resolves lib/math.pr automatically
import "lib/functional"    # compose, curry, partial, memoize, once...
import "lib/strings"       # capitalize, camelCase, levenshtein...
import "mymodule"          # tries: mymodule, mymodule.pr, lib/mymodule, lib/mymodule.pr
```

## Project Structure

```
src/          Lexer, parser, AST, values, interpreter, VM, compiler,
              JIT, GC, PSS, formatter, transpiler, GUI
lib/          Standard library (.pr modules)
docs/         Reference documentation (getting-started, language-reference, …)
examples/     Feature demonstrations and benchmarks
benchmarks/   Performance benchmarks and results
tests/        Test suite (test_*.pr files)
RULES.txt     Language specification
CHANGELOG.md  Version history
ideas.md      Syntax feature proposals
Prism.sublime-syntax   Sublime Text syntax highlighting
prism.svg              File type icon for .pr files
```

## Documentation

| Document | Description |
|----------|-------------|
| [Getting Started](docs/getting-started.md) | Install, build, hello world |
| [Language Reference](docs/language-reference.md) | Full syntax guide |
| [Built-in Functions](docs/builtins.md) | All built-ins |
| [Standard Library](docs/standard-library.md) | `lib/*.pr` modules |
| [Functions & Closures](docs/closures-and-functions.md) | Closures, varargs, arrow fns |
| [Classes & Structs](docs/classes-and-structs.md) | OOP system |
| [VM & Compiler](docs/vm-and-compiler.md) | Bytecode, JIT, transpiler |
| [GC & Memory](docs/gc-and-memory.md) | Garbage collector, ref-counting |
| [Performance Guide](docs/performance.md) | Optimization techniques and tuning |
