# Prism VM Internals

This document describes the Prism bytecode virtual machine architecture,
optimization techniques, and the garbage collector.

---

## Architecture Overview

```
Source (.pr)
  │
  ├─► Lexer (src/lexer.c)
  │     Tokenizes source into TOKEN_* stream
  │
  ├─► Parser (src/parser.c)
  │     Produces an AST (Abstract Syntax Tree)
  │
  ├─► Compiler (src/compiler.c)
  │     Walks the AST, emits Bytecode (Chunk)
  │     Applies constant folding and peephole optimizations
  │
  └─► VM (src/vm.c)
        Executes bytecode in a stack-based dispatch loop
        Includes: method dispatch hash table, inline caches,
                  JIT hot-loop detection, local variable slots
```

---

## Bytecode Format

Each **Chunk** holds:
- `code[]` — array of `uint8_t` instructions
- `constants[]` — constant pool (Value pointers)
- `lines[]` — source line number per instruction byte
- `inline_caches[]` — polymorphic inline cache entries

### Instruction Sizes
| Size | Description |
|------|-------------|
| 1 byte | Opcode only (no operand) |
| 3 bytes | Opcode + uint16_t operand |
| 5 bytes | Opcode + two uint16_t operands (e.g. `OP_CALL_METHOD`) |
| 5 bytes | Opcode + int32_t wide offset (wide jump variants) |

---

## Key Opcodes

### Local Variable Slots (O(1) access)

| Opcode | Operand | Description |
|--------|---------|-------------|
| `OP_DEFINE_LOCAL` | slot | `frame->locals[slot] = pop()` |
| `OP_STORE_LOCAL`  | slot | `frame->locals[slot] = pop()` |
| `OP_LOAD_LOCAL`   | slot | `push(frame->locals[slot])` |
| `OP_INC_LOCAL`    | slot | `frame->locals[slot] += 1` |
| `OP_DEC_LOCAL`    | slot | `frame->locals[slot] -= 1` |

Local slots bypass the environment hash table entirely — each function
frame has a flat `Value*[256]` array (`frame->locals`) for O(1) access.

### Specialized Integer Operations

When the compiler can statically verify both operands are integers,
it emits these type-specialized opcodes that skip the type dispatch:

| Opcode | Description |
|--------|-------------|
| `OP_ADD_INT` | `push(a->int_val + b->int_val)` |
| `OP_SUB_INT` | Integer subtract |
| `OP_MUL_INT` | Integer multiply |
| `OP_DIV_INT` | Integer divide (with zero check) |
| `OP_MOD_INT` | Integer modulo |
| `OP_LT_INT`, `OP_LE_INT`, `OP_GT_INT`, `OP_GE_INT`, `OP_EQ_INT`, `OP_NE_INT` | Comparison |

### Small Integer Immediate

```
OP_PUSH_INT_IMM   int16_t
```

Integers in the range `[-32768, 32767]` are pushed directly as a 3-byte
instruction (opcode + signed 16-bit value) instead of a constant pool entry.
This eliminates a pointer dereference per integer literal.

### Range Creation

```
OP_MAKE_RANGE   flags
```

`flags` bit 0: has explicit step; bit 1: inclusive stop (`..=`).
Pops start/stop (and optionally step) from the stack and pushes a new
integer array.

### Other New Opcodes

| Opcode | Description |
|--------|-------------|
| `OP_IDIV` | Floor division (`//`) |
| `OP_NULL_COAL` | `??` — if lhs is null, use rhs |
| `OP_PIPE` | `\|>` — pipe lhs as first arg of rhs function |
| `OP_LINK_STYLE` | Load PSS file at runtime |
| `OP_EXPECT` | Assert with message (non-recoverable) |
| `OP_SAFE_GET_ATTR` | `?.` safe attribute access |
| `OP_SAFE_GET_INDEX` | `?[]` safe index |
| `OP_JUMP_WIDE` | 32-bit signed jump offset |
| `OP_TAIL_CALL` | Tail-call optimized function call |

---

## Method Dispatch

The VM uses a **single open-address hash table** (`s_method_table`) built
once at startup using `method_table_init()`. Keys are interned string
pointers + `ValueType` pairs.

```
hash(type, interned_name) → slot → method_id → switch-case handler
```

This gives **O(1) method dispatch** via pointer-equality comparison on
interned strings — identical string values share one allocation in the
global intern table.

### Inline Caches

`OP_CALL_METHOD` and `OP_GET_ATTR` maintain per-callsite **inline caches**
(`InlineCache` structs stored parallel to the bytecode). On the first call,
the method id and receiver type are cached. On subsequent calls with the
same receiver type, the hash lookup is skipped entirely.

---

## Compiler Optimizations

### Constant Folding

Binary expressions where **both operands are integer or float literals**
are evaluated at compile time:

```prism
let x = 2 + 3        # → OP_PUSH_INT_IMM 5   (not ADD)
let y = 2 ** 8       # → OP_PUSH_CONST 256.0 (not POW)
let z = 10 == 10     # → OP_PUSH_TRUE         (not EQ)
```

All arithmetic, bitwise, and comparison operators are folded. Division
by zero is left to the runtime for a proper error message.

### Short-Circuit Evaluation

`&&` and `||` use peek-based jump opcodes to skip the right operand:

```
OP_JUMP_IF_FALSE_PEEK  offset   # skip right if left is falsy
OP_POP                          # pop left before evaluating right
```

The value is preserved on the stack until the jump resolves.

---

## Call Frame

Each function invocation pushes a `CallFrame` on `vm->frames[]`:

```c
typedef struct CallFrame {
    Chunk        *chunk;
    int           ip;            /* instruction pointer (byte index) */
    int           stack_base;    /* stack depth at function entry */
    Env          *env;           /* current variable environment */
    Env          *root_env;      /* function's root env (for cleanup) */
    int           owns_env;      /* whether to free env on return */
    Value        *locals[256];   /* local variable flat array */
    int           local_count;   /* number of live locals */
    const char   *local_names[256];  /* debug: local name at each slot */
} CallFrame;
```

Maximum frames: `512`. Maximum locals per frame: `256`.

---

## Garbage Collector

Prism uses **reference counting with cycle collection** (`src/gc.c`).

### Reference Counting
Every `Value` has a `refcount` field. `value_retain(v)` increments it;
`value_release(v)` decrements and frees when it reaches zero.

### Cycle Collection
Cyclic references (arrays containing themselves, closures, etc.) are
handled by a periodic mark-sweep pass that traces all reachable values
from GC roots (globals + VM stack) and frees unreachable objects.

### Generational Hints
Values are tagged with allocation age. Young objects that survive a GC
cycle are promoted to the old generation and collected less frequently.

### Intern Table
Short strings (≤ 128 chars) are **interned** — identical strings share
one `Value` allocation. The intern table is a 512-slot open-address
hash map. Interned strings are immortal (never freed).

### Allocation Tracking
Each allocation is recorded with its source file and line number
(`gc_set_alloc_site`). The `GC_ALLOC_SITE_CAP = 4096` slot table is
queried by the profiler and the leak reporter.

---

## JIT Hot-Loop Detection

The VM counts backward jumps. When a loop header's back-edge count
exceeds `JIT_HOT_THRESHOLD`, a `JitTrace` is compiled for that loop.
The trace is a simplified intermediate representation (IR) that is
re-executed directly on subsequent iterations, bypassing the bytecode
switch-dispatch.

Traces are invalidated (guard exits) when types change dynamically.
The exit count is tracked in `vm->jit->guard_exits`.

Enable JIT with `--jit` flag; use `--jit-verbose` for IR dumps.

---

## Disassembly

Use `--disasm` to print bytecode before executing:

```bash
./prism --disasm examples/fibonacci.pr
```

Output:
```
=== chunk @ examples/fibonacci.pr ===
0000 MAKE_FUNCTION    0   (<fn fib>)
0003 DEFINE_NAME      1   (fib)
0006 LOAD_NAME        1   (fib)
0009 PUSH_INT_IMM     35
0012 CALL             1
0015 POP
0016 HALT
```

---

*See also: [language_guide.md](language_guide.md), [stdlib_reference.md](stdlib_reference.md)*
