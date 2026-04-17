# Prism Language

An interpreted programming language built in C. Source files use the `.pr` extension.

## Project Structure

```
src/
  lexer.h / lexer.c       — Tokenizer: converts source text into tokens
  ast.h / ast.c           — AST node definitions and memory management
  parser.h / parser.c     — Recursive descent parser: builds AST from tokens
  value.h / value.c       — Runtime value types with reference counting and hash-indexed dictionaries
  gc.h / gc.c             — AGC scaffold: allocation tracking, root audits, memory stats
  interpreter.h / interpreter.c — Tree-walking interpreter
  vm.h / vm.c             — Stack-based bytecode VM
  chunk.h / chunk.c       — Bytecode chunk format
  compiler.h / compiler.c — AST → bytecode compiler
  jit.h / jit.c           — Trace-based JIT compiler (x86-64 + ARM64 codegen, LLVM IR emitter)
  transpiler.h / transpiler.c — AST → standalone C transpiler (--emit-c)
  pss.h / pss.c           — PSS stylesheet parser (CSS-like); v2: 40+ widget types, :root vars, var(), rgb(), shadow, opacity, margin, font-weight, text-decoration, cursor, per-side borders
  xgui.h / xgui.c         — Native X11 GUI engine (Xlib + Xft)
  gui_native.h / gui_native.c — PGUI GTK-style toolkit (legacy)
  formatter.h / formatter.c — Built-in Prism source formatter
  main.c                  — Entry point: REPL, formatter, and file execution

examples/
  hello.pr                — Comprehensive feature demo
  gui_demo.pr             — Original GUI helper demo
  pgui_demo.pr            — PGUI GTK-style native toolkit demo
  new_features.pr         — New language features demo
  default.pss             — Default PSS style sheet for XGUI

tests/
  test_arithmetic.pr      — Arithmetic operators, augmented assignment, bitwise
  test_collections.pr     — Arrays, dicts, sets, tuples
  test_control.pr         — if/elif/else, while, for, break, continue, logic
  test_functions.pr       — Functions, recursion, higher-order functions
  test_pss.pr             — PSS stylesheet parser (no X11 needed)
  test_strings.pr         — String methods, slicing, f-strings, operators
  test_typecast.pr        — int/float/bool/str/complex/array/tuple/set casts
  test_types.pr           — type() built-in and VAL_* type tags
  run_tests.sh            — Shell test runner (used by `make test`)

benchmarks/
  fib_recursive.pr        — Fibonacci(32) via tree recursion (~7.9 M calls)
  fib_iterative.pr        — Fibonacci via 2,000,000 loop iterations
  loop_count.pr           — Tight while-loop: 5,000,000 iterations
  sieve.pr                — Sieve of Eratosthenes up to 500,000
  bubble_sort.pr          — Bubble sort 800 elements (worst case)
  dict_ops.pr             — 10,000 dict inserts + 10,000 dict lookups
  string_ops.pr           — 50,000 × (join/split/upper/startswith)
  recursive_sum.pr        — rsum(200) × 5,000 = 1 M recursive calls
  ackermann.pr            — ack(3,5) × 100 = ~1.03 M recursive calls
  array_ops.pr            — 100,000 array add + read + slice
  RESULTS.md              — Timing results table (wall-clock, debug build)

Makefile                  — Build system (gcc, pkg-config auto-detects X11)
RULES.txt                 — Language specification
CHANGELOG.md              — Project changes and history
todo.md                   — Next development tasks
```

## Building

```bash
make          # builds ./prism binary
make clean    # removes build artifacts
make run      # builds and runs examples/hello.pr
make test     # builds and runs all tests/test_*.pr files
make sanitize # builds ./prism-san with -fsanitize=address,undefined
```

## Replit Environment

The project is configured for Replit as a native C project using the C toolchain module. The `Start application` workflow builds the interpreter with `make` and runs `examples/hello.pr` as a console smoke test.

This is a CLI/interpreter project, not a web application. It does not require a browser preview, HTTP server, client/server API boundary, or external package installation to run safely in Replit. The migration was verified by building the `prism` binary and running the bundled hello example through the Replit workflow without runtime errors.

## Running

```bash
./prism file.pr              # run a .pr source file (tree-walker, default)
./prism --vm file.pr         # run with bytecode VM
./prism --jit file.pr        # run with JIT enabled (hot integer loops compiled to native code)
./prism --jit-verbose file.pr # JIT + print IR and stats
./prism --emit-c file.pr     # transpile to standalone C source (stdout)
./prism --emit-llvm file.pr  # emit LLVM IR for hot loops (stdout)
./prism --format file.pr     # print formatted Prism source
./prism --format-write file.pr # format a Prism source file in place
./prism                      # start the interactive REPL
```

## VM Performance Notes

- The VM dispatch loop in `src/vm.c` compiles on x86-64 to an indirect jump-table dispatch under optimized GCC builds.
- Integer `+`, `-`, `*`, `&`, `|`, and `^` bytecode paths use guarded x86-64 inline assembly helpers with portable C fallbacks.
- Bytecode chunks now carry per-instruction inline caches. `OP_GET_ATTR`/`OP_SET_ATTR` cache dictionary slot indexes with dictionary version invalidation, while `OP_CALL_METHOD` caches receiver type and resolved built-in method ID to avoid repeated string-based method lookup on hot call sites.
- **JIT compiler** (`--jit`): backward jumps on `OP_JUMP` are profiled; loops crossing a hot threshold (200 back-edges) are trace-recorded into a flat `JIRInstr` IR and compiled to native machine code via `mmap(PROT_EXEC)`. x86-64 and ARM64 backends share the same IR. Compiled traces are cached and reused on subsequent iterations. Guard failures transparently fall back to the interpreter.

## Instructions for the next agent

- Read `todo.md` first.
- Read `CHANGELOG.md` second.
- Update `todo.md` whenever the plan changes.
- Keep both files current and consistent.

## Bugs Fixed (Session Notes)

- **`elif` in multi-line blocks**: Added `skip_newlines()` before `while (check(p, TOKEN_ELIF))` and after each `elif` body in `parse_if()` in `src/parser.c`. Previously `elif` only worked when on the same line as the closing `}`.
- **`**` right-associativity**: Changed `parse_power()` right-hand side from `parse_unary(p)` to `parse_power(p)` in `src/parser.c`. Previously `2 ** 3 ** 2` was a parse error.
- **Set equality**: Added `VAL_SET` case to `value_equals()` in `src/value.c`. Previously two sets with identical elements compared as unequal (fell through to pointer comparison).
- **GC: nested function chunk constants not marked (use-after-free)**: `gc_mark_value` for `VAL_FUNCTION` only marked the closure env, not `func.chunk`. String/int literals inside nested function bytecode were swept while still owned by the chunk, causing a use-after-free when `chunk_free` tried to release them. Fixed by calling `gc_mark_chunk(gc, value->func.chunk)` in `gc_mark_value`.
- **GC: `fread` unused-result warnings**: `(void)fread(...)` does not silence GCC's `__attribute__((warn_unused_result))`; replaced with `{ size_t _nr = fread(...); (void)_nr; }` in `main.c`, `interpreter.c`, and `vm.c`.
- **GC: shutdown sweep count inaccurate**: the "from N live objects" count in the shutdown sweep message read `gc->stats.live_objects` (already decremented by earlier sweeps) instead of walking the actual tracked-object list; fixed to count nodes in `gc->objects` directly.

## Known Language Limitations (Documented in edgecase/)

- **`arr` is a reserved keyword**: Cannot be used as a variable or parameter name. Use `lst`, `nums`, etc. The syntax `arr[a, b, c]` creates an array literal.
- **`<<` / `>>` (shift operators)**: Lexed but not yet parsed or interpreted. Commented out in edge case files.
- **`|>` (pipe operator)**: Lexed (`TOKEN_PIPE_ARROW`) but not yet handled in the parser. Commented out.
- **True closures over returned frames**: Returning an inner function that captures locals from an outer function that has already returned causes a segfault. Closures over global/module-level variables work correctly.
- **`set.clear()` method**: Not yet implemented. `array.clear()` and `dict.clear()` do exist.
- **Empty dict literal**: `{}` creates an empty **set**, not a dict. Use `dict()` for an empty dict.
- **`/` is float division**: `7 / 2` returns `3.5`, not `3`. There is no integer floor-division operator.
- **`catch` error format**: Caught values are always strings in the form `"line N: VALUE"` regardless of the throw type.
- **`dict()` equality**: Not yet implemented in `value_equals`; dicts fall through to pointer comparison.

## Edge Case Test Files (edgecase/)

14 `.pr` files exercising every major syntax and runtime feature. All 14 pass as of the current build:

| File | Coverage |
|---|---|
| `arrays.pr` | indexing, mutation, slicing, nesting, add/remove/pop/sort/reverse/count/index/clear |
| `closures.pr` | captured vars, mutation, shared state, nested scopes (factory pattern noted as unsupported) |
| `control_flow.pr` | if/elif/else chains, while, for, break, continue, nesting |
| `dicts.pr` | creation, get/set, keys/values/items, erase/has/remove/clear, equality |
| `error_handling.pr` | try/catch/finally, throw, nested try, rethrow |
| `functions.pr` | defaults, recursion, higher-order, variadic, nested |
| `match.pr` | when/else, literal and expression match arms |
| `numbers.pr` | int/float/complex, underscore separators, hex/bin/oct literals, overflow |
| `operators.pr` | arithmetic, bitwise, comparison, logical, augmented assign, `**` right-assoc |
| `sets.pr` | union, intersection, difference, sym-diff, equality, contains |
| `strings.pr` | f-strings, slicing, all methods, verbatim strings |
| `tuples.pr` | creation, indexing, nesting, iteration |
| `types.pr` | `type()` built-in, `is`/`is not`, typecasting |
| `variables.pr` | let, const, walrus, shadowing, null safety `?.` and `??` |

Run individually with `./prism edgecase/<file>.pr` or all at once:

```bash
for f in edgecase/*.pr; do echo -n "$(basename $f): "; ./prism "$f" 2>&1 | tail -1; done
```

## Language Features

- **Variables**: `let x = 10` / `const PI = 3.14` / walrus `x := 10`
- **Types**: int, float, complex, string, bool (true/false/unknown), array, dict, set, tuple, null
- **Strings**: single/double/triple quotes, f-strings `f"Hello {name}"`, auto-interpolation `"Hello {name}"` (double-quoted), verbatim `@"C:\path"`
- **Arrays**: `[1, 2, 3]` or `arr[1, 2, 3]`, with `.add()`, `.pop()`, `.sort()`, `.clear()`, `.contains()`, `.reverse()`, `.join()` etc.
- **Dicts**: `{"key": "value"}` with `.keys()`, `.values()`, `.items()`, `.erase()`, `.has()`, `.remove()`, `.clear()`, `.get(key, default)`
- **Sets**: `{1, 2, 3}` with `|` union, `&` intersection, `-` difference, `^` sym-diff
- **Tuples**: `(1, 2, 3)` or trailing-comma `let t = 1,`
- **Control flow**: `if/elif/else`, `while`, `for x in iterable`, `break`, `continue`
- **Repeat**: `repeat N { }` (count), `repeat while cond { }`, `repeat until cond { }`
- **Ranges**: `1..10` (inclusive), `1..10 step 2` (with step)
- **Match**: `match val { when x { } when y { } else { } }`
- **Error handling**: `try { } catch err { } finally { }`, `throw "message"`
- **Functions**: `func name(type param, param = default) { ... }` with `return`; supports default parameter values
- **Imports**: `import "module"`, `import "module" as alias`, `from "module" import symbol`
- **Null safety**: `obj?.method()`, `obj?.property`, `val ?? default`
- **Type checks**: `x is int`, `x is not str`
- **Pipe operator**: `|>` for functional chaining
- **I/O**: `output(...)`, `input(prompt)`
- **Typecasting**: `int(x)`, `float(x)`, `bool(x)`, `str(x)`, `complex(real, imag)`, `array(x)`, `tuple(x)`, `set(x)`, `dict()` — convert between all types. `int()` recognises `"0xFF"`, `"0b1010"`, `"0o17"` string literals.
- **String methods**: `.upper()`, `.lower()`, `.strip()`, `.split()`, `.join()`, `.replace()`, `.contains()`, `.before()`, `.after()`, `.reverse()`, `.words()`, `.lines()`, `.startswith()`, `.endswith()`, `.title()`, `.format()`
- **Numeric literals**: `1_000_000` (underscore separators), `0x1F` (hex), `0b1010` (binary), `0o17` (octal), `3.5j` (complex)
- **Assertions**: `assert(cond, msg)` / `assert_eq(a, b, msg)` — used by `make test`
- **Memory tools**: `memory.stats()`, `memory.collect()`, `memory.limit("512mb")`, `memory.profile()`
- **PGUI**: legacy web-rendered GUI helpers `gui_window`, `gui_label`, `gui_button`, `gui_input`, `gui_run`
- **XGUI**: native X11 desktop GUI — `xgui_init(w,h,title)`, etc.
- **Operators**: arithmetic `+ - * / % **`, comparison, logical `&& || !`, bitwise `& | ^ ~`
- **Membership**: `x in arr`, `x not in arr`
- **Slicing**: `s[start:stop:step]` for strings, arrays, tuples

## GC / Memory System

The GC is a generational mark-and-sweep collector layered on top of reference counting:

- **Default mode**: sweep is always on (`gc_init` sets `sweep_enabled = true`). `PRISM_GC_SWEEP=0` disables it for debugging. The old `--gc-sweep` CLI flag is a no-op.
- **Generations**: young/old split; minor collections sweep only young-gen. Major collections sweep everything.
- **Root stack**: `gc_push_root` / `gc_pop_root` protect in-flight values during expression evaluation.
- **Precise rooting**: all GC roots are marked — VM stack, all Env chains, chunk constant pools, function chunk constants (`func.chunk`), interned string table.
- **Shutdown sweep**: at process exit, one final major collection runs; any surviving objects are reported as confirmed leaks and forcibly reclaimed.
- **Sanitizer build**: `make sanitize` produces `prism-san` with `-fsanitize=address,undefined`. Run with `ASAN_OPTIONS=detect_leaks=0 ./prism-san file.pr`. ASan exits clean (exit 0) on the full example suite. LSan is disabled by default due to ptrace restrictions in the sandbox; use `valgrind --leak-check=full` as an alternative.
- **GC diagnostics**: `PRISM_GC_STATS=1 ./prism file.pr` prints a per-type allocation/free/live breakdown and the shutdown sweep report to stderr.

## Execution Model

- **Default**: Tree-walking interpreter (full new syntax support)
- **`--vm`**: Bytecode VM (legacy syntax; new AST nodes fall back gracefully)
- **`--jit`**: JIT compiler for hot integer loops in VM mode

## Tech Stack

- **Language**: C (C11)
- **Compiler**: GCC
- **Architecture**: Tree-walking interpreter (Lexer → Parser → AST → Interpreter)
- **Memory**: Reference-counted values
- **GUI**: PGUI is implemented in Prism's C core as a GTK3-style renderer without linking GTK3, third-party modules, or language bindings.
- **AGC Roadmap**: `pipeline.md` describes the planned Adaptive Memory Engine. Current code includes an AGC scaffold that tracks runtime `Value` allocations, audits roots, and reports memory statistics while remaining compatible with existing reference counting.
