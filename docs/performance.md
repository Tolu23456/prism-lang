# Prism Performance Guide

This document describes the performance architecture of the Prism runtime and how to write fast Prism code.

## VM Architecture

### Computed-Goto Dispatch

Prism uses **computed-goto (threaded-code) dispatch** on GCC/Clang, replacing the traditional `switch`/`break` loop. This eliminates the branch misprediction penalty at the central dispatch point and allows the CPU's branch predictor to learn each individual opcode transition.

```
Traditional:    FETCH → switch(op) { case OP_ADD: ... break; ... }
Computed-goto:  FETCH → goto *dispatch_table[op];
```

**Speedup:** Measured 15–30% improvement on integer-heavy microbenchmarks.

**How it works:**

```c
#ifdef __GNUC__
    static void *s_dt[] = {
        &&lbl_OP_HALT, &&lbl_OP_ADD, &&lbl_OP_PUSH_INT_IMM, ...
    };
    #define DISPATCH() goto *s_dt[chunk->code[frame->ip++]]
#else
    #define DISPATCH() goto dispatch_top   /* fallback switch */
#endif
```

Each opcode handler ends with `DISPATCH()` instead of `break`, dispatching directly to the next handler without going through a central `switch`.

### Specialized Opcodes

Prism compiles common patterns to specialized fast-path opcodes:

| Pattern | Compiled to | Benefit |
|---------|-------------|---------|
| Small integer (-32768..32767) | `OP_PUSH_INT_IMM` | No constant pool lookup |
| `a + b` where both int | `OP_ADD_INT` | No type check |
| `a < b` where both int | `OP_LT_INT` | No type check |
| `i += 1` on local | `OP_INC_LOCAL` | Inline increment |
| `i -= 1` on local | `OP_DEC_LOCAL` | Inline decrement |
| Local var access | `OP_LOAD_LOCAL` / `OP_STORE_LOCAL` | O(1) array slot, no hash |

### Stack-Buffer Optimization for Calls

`OP_CALL` and `OP_CALL_METHOD` avoid `malloc` for ≤16 arguments by using a C stack-allocated buffer:

```c
Value *_arg_buf[VM_CALL_STACK_BUF];  /* 16 slots on C stack */
Value **args = (argc <= VM_CALL_STACK_BUF) ? _arg_buf : malloc(...);
```

This eliminates one `malloc`/`free` pair per function call in the common case.

### Inline Caches

`OP_GET_ATTR`, `OP_SET_ATTR`, and `OP_CALL_METHOD` use **monomorphic inline caches**: after the first dispatch, the receiver type and resolved slot are cached in the bytecode stream. If the next call has the same receiver type, the lookup is skipped entirely.

## Compiler Optimizations

### Constant Folding

Arithmetic on integer and float literals is folded at compile time:

```prism
let x = 2 + 3       # compiled as: PUSH_INT_IMM 5
let y = 3.14 * 2    # compiled as: PUSH_CONST 6.28
let z = 100 // 7    # compiled as: PUSH_INT_IMM 14
```

Folded operations: `+`, `-`, `*`, `/`, `//`, `%`, `**`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&`, `|`, `^`, `<<`, `>>`

Unary folding: `-literal`, `not literal`, `~literal`

String folding: `"hello" + " " + "world"` → `PUSH_CONST "hello world"`

### Variable Classification

The compiler classifies variables into:
1. **Locals** (function-level): accessed via `OP_LOAD_LOCAL` / `OP_STORE_LOCAL` using a fixed slot index — O(1), no hash lookup.
2. **Globals/Env**: accessed via `OP_LOAD_NAME` / `OP_STORE_NAME` — hash table lookup.

### Dead Code Elimination

Branches on constant conditions (`if true`, `if false`) and unreachable code after `return` at top level are eliminated.

## Garbage Collector

### Generational Collection

Prism uses a **generational GC** with:
- **Young generation**: newly allocated objects (collected frequently with low cost).
- **Old generation**: objects that survive N minor GCs (collected infrequently).

Minor collections only scan young objects, which are typically 95%+ dead. This keeps pause times short.

### Write Barriers

When an old-generation object is mutated to point to a young-generation object, a **write barrier** records this in the remembered set. Minor GCs scan the remembered set to find young objects reachable from old objects.

### Adaptive Tuning

The GC monitors the survival rate (fraction of young objects surviving each minor GC) and adjusts collection frequency automatically:
- Low survival rate → collect more aggressively (objects die young).
- High survival rate → collect less often (objects are long-lived).

### String Interning

Short strings (≤128 chars) are interned — identical strings share one `Value*`. This benefits:
- Dict key lookups (pointer equality fast-path).
- Memory usage for repeated strings.
- String comparison performance.

## Writing Fast Prism Code

### Prefer local variables over global

```prism
# Slow: global lookup on every iteration
let global_total = 0
func sum(arr) {
    for x in arr { global_total += x }  # slow: global write each iteration
}

# Fast: accumulate locally
func sum(arr) {
    let total = 0
    for x in arr { total += x }   # local slot, O(1)
    return total
}
```

### Avoid string concatenation in loops

```prism
# Slow: O(n²) due to string copies
let result = ""
for x in arr { result = result + str(x) + "," }

# Fast: collect then join
let parts = []
for x in arr { push(parts, str(x)) }
let result = join(",", parts)
```

### Use integer arithmetic where possible

Prism emits `OP_ADD_INT` when both operands are known-int typed at compile time. The specialized opcodes skip type checking, providing significant speedup in numeric loops.

### Avoid creating temporaries in hot loops

```prism
# Slow: creates a new array on each iteration
while condition {
    let tmp = [a, b, c]   # heap allocation each time
    process(tmp)
}

# Fast: reuse
let tmp = [0, 0, 0]
while condition {
    tmp[0] = a; tmp[1] = b; tmp[2] = c
    process(tmp)
}
```

### Profile with gc_stats()

```prism
let before = gc_stats()
my_code()
let after = gc_stats()
print("allocations:", after["total_allocations"] - before["total_allocations"])
```

## Benchmarks

Run the included benchmarks to measure performance:

```bash
./prism benchmarks/bench_vm_dispatch.pr
./prism benchmarks/bench_gc.pr
./prism benchmarks/bench_closures.pr
./prism benchmarks/bench_fibonacci.pr
./prism benchmarks/sieve.pr
```

## Environment Variables

| Variable | Effect |
|----------|--------|
| `PRISM_GC_LOG=1` | Enable GC event logging |
| `PRISM_GC_STRESS=1` | Collect on every allocation (stress test) |
| `PRISM_GC_STATS=1` | Print GC stats on exit |
| `PRISM_GC_POLICY=throughput` | Maximize throughput (fewer, larger collections) |
| `PRISM_GC_POLICY=low_latency` | Minimize pause times |
| `PRISM_MEM_REPORT=1` | Print top allocation sites on exit |
