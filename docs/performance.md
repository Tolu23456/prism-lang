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

### Tagged Integer Values

All Prism `Value`s are represented as a single `uintptr_t` word using pointer tagging:

| Tag bits | Type |
|----------|------|
| `bit 0 = 1` | Integer: value = `word >> 1` |
| `0x02` | `null` |
| `0x06` | `false` |
| `0x0A` | `true` |
| `bits 1:0 = 00` | Heap pointer (string, array, dict, …) |

Tagged integers require **no heap allocation** and no reference counting — arithmetic on integers is purely in registers.

### Specialized Opcodes

Prism compiles common patterns to specialized fast-path opcodes:

| Pattern | Compiled to | Benefit |
|---------|-------------|---------|
| Small integer (-32768..32767) | `OP_PUSH_INT_IMM` | No constant pool lookup |
| `a + b` where both int | `OP_ADD_INT` | No type check, tagged-int trick |
| `a - b` where both int | `OP_SUB_INT` | No type check |
| `a * b` where both int | `OP_MUL_INT` | No type check |
| `a < b` where both int | `OP_LT_INT` | Direct tagged comparison |
| `i += 1` on local | `OP_INC_LOCAL` | Single instruction: `v += 2` |
| `i -= 1` on local | `OP_DEC_LOCAL` | Single instruction: `v -= 2` |
| Local var access | `OP_LOAD_LOCAL` / `OP_STORE_LOCAL` | O(1) array slot, no hash |

**Tagged-int arithmetic tricks** (no untagging needed for add/sub):
```
a = (n<<1)|1,  b = (m<<1)|1
ADD_INT:  a + b - 1  = ((n+m)<<1)|1   ✓ no untag
SUB_INT:  a - b + 1  = ((n-m)<<1)|1   ✓ no untag
INC_LOCAL: v + 2     = ((n+1)<<1)|1   ✓ no untag
LT_INT:   (intptr_t)a < (intptr_t)b   ✓ direct comparison
```

### JIT Compiler

Prism includes a trace-based JIT that compiles hot loops to native x86-64 or AArch64 machine code.

**How it works:**
1. Every backward jump (`OP_JUMP` with negative offset) increments a hot-counter.
2. When a loop back-edge reaches `JIT_HOT_THRESHOLD` (default: 10), the JIT records the trace IR.
3. The IR is compiled to native code (x86-64 or AArch64).
4. Subsequent iterations run the native code directly, bypassing the interpreter.

**Supported IR operations:**
- Integer add, sub, mul, div, mod, neg
- Float add, sub, mul, div
- Comparisons: `<`, `<=`, `>`, `>=`, `==`, `!=`
- Load/store: named variables (env lookup) and local slots
- `EXIT_IF_FALSE`: exits the JIT loop on condition failure
- `LOOP_BACK`: writes all live variables back and branches to loop head

**JIT variable mapping:** At loop entry, the JIT loads all tracked integer variables from `frame->locals[]` into a `long long regs[]` array (untagged). At exit, it writes them back as `value_int(regs[i])`.

**Guard mechanism:** If any variable is not an integer at loop entry, the JIT falls back to the interpreter for that iteration.

**Specialized INT opcodes in JIT:** `OP_ADD_INT`, `OP_SUB_INT`, `OP_MUL_INT`, `OP_DIV_INT`, `OP_MOD_INT`, `OP_NEG_INT`, `OP_LT_INT`, `OP_LE_INT`, `OP_GT_INT`, `OP_GE_INT`, `OP_EQ_INT`, `OP_NE_INT` are all handled by the trace recorder — loops using these specialized opcodes JIT-compile correctly.

**Typical JIT speedup:** 3–10× for tight integer loops.

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

## Build Flags

### Default build (`make`)

```
-O3 -fno-gcse -march=native -fomit-frame-pointer -finline-functions
```

`vm.c` specifically uses:
```
-O3 -fno-optimize-sibling-calls -fstack-reuse=none
```
(prevents GCC from misoptimising the computed-goto dispatch loop)

### LTO build (`make lto-simple`)

```bash
make lto-simple
```

Link-Time Optimisation allows GCC to inline `value_retain`, `value_release`, `env_get`, and other frequently-called functions directly into the VM dispatch loop across translation-unit boundaries. Typical additional speedup: **10–25%** on recursive workloads.

### PGO build (`make pgo-train && make pgo-use`)

Profile-Guided Optimisation trains on real workloads then re-compiles with branch prediction hints, inlining decisions guided by actual call frequencies, and better register allocation. Typical additional speedup: **5–20%**.

```bash
make pgo-gen                  # Build instrumented binary
make pgo-train                # Run training workload automatically
make pgo-use                  # Build PGO-optimised binary → prism-pgo-opt
```

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

Prism emits `OP_ADD_INT` when both operands are known-int typed at compile time. The specialized opcodes skip type checking and the JIT handles them natively, providing significant speedup in numeric loops.

### Leverage the JIT for tight loops

The JIT fires after 10 backward-jump iterations. Loops that:
- Use only integer local variables
- Contain arithmetic and comparisons
- Have a simple exit condition

...will JIT-compile to native code and run at near-C speed.

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
make bench                          # Quick benchmark suite
./prism --vm benchmarks/fib_recursive.pr
./prism --vm benchmarks/loop_count.pr
./prism --vm benchmarks/ackermann.pr
./prism --vm benchmarks/sieve.pr
./prism benchmarks/bench_vm_dispatch.pr
./prism benchmarks/bench_gc.pr
./prism benchmarks/bench_closures.pr
./prism benchmarks/bench_fibonacci.pr
```

See `benchmarks/RESULTS.md` for latest results.

## Environment Variables

| Variable | Effect |
|----------|--------|
| `PRISM_GC_LOG=1` | Enable GC event logging |
| `PRISM_GC_STRESS=1` | Collect on every allocation (stress test) |
| `PRISM_GC_STATS=1` | Print GC stats on exit |
| `PRISM_GC_POLICY=throughput` | Maximize throughput (fewer, larger collections) |
| `PRISM_GC_POLICY=low_latency` | Minimize pause times |
| `PRISM_MEM_REPORT=1` | Print top allocation sites on exit |
