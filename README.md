# Prism

Prism is a general-purpose programming language built in C.
It uses the `.pm` extension and currently runs as an interpreted language.

## Highlights

- C implementation
- `.pm` source files
- Tree-walking interpreter
- Sublime Text syntax support
- Fast VM planned next

## Project Files

- `src/` - Lexer, parser, AST, values, interpreter, and entry point
- `examples/hello.pm` - Feature demo
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
```
