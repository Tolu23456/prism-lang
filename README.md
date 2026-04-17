# Prism

Prism is a general-purpose, dynamically-typed programming language built in C.
Source files use the `.pr` extension.

## Highlights

- Written entirely in C — no external language runtimes or third-party libraries
- Tree-walking interpreter (feature-complete) and stack-based bytecode VM
- x86-64 JIT with hot-loop detection, trace recording, and native code generation
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
- Sublime Text syntax file included

## Build

```bash
make                # debug build → ./prism
make release        # -O3 build → ./prism-release
make sanitize       # ASan + UBSan build → ./prism-san
make test           # run test suite (8 tests)
make install        # install to PREFIX (default /usr/local)
```

Requirements: `gcc`, `make`, `libm`.
X11/Xft/Fontconfig headers are optional (enables `xgui_*`).

## Run

```bash
./prism program.pr              # tree-walking interpreter
./prism --vm program.pr         # bytecode VM
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

# Error handling
try {
    throw "oops"
} catch (e) {
    output "caught: {e}"
}
```

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
RULES.txt     Language specification
CHANGELOG.md  Version history
todo.md       Roadmap and next steps
Prism.sublime-syntax   Sublime Text syntax highlighting
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
