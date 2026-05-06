# Prism Programming Language

## Overview
Prism is a dynamically-typed, general-purpose programming language implemented in C11. It features multiple execution modes and modern language features.

## Tech Stack
- **Core**: C11 (gcc)
- **Build System**: GNU Make
- **Dependencies** (via Nix): gcc, gnumake, X11, Xft, Xrender, fontconfig, freetype, pkg-config

## Project Structure
- `src/` - Core C implementation: lexer, parser, AST, interpreter, VM, JIT, GC, transpiler, GUI
- `lib/` - Standard library written in Prism (`.pr` files): async, collections, crypto, fs, json, math, onion, etc.
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

## Important: OOP Patterns
- Prism's `class` keyword is parsed but **not handled by the bytecode VM compiler or default interpreter**.
- Use **closure-based OOP** (dicts + functions) — the pattern used in `examples/classes_demo.pr` and `examples/closures_and_oop.pr`.
- Use `elif` (not `else if`) for chained conditionals.

## Key Language Features
- Closures, closure-based OOP (dicts + functions)
- **Ternary operator**: `cond ? then_val : else_val`
- **Null coalescing**: `expr ?? fallback`
- **`repeat N { }`** — iterate N times; supports `break`/`continue`
- **`repeat while cond { }`** / **`repeat until cond { }`** — condition-controlled loops
- **`match val { when P { } else { } }`** — pattern-matching statement (compiled as if-elif-else chain)
- **`match val { when P: expr  else: expr }`** — match **expression** (inline form, yields a value; `NODE_MATCH_EXPR`)
- **`try { } catch e { }`** — structured exception handling with proper stack unwinding
- **`throw expr`** — raise any value as an exception
- **Logical keywords**: `and` / `or` / `not` are full aliases for `&&` / `||` / `!` in both VM and tree-walker
- F-strings, closures, classes with inheritance
- Generational mark-and-sweep GC (periodic sweep every 64k instructions via `gc_collect_minor`)
- X11-native GUI toolkit with PSS (Prism StyleSheet) styling engine

## Performance Optimisations
Baselines (original) → current measured results:
- **fib_recursive** fib(32): 1.77s → **0.527s** (**3.4×**)
- **loop_count** (5M iters): 0.93s → **0.260s** (**3.6×**)
- **ackermann** ack(3,5)×100: 1.23s → **0.376s** (**3.3×**)
- **recursive_sum**: 0.39s → **0.104s** (**3.7×**)
- **sieve** (primes ≤500k): 0.54s → **0.244s** (**2.2×**)

Techniques applied:
1. **Computed-goto dispatch** (GCC `&&label`): 15–25% vs switch dispatch.
2. **GC removed from hot DISPATCH macro**: was firing a full sweep every 65,536 instructions (~700 sweeps/benchmark); GC is now triggered lazily via `gc_collect_minor` from allocation sites only.
3. **OP_CALL single-pass param binding**: combined `env_set + local-fill` in one loop, eliminating a redundant `env_get` per parameter.
4. **Env slab pool** (512-entry): `env_new`/`env_free` are O(1) for standard-capacity (16-slot) envs via a pre-allocated pool.
5. **Skip OP_PUSH_SCOPE for empty blocks**: compiler checks `block_has_any_decl()`; blocks like `if n<=1 { return n }` skip `malloc`/`free` entirely.
6. **`chunk->no_env` flag** (`compiler.c`): functions whose bodies never emit `OP_DEFINE_NAME`, `OP_DEFINE_CONST`, or `OP_MAKE_FUNCTION` have `no_env=1`. `OP_CALL` skips `env_new()` entirely for those functions and fills locals directly from args.
7. **Inline name cache for `OP_LOAD_NAME`/`OP_STORE_NAME`** (`vm.c`): each instruction's `InlineCache` entry stores `(name_env, name_slot)` after the first hash lookup. Subsequent accesses use direct array indexing — no hash computation.
8. **Interned name constants**: `chunk_add_const_str` uses `value_string_intern`; hash-table probes use pointer equality before `strcmp`.
9. **Frame size reduction** (`VM_LOCALS_MAX` 256→64, removed `local_names[256]`): `CallFrame` 4.1 KB → 0.55 KB; total `vm->frames[2048]` array **8.4 MB → 1.1 MB** (now fits in L3 cache for deep recursive workloads).
10. **Minimal `memset` per call** (`Chunk.local_count_max`): compiler stores the high-water mark of local slots; `OP_CALL` zeros only slots `[param_count, local_count_max)`. For `fib(n)` and `ackermann(m,n)` this is **zero bytes** per call.
11. **JIT `OP_PUSH_INT_IMM` support**: small integer literals (e.g. literal `1` in `i += 1`) no longer abort the trace recorder. `loop_count.pr` now JITs at native speed.
12. **JIT frame-local variable support** (`OP_LOAD_LOCAL`, `OP_STORE_LOCAL`, `OP_INC_LOCAL`, `OP_DEC_LOCAL`): loops inside functions now JIT using direct `frame->locals[slot]` access. `JitTrace.var_local_slots[]` maps JIT register slots to either `locals[i]` or `env_get(name)`.
13. **vm.c at `-O2`** with `-fno-optimize-sibling-calls -fstack-reuse=none`: disables the two passes that previously caused computed-goto dispatch-loop miscompilation while enabling all other -O2 optimisations.
14. **JIT hot threshold lowered** (200→50): hot traces compile 4× sooner.

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

**VM import limitation**: the bytecode VM's `OP_IMPORT` runs the module in
the caller's environment (star-import only). `import X as alias` dict-namespacing
only works in the tree-walking interpreter (`--tree`). Always use
`import onion` (no alias) in code targeting the default VM.

## onion — Serialisation Library (`lib/onion.pr`)
Prism-native equivalent of Python's `pickle`, using `.oni` files.

```prism
import onion

# Serialise any value to a string
let s = dumps({"name": "Alice", "scores": [98, 87]})

# Deserialise back
let d = loads(s)

# Write / read .oni files
dump(d, "data.oni")
let d2 = load("data.oni")

# Deep-copy via round-trip
let clone = copy(d)
```

ONI wire format: `N` null · `T/F` bool · `I42;` int · `R3.14;` float ·
`"str"` string · `[n:items]` array · `{n:kvs}` dict · `(n:items)` tuple

## strings — String Utilities Library (`lib/strings.pr`)
Unicode-aware string manipulation. Works in both VM and tree-walker modes.

```prism
import strings

# UTF-8 aware (codepoint-based)
utf8_len("café")               # → 4  (not 5 bytes)
utf8_chars("café")             # → ["c","a","f","é"]
utf8_codepoints("café")        # → [99, 97, 102, 233]
utf8_slice("αβγδε", 1, 4)      # → "βγδ"
utf8_slice("αβγδε", -2, null)  # → "δε"
utf8_encode([72, 101, 108, 108, 111])  # → "Hello"

# General utilities
capitalize("hello world")      # → "Hello world"
titleCase("the quick fox")     # → "The Quick Fox"
camelCase("hello world")       # → "helloWorld"
snakeCase("Hello World")       # → "hello_world"
padLeft("42", 8, null)         # → "      42"  (uses utf8_len for width)
padCenter("OK", 10, "-")       # → "----OK----"
reverse("café")                # → "éfac"       (codepoint-safe)
truncate("The quick brown fox", 12, "…")  # → "The quick b…"
isPalindrome("racecar")        # → true
levenshtein("kitten", "sitting")  # → 3
slugify("Hello World! 123")    # → "hello-world-123"
template("Hi {{name}}!", {"name": "Alice"})  # → "Hi Alice!"
```

NOTE: `chars()` builtin now returns one element per Unicode codepoint (not one byte).
`len()` and `slice()` remain byte-based — use `utf8_len()` / `utf8_slice()` for character-indexed operations on non-ASCII strings.

## json — JSON Library (`lib/json.pr`)
Full RFC 8259 JSON encoder / decoder. Works in both VM and tree-walker modes.

```prism
import json

# Encode Prism value → compact JSON string
let s = stringify({"name": "Alice", "scores": [98, 87]})

# Decode JSON string → Prism value
let d = parse(s)

# Pretty-printed output (2-space indent)
output(pretty(d))

# Validation and safe decode
isValid("[1,2,3]")          # → true
parseOr("bad!", null)       # → null (no throw)
minify("{ \"x\" : 1 }")    # → {"x":1}
```

NOTE: `\uXXXX` escapes decode correctly for the full Unicode range (U+0000–U+10FFFF).
`chr()` and `ord()` encode/decode proper UTF-8 in both VM and tree-walker modes.

## Gotchas
- **`"{"` is an empty string** — Prism's lexer treats `{` inside double-quoted
  strings as an f-string interpolation opener, so `"{"` yields `""`.
  Use `chr(123)` to get the literal `{` character. Same issue does NOT affect
  `"}"`, `"["`, `"]"`, `"("`, or `")"`.
- `import X as alias` (dict-namespacing) only works in `--tree` mode; use
  plain `import X` (star-import) when targeting the default bytecode VM.
