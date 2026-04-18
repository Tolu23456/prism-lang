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
- Pattern matching, f-strings
- Generational mark-and-sweep garbage collector with adaptive GC policies
- X11-native GUI toolkit with PSS (Prism StyleSheet) styling engine
- JIT compilation for hot integer loops
