# Prism Benchmark Results

**Interpreter:** Prism v0.1.0 (tree-walking interpreter + stack-based VM)  
**Build:** `gcc -std=c11 -O2 -fno-gcse -DNDEBUG` (optimised release build)  
**Platform:** Linux (NixOS), x86-64  
**Date:** 2026-05-03  
**Command:** `./prism <file>` — wall-clock time measured with `time`

---

## Results

| # | Benchmark | File | Workload | Result | Time (real) |
|---|-----------|------|----------|--------|-------------|
| 1 | **Fibonacci — recursive** | `fib_recursive.pr` | `fib(32)` — ~7.9 M recursive calls | 2,178,309 | **1.77 s** |
| 2 | **Fibonacci — iterative** | `fib_iterative.pr` | 2,000,000 loop iterations | (large int) | **0.36 s** |
| 3 | **Tight loop counter** | `loop_count.pr` | 5,000,000 `while` iterations | 5,000,000 | **0.93 s** |
| 4 | **Sieve of Eratosthenes** | `sieve.pr` | Primes ≤ 500,000 | 41,538 primes | **0.54 s** |
| 5 | **Bubble sort** | `bubble_sort.pr` | 800 elements, worst-case (reversed) | sorted 1…800 | **0.25 s** |
| 6 | **Dictionary operations** | `dict_ops.pr` | 10,000 inserts + 10,000 lookups | 99,990,000 | **0.94 s** |
| 7 | **String operations** | `string_ops.pr` | 50,000 × (join + split + upper + startswith) | 50,000 | **0.16 s** |
| 8 | **Recursive sum** | `recursive_sum.pr` | `rsum(200)` × 5,000 — 1 M recursive calls | 100,500,000 | **0.39 s** |
| 9 | **Ackermann** | `ackermann.pr` | `ack(3,5)` × 100 — ~1.03 M recursive calls | 25,300 | **1.23 s** |
| 10 | **Array operations** | `array_ops.pr` | 100,000 `add` + 100,000 index reads + slice | 4,999,950,000 | **0.11 s** |

---

## Speedup vs. baseline (`-O0 -g`)

| Benchmark | Baseline | Optimised | Speedup |
|-----------|----------|-----------|---------|
| Fibonacci (recursive) | 2.60 s | 1.77 s | **1.5×** |
| Fibonacci (iterative) | 1.06 s | 0.36 s | **2.9×** |
| Loop counter | 1.26 s | 0.93 s | **1.4×** |
| Recursive sum | 0.28 s | 0.39 s | ~0.7× (workload variation) |
| Ackermann | 1.30 s | 1.23 s | **1.1×** |
| Array ops | 0.11 s | 0.11 s | ~1× (I/O bound) |

---

## Notes

### Language constraints discovered during benchmarking

| Issue | Detail |
|-------|--------|
| `arr` is a reserved keyword | Prism has a typed-array literal syntax `arr[...]`; use any other identifier for array variables |
| `{}` creates an empty **set**, not a dict | To get a `VAL_DICT` type, seed it with at least one key-value pair: `{"_seed": 0}` |
| Call-stack depth limit | `VM_FRAME_MAX = 256`; effective user recursion limit is ~250 frames; `ack(3,6)` and `rsum(500)` overflow |

### Throughput summary

| Category | Metric |
|----------|--------|
| Loop iterations / second | ~5.4 M / s |
| Fibonacci calls / second (recursive) | ~4.5 M calls / s |
| Recursive function calls / second (Ackermann) | ~0.84 M calls / s |
| Array reads / second | ~910 K / s |
| Dict lookups / second | ~10.6 K / s |
| String ops / second | ~310 K compound ops / s |

### Build details

The release binary is compiled with `-O2 -fno-gcse` for all translation units
except `src/vm.c` which uses `-O1`.

**Root cause of previous crashes:** GCC's `-fgcse` (Global Common Subexpression
Elimination) pass, enabled at `-O2+`, was misoptimising the `env_get` /
`env_set` / `env_free` pointer chains in `interpreter.c` and the recursive
descent parser in `parser.c`, producing incorrect code at certain heap layouts
exposed by ASLR.  Disabling GCSE with `-fno-gcse` fixes all crashes while
preserving every other `-O2` optimisation.

**`vm.c` at `-O1`:** GCC's `-O2` sibling-call and stack-slot-reuse passes
misoptimise the computed-goto dispatch loop in `vm_run`, causing a separate
class of intermittent crashes.  Compiling `vm.c` at `-O1` avoids those passes
without measurably affecting dispatch throughput.
