# Prism VM, Compiler & JIT

Prism has two execution engines that can be selected at runtime:

## Tree-Walking Interpreter (default)

The default engine evaluates the AST directly. It is the most complete and
feature-rich path — all language constructs are supported.

```bash
./prism program.pr          # uses tree-walker by default
```

## Bytecode VM

The `--vm` flag compiles the program to Prism bytecode first, then runs it on
the stack-based VM. The VM supports core data types, arithmetic, control flow,
function calls, closures, and classes.

```bash
./prism --vm program.pr
```

### Bytecode Cache

Compiled bytecode can be serialized to a `.pmc` file for faster repeated
execution:

```bash
./prism --emit-bytecode program.pr   # writes program.pmc
./prism program.pmc                  # runs directly from cache
```

### Benchmark Mode

The `--bench` flag enables built-in timing for VM execution:

```bash
./prism --bench --vm program.pr
```

---

## Instruction Set

The Prism bytecode VM uses a compact, fixed-width instruction set.

| Opcode | Description |
|--------|-------------|
| `OP_LOAD_CONST` | Push constant from pool |
| `OP_LOAD_VAR` | Load variable by name |
| `OP_STORE_VAR` | Store variable by name |
| `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV` | Arithmetic |
| `OP_MOD`, `OP_POW`, `OP_FLOOR_DIV` | More arithmetic |
| `OP_NEG`, `OP_NOT` | Unary operators |
| `OP_EQ`, `OP_NEQ`, `OP_LT`, `OP_LE`, `OP_GT`, `OP_GE` | Comparison |
| `OP_AND`, `OP_OR` | Logical |
| `OP_JUMP`, `OP_JUMP_IF_FALSE` | Control flow |
| `OP_CALL`, `OP_RETURN` | Function calls |
| `OP_BUILD_ARRAY`, `OP_BUILD_DICT` | Collection constructors |
| `OP_INDEX_GET`, `OP_INDEX_SET` | Subscript operations |
| `OP_GET_FIELD`, `OP_SET_FIELD` | Object field access |
| `OP_CALL_METHOD` | Method dispatch (with inline cache) |
| `OP_MAKE_CLOSURE` | Create function value with closure |
| `OP_PRINT` | Output value |
| `OP_HALT` | Stop execution |

---

## Compiler Pipeline

```
Source (.pr)
    │
    ▼ Lexer (src/lexer.c)
Token stream
    │
    ▼ Parser (src/parser.c)
AST (Abstract Syntax Tree)
    │
    ├──► Tree-walking interpreter (src/interpreter.c)
    │         └── Evaluates AST nodes directly
    │
    └──► Compiler (src/compiler.c)
             │
             ▼ Bytecode chunks (src/chunk.c)
             │
             ├──► Stack-based VM (src/vm.c)
             │         └── x86-64 JIT hot paths (src/jit.c)
             │
             └──► Transpiler (src/transpiler.c)
                       └── Outputs readable Prism source
```

---

## JIT Compilation (x86-64)

The JIT module (`src/jit.c`) provides fast-paths for integer arithmetic in the
VM. When the VM detects hot integer operations, it emits native x86-64
machine code directly into an executable buffer.

Hot paths currently JIT-compiled:
- Integer `+`, `-`, `*`, `/`, `//`, `%`
- Integer comparison (`==`, `<`, `>`)
- Bounds-checked array index load/store

Build a JIT-enabled binary with the release target:

```bash
make release
./prism-release --vm program.pr
```

---

## Performance Builds

| Target | Command | Flags |
|--------|---------|-------|
| Debug | `make` | `-g -O0` |
| Release | `make release` | `-O3 -DNDEBUG -march=native -fomit-frame-pointer` |
| Sanitizer | `make sanitize` | `-fsanitize=address,undefined` |
| PGO Generate | `make pgo-gen` | `-O2 -fprofile-generate` |
| PGO Apply | `make pgo-use` | `-O3 -fprofile-use` |

---

## Transpiler

The transpiler (`src/transpiler.c`) converts Prism AST back to clean,
formatted Prism source. It is used by the formatter:

```bash
./prism --format program.pr         # print formatted source
./prism --format-write program.pr   # format in-place
```
