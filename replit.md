# Prism Programming Language

## Overview
Prism is a dynamically-typed, general-purpose programming language implemented in C11. It features multiple execution modes and modern language features.

## Tech Stack
- **Core**: C11 (gcc)
- **Build System**: GNU Make
- **Dependencies** (via Nix): gcc, gnumake, X11, Xft, Xrender, fontconfig, freetype, pkg-config

## Project Structure
- `src/` - Core C implementation: lexer, parser, AST, interpreter, VM, JIT, GC, transpiler, GUI
- `lib/` - Standard library written in Prism (`.pr` files): async, collections, crypto, fs, json, math, etc.
- `docs/` - Language and internals documentation
- `examples/` - Sample Prism programs (hello.pr, game_of_life.pr, gui_demo.pr, etc.)
- `tests/` - Test suite and `run_tests.sh`
- `benchmarks/` - Performance benchmarks

## Execution Modes
- **Tree-walking interpreter** (default): `./prism program.pr`
- **Bytecode VM**: `./prism --vm program.pr`
- **JIT compiler** (x86-64 hot loops): `./prism --jit program.pr`
- **AOT transpiler to C**: `./prism --transpile program.pr`
- **REPL**: `./prism` (no arguments)
- **Code formatter**: `./prism --format program.pr`
- **Memory diagnostics**: `./prism --mem-report program.pr`

## Build Commands
- `make` - Debug build → `./prism`
- `make release` - Optimized build → `./prism-release`
- `make sanitize` - AddressSanitizer/UBSan build → `./prism-san`
- `make test` - Run test suite
- `make clean` - Remove build artifacts
- `make install` - Install to `/usr/local/bin`

## Workflow
The **"Start application"** workflow runs `make && ./prism examples/hello.pr`, which compiles the project and runs the hello world example to verify the build.

## Key Language Features
- Closures, classes with inheritance
- **Ternary operator**: `cond ? then_val : else_val` (compiler fixed)
- **`repeat N { }`** — iterate N times; supports `break`/`continue`
- **`repeat while cond { }`** / **`repeat until cond { }`** — condition-controlled loops
- **`match val { when P { } else { } }`** — pattern-matching compiled as if-elif-else chain
- **`try { } catch e { }`** — structured exception handling with proper stack unwinding
- **`throw expr`** — raise any value as an exception
- F-strings, pattern matching, closures, classes with inheritance
- Generational mark-and-sweep GC (minor/major/sweep now active, periodic every 64k instructions)
- X11-native GUI toolkit with PSS (Prism StyleSheet) styling engine
- JIT compilation for hot integer loops

## Performance Optimisations (recent)
- **Interned name constants**: `chunk_add_const_str` uses `value_string_intern` so VM name lookups
  benefit from pointer-equality fast path (`key == name`) before `strcmp` in `env_get`/`env_assign`/`env_is_const`.
- **Computed-goto dispatch** (GCC): 15–25% speedup over switch-based dispatch.
- **Periodic GC**: `gc_collect_minor` called every 65,536 instructions to avoid unbounded heap growth.
- **CFLAGS**: `-O2 -fno-gcse -march=native -fomit-frame-pointer -fno-strict-aliasing -DNDEBUG`

## Exception Handling Architecture
`vm_error` checks `vm->try_depth`. If inside a try block it:
1. Saves the error message to `vm->exception_msg`
2. Unwinds call frames back to the try-frame boundary
3. Discards extra stack values pushed since the try began
4. Redirects `frame->ip` to the catch handler
5. Pushes the exception string onto the stack
6. Sets `vm->exception_handled = 1` (no `had_error`)

`DISPATCH()` re-syncs the local `frame` pointer when `exception_handled` is set.

## Module Import Syntax
Prism supports a concise `%` import syntax with **automatic tree-shaking** —
only the symbols the program actually references are pulled into scope.

- `%libname` — imports referenced names from `lib/libname.pr` directly into the
  current scope. Unused declarations from the module are skipped entirely.
- `%libname as alias` — loads the module into a namespace bound to `alias`.
  Only members accessed via `alias.member` are imported into the namespace.

Example (`examples/import_demo.pr`):
```
%greet
output(HELLO)         # only HELLO + hi are pulled in
output(hi("world"))

%greet as g
output(g.BYE)         # only BYE + bye are pulled into the `g` namespace
output(g.bye("you"))
```
Implemented in `src/parser.c` (statement parsing) and `src/interpreter.c`
(`NODE_IMPORT` handler with reachability scan over the program AST).
