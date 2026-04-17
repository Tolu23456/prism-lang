# Prism

Prism is a general-purpose programming language built in C.
It uses the `.pr` extension and currently runs as an interpreted language.

## Highlights

- C implementation
- `.pr` source files
- Tree-walking interpreter and stack-based VM
- Bytecode cache output with `.pmc` files
- PGUI native GTK-style GUI helpers built into the C core
- PSS (Prism StyleSheet) theming with 40+ widget types
- Standard library modules in `lib/`
- Sublime Text syntax support

## Project Files

- `src/` - Lexer, parser, AST, values, interpreter, and entry point
- `lib/` - Standard library modules (math, string, array, json, …)
- `examples/hello.pr` - Feature demo
- `examples/default.pss` - Catppuccin Mocha dark theme
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
./prism examples/hello.pr
./prism --emit-bytecode examples/hello.pr
./prism --bench examples/hello.pr
```

## Importing modules

```prism
import "lib/math"          // resolves lib/math.pr automatically
import "lib/string.pr"     // explicit extension also works
import "mymodule"          // tries: mymodule, mymodule.pr, lib/mymodule, lib/mymodule.pr
```
