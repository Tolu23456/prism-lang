# Prism Language

An interpreted programming language built in C. Source files use the `.pm` extension.

## Project Structure

```
src/
  lexer.h / lexer.c       — Tokenizer: converts source text into tokens
  ast.h / ast.c           — AST node definitions and memory management
  parser.h / parser.c     — Recursive descent parser: builds AST from tokens
  value.h / value.c       — Runtime value types with reference counting and hash-indexed dictionaries
  gc.h / gc.c             — AGC scaffold: allocation tracking, root audits, memory stats
  interpreter.h / interpreter.c — Tree-walking interpreter
  gui_native.h / gui_native.c — Native framebuffer GUI and PGUI GTK-style toolkit
  formatter.h / formatter.c — Built-in Prism source formatter
  main.c                  — Entry point: REPL, formatter, and file execution

examples/
  hello.pm                — Comprehensive feature demo
  gui_demo.pm             — Original GUI helper demo
  pgui_demo.pm            — PGUI GTK-style native toolkit demo

Makefile                  — Build system (gcc)
RULES.txt                 — Language specification
CHANGELOG.md              — Project changes and history
todo.md                   — Next development tasks
```

## Building

```bash
make          # builds ./prism binary
make clean    # removes build artifacts
make run      # builds and runs examples/hello.pm
```

## Replit Environment

The project is configured for Replit as a native C project using the C toolchain module. The `Start application` workflow builds the interpreter with `make` and runs `examples/hello.pm` as a console smoke test.

This is a CLI/interpreter project, not a web application. It does not require a browser preview, HTTP server, client/server API boundary, or external package installation to run safely in Replit. The migration was verified by building the `prism` binary and running the bundled hello example through the Replit workflow without runtime errors.

## Running

```bash
./prism file.pm              # run a .pm source file
./prism --format file.pm     # print formatted Prism source
./prism --format-write file.pm # format a Prism source file in place
./prism                      # start the interactive REPL
```

## VM Performance Notes

- The VM dispatch loop in `src/vm.c` compiles on x86-64 to an indirect jump-table dispatch under optimized GCC builds.
- Integer `+`, `-`, `*`, `&`, `|`, and `^` bytecode paths use guarded x86-64 inline assembly helpers with portable C fallbacks.
- Bytecode chunks now carry per-instruction inline caches. `OP_GET_ATTR`/`OP_SET_ATTR` cache dictionary slot indexes with dictionary version invalidation, while `OP_CALL_METHOD` caches receiver type and resolved built-in method ID to avoid repeated string-based method lookup on hot call sites.

## Instructions for the next agent

- Read `todo.md` first.
- Read `CHANGELOG.md` second.
- Update `todo.md` whenever the plan changes.
- Keep both files current and consistent.

## Language Features

- **Variables**: `let x = 10` / `const PI = 3.14`
- **Types**: int, float, complex, string, bool (true/false/unknown), array, dict, set, tuple, null
- **Strings**: single/double/triple quotes, f-strings `f"Hello {name}"`, verbatim `@"C:\path"`
- **Arrays**: `[1, 2, 3]` or `arr[1, 2, 3]`, with `.add()`, `.pop()`, `.sort()` etc.
- **Dicts**: `{"key": "value"}` with `.keys()`, `.values()`, `.items()`, `.erase()`
- **Sets**: `{1, 2, 3}` with `|` union, `&` intersection, `-` difference, `^` sym-diff
- **Tuples**: `(1, 2, 3)` or trailing-comma `let t = 1,`
- **Control flow**: `if/elif/else`, `while`, `for x in iterable`, `break`, `continue`
- **Functions**: `func name(type param) { ... }` with `return`
- **I/O**: `output(...)`, `input(prompt)`
- **PGUI**: native GTK3-style GUI helpers `pgui_window`, `pgui_label`, `pgui_button`, `pgui_input`, `pgui_box`, `pgui_run`
- **Operators**: arithmetic `+ - * / % **`, comparison, logical `&& || !`, bitwise `& | ^ ~`
- **Membership**: `x in arr`, `x not in arr`
- **Slicing**: `s[start:stop:step]` for strings, arrays, tuples

## Tech Stack

- **Language**: C (C11)
- **Compiler**: GCC
- **Architecture**: Tree-walking interpreter (Lexer → Parser → AST → Interpreter)
- **Memory**: Reference-counted values
- **GUI**: PGUI is implemented in Prism's C core as a GTK3-style renderer without linking GTK3, third-party modules, or language bindings.
- **AGC Roadmap**: `pipeline.md` describes the planned Adaptive Memory Engine. Current code includes an AGC scaffold that tracks runtime `Value` allocations, audits roots, and reports memory statistics while remaining compatible with existing reference counting.
