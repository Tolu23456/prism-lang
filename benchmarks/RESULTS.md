# Prism Benchmark Results

**Interpreter:** Prism v0.1.0 (tree-walking interpreter + stack-based VM)  
**Build:** `gcc -std=c11 -O0 -g` (debug build, no optimisation flags)  
**Platform:** Linux (NixOS), x86-64  
**Date:** 2026-04-16  
**Command:** `./prism <file>` — wall-clock time measured with `time`

---

## Results

| # | Benchmark | File | Workload | Result | Time (real) |
|---|-----------|------|----------|--------|-------------|
| 1 | **Fibonacci — recursive** | `fib_recursive.pr` | `fib(32)` — ~7.9 M recursive calls | 2,178,309 | **2.60 s** |
| 2 | **Fibonacci — iterative** | `fib_iterative.pr` | 2,000,000 loop iterations | (large int) | **1.06 s** |
| 3 | **Tight loop counter** | `loop_count.pr` | 5,000,000 `while` iterations | 5,000,000 | **1.26 s** |
| 4 | **Sieve of Eratosthenes** | `sieve.pr` | Primes ≤ 500,000 | 41,538 primes | **0.68 s** |
| 5 | **Bubble sort** | `bubble_sort.pr` | 800 elements, worst-case (reversed) | sorted 1…800 | **0.34 s** |
| 6 | **Dictionary operations** | `dict_ops.pr` | 10,000 inserts + 10,000 lookups | 99,990,000 | **0.97 s** |
| 7 | **String operations** | `string_ops.pr` | 50,000 × (join + split + upper + startswith) | 50,000 | **0.19 s** |
| 8 | **Recursive sum** | `recursive_sum.pr` | `rsum(200)` × 5,000 — 1 M recursive calls | 100,500,000 | **0.28 s** |
| 9 | **Ackermann** | `ackermann.pr` | `ack(3,5)` × 100 — ~1.03 M recursive calls | 25,300 | **1.30 s** |
| 10 | **Array operations** | `array_ops.pr` | 100,000 `add` + 100,000 index reads + slice | 4,999,950,000 | **0.11 s** |

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
| Loop iterations / second | ~4.0 M / s |
| Fibonacci calls / second (recursive) | ~3.0 M calls / s |
| Recursive function calls / second (Ackermann) | ~0.8 M calls / s |
| Array reads / second | ~910 K / s |
| Dict lookups / second | ~10 K / s (includes `str()` int→string key conversion) |
| String ops / second | ~265 K compound ops / s |
| Sieve inner-loop steps / second | ~7.3 M array-write steps / s |

### Build note

The binary is compiled without `-O2`/`-O3`. Enabling optimisation flags in the `Makefile`
would significantly reduce these times — primarily by inlining the C interpreter's
`eval_node` dispatch loop and the GC hot paths.
