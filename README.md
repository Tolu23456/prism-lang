# Prism

Prism is a general-purpose programming language built in C.
It uses the `.pm` extension and currently runs as an interpreted language.

## Highlights

- C implementation
- `.pm` source files
- Tree-walking interpreter and stack-based VM
- Bytecode cache output with `.pmc` files
- PGUI native GTK-style GUI helpers built into the C core
- Sublime Text syntax support

## Project Files

- `src/` - Lexer, parser, AST, values, interpreter, and entry point
- `examples/hello.pm` - Feature demo
- `examples/pgui_demo.pm` - PGUI native GUI demo
- `RULES.txt` - Language specification
- `todo.md` - Next development tasks
- `CHANGELOG.md` - Project history
- `Prism.sublime-syntax` - Sublime Text syntax file

## Build

```bash
make
```

## Run

```bash
./prism examples/hello.pm
./prism --emit-bytecode examples/hello.pm
./prism --bench examples/hello.pm
```
