# Prism Language

An interpreted programming language built in C. Source files use the `.pm` extension.

## Project Structure

```
src/
  lexer.h / lexer.c       — Tokenizer: converts source text into tokens
  ast.h / ast.c           — AST node definitions and memory management
  parser.h / parser.c     — Recursive descent parser: builds AST from tokens
  value.h / value.c       — Runtime value types with reference counting
  interpreter.h / interpreter.c — Tree-walking interpreter
  main.c                  — Entry point: REPL and file execution

examples/
  hello.pm                — Comprehensive feature demo

Makefile                  — Build system (gcc)
RULES.txt                 — Language specification
```

## Building

```bash
make          # builds ./prism binary
make clean    # removes build artifacts
make run      # builds and runs examples/hello.pm
```

## Running

```bash
./prism file.pm    # run a .pm source file
./prism            # start the interactive REPL
```

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
- **Operators**: arithmetic `+ - * / % **`, comparison, logical `&& || !`, bitwise `& | ^ ~`
- **Membership**: `x in arr`, `x not in arr`
- **Slicing**: `s[start:stop:step]` for strings, arrays, tuples

## Tech Stack

- **Language**: C (C11)
- **Compiler**: GCC
- **Architecture**: Tree-walking interpreter (Lexer → Parser → AST → Interpreter)
- **Memory**: Reference-counted values
