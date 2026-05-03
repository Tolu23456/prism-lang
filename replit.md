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
- **Bytecode VM** (default): `./prism program.pr`
- **Tree-walking interpreter**: `./prism --tree program.pr`
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
- **Ternary operator**: `cond ? then_val : else_val`
- **`repeat N { }`** — iterate N times; supports `break`/`continue`
- **`repeat while cond { }`** / **`repeat until cond { }`** — condition-controlled loops
- **`match val { when P { } else { } }`** — pattern-matching statement (compiled as if-elif-else chain)
- **`match val { when P: expr  else: expr }`** — match **expression** (inline form, yields a value; `NODE_MATCH_EXPR`)
- **`try { } catch e { }`** — structured exception handling with proper stack unwinding
- **`throw expr`** — raise any value as an exception
- F-strings, closures, classes with inheritance
- Generational mark-and-sweep GC (periodic sweep every 64k instructions via `gc_collect_minor`)
- X11-native GUI toolkit with PSS (Prism StyleSheet) styling engine

## Performance Optimisations
Baselines (original) → current:
- **fib_recursive**: 1.61s → 0.54s (**3.0×**)
- **loop_count** (5M iters): 0.92s → 0.20s (**4.6×**)
- **ackermann**: 1.24s → 0.40s (**3.1×**)
- **recursive_sum**: 0.36s → 0.11s (**3.2×**)

Techniques applied:
1. **Computed-goto dispatch** (GCC `&&label`): 15–25% vs switch dispatch.
2. **GC removed from hot DISPATCH macro**: was firing a full sweep every 65,536 instructions (~700 sweeps/benchmark); GC is now triggered lazily via `gc_collect_minor` from allocation sites only.
3. **OP_CALL single-pass param binding**: combined `env_set + local-fill` in one loop, eliminating a redundant `env_get` per parameter.
4. **Env slab pool** (512-entry): `env_new`/`env_free` are O(1) for standard-capacity (16-slot) envs via a pre-allocated pool.
5. **Skip OP_PUSH_SCOPE for empty blocks**: compiler checks `block_has_any_decl()`; blocks like `if n<=1 { return n }` skip `malloc`/`free` entirely.
6. **`chunk->no_env` flag** (`compiler.c`): functions whose bodies never emit `OP_DEFINE_NAME`, `OP_DEFINE_CONST`, or `OP_MAKE_FUNCTION` have `no_env=1`. `OP_CALL` skips `env_new()` entirely for those functions and fills locals directly from args (saves ~50ns per recursive call).
7. **Inline name cache for `OP_LOAD_NAME`/`OP_STORE_NAME`** (`vm.c`): each instruction's `InlineCache` entry stores `(name_env, name_slot)` after the first hash lookup. Subsequent accesses use direct array indexing — no hash computation. Validated by pointer-equality on `slots[slot].key`.
8. **Interned name constants**: `chunk_add_const_str` uses `value_string_intern`; hash-table probes use pointer equality before `strcmp`.
9. **CFLAGS**: `-O2 -fno-gcse -march=native -fomit-frame-pointer -fno-strict-aliasing -DNDEBUG`

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
