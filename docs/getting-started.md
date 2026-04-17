# Getting Started with Prism

Prism is a general-purpose, dynamically-typed programming language implemented in C.
Source files use the `.pr` extension.

## Building

```bash
git clone https://github.com/Tolu23456/prism-lang.git
cd prism-lang
make          # debug build  → ./prism
make release  # O3 build     → ./prism-release
```

Requirements: `gcc`, `make`, `libm`.  
X11/Xft/Fontconfig are optional — the native GUI (`xgui_*`) requires them.

## Hello World

```prism
output "Hello, World!"
```

Run it:

```bash
./prism examples/hello.pr
```

## Variables

```prism
let name  = "Alice"
let age   = 30
let score = 9.5
let flag  = true
const PI  = 3.14159265358979
```

`let` declares a mutable variable. `const` declares a constant.  
All variables must be declared before use. Assignment without `let` is a compile-time error.

## Basic Types

| Type    | Example                      |
|---------|------------------------------|
| int     | `42`, `-7`, `0xFF`, `0b1010` |
| float   | `3.14`, `1e-9`               |
| bool    | `true`, `false`              |
| string  | `"hello"`, `f"hi {name}"`   |
| null    | `null`                       |
| array   | `[1, 2, 3]`                  |
| dict    | `{"key": "val"}`             |
| set     | `{1, 2, 3}`                  |
| tuple   | `(1, "two", 3.0)`            |

## String Interpolation

Double-quoted strings containing `{expr}` are automatically interpolated.  
The `f""` prefix is optional but accepted:

```prism
let name = "World"
output "Hello, {name}!"        # Hello, World!
output f"2 + 2 = {2 + 2}"    # 2 + 2 = 4
```

## Output

```prism
output "simple message"          # no parens needed
output(f"formatted: {value}")   # parens optional
print("no newline appended")     # alias without trailing newline
```

## Running Programs

```bash
./prism program.pr               # run with tree-walking interpreter
./prism --vm program.pr          # run with bytecode VM
./prism --emit-bytecode prog.pr  # compile to prog.pmc cache
./prism --format program.pr      # pretty-print formatted source
./prism --format-write prog.pr   # format in-place
./prism --version                # print version info
./prism --mem-report prog.pr     # run with memory diagnostics
./prism --bench prog.pr          # enable VM benchmark timing
```

## Next Steps

- [Language Reference](language-reference.md) — full syntax guide
- [Built-in Functions](builtins.md) — all built-in functions
- [Standard Library](standard-library.md) — `lib/*.pr` modules
