# Prism Benchmark Results

**Interpreter:** Prism v0.1.0 (stack-based bytecode VM + trace JIT compiler)  
**Build:** `gcc -std=c11 -O2 -fno-gcse -DNDEBUG` (optimised release build)  
**Platform:** Linux (NixOS), x86-64  
**Date:** 2026-05-05  
**Command:** `./prism <file>` — wall-clock time measured with `time`

---

## Results

| # | Benchmark | File | Workload | Result | Time (real) | vs Baseline |
|---|-----------|------|----------|--------|-------------|-------------|
| 1 | **Fibonacci — recursive** | `fib_recursive.pr` | `fib(32)` — ~7.9 M recursive calls | 2,178,309 | **0.527 s** | 3.4× faster |
| 2 | **Fibonacci — iterative** | `fib_iterative.pr` | 2,000,000 loop iterations | (large int) | **0.493 s** | — |
| 3 | **Tight loop counter** | `loop_count.pr` | 5,000,000 `while` iterations | 5,000,000 | **0.260 s** | 3.6× faster |
| 4 | **Sieve of Eratosthenes** | `sieve.pr` | Primes ≤ 500,000 | 41,538 primes | **0.244 s** | 2.2× faster |
| 5 | **Bubble sort** | `bubble_sort.pr` | 800 elements, worst-case (reversed) | sorted 1…800 | **0.155 s** | 1.6× faster |
| 6 | **Dictionary operations** | `dict_ops.pr` | 10,000 inserts + 10,000 lookups | 99,990,000 | **0.924 s** | ~1× (hash-table bound) |
| 7 | **String operations** | `string_ops.pr` | 50,000 × (join + split + upper + startswith) | 50,000 | **0.16 s** | ~1× (alloc-bound) |
| 8 | **Recursive sum** | `recursive_sum.pr` | `rsum(200)` × 5,000 — 1 M recursive calls | 100,500,000 | **0.104 s** | 3.7× faster |
| 9 | **Ackermann** | `ackermann.pr` | `ack(3,5)` × 100 — ~1.03 M recursive calls | 25,300 | **0.376 s** | 3.3× faster |
| 10 | **Array operations** | `array_ops.pr` | 100,000 `add` + 100,000 index reads + slice | 4,999,950,000 | **0.049 s** | 2.2× faster |

---

## Speedup vs original baseline

| Benchmark | Baseline | Optimised | Speedup |
|-----------|----------|-----------|---------|
| Fibonacci (recursive) | 1.77 s | **0.527 s** | **3.4×** |
| Loop counter          | 0.93 s | **0.260 s** | **3.6×** |
| Ackermann             | 1.23 s | **0.376 s** | **3.3×** |
| Recursive sum         | 0.39 s | **0.104 s** | **3.7×** |
| Sieve                 | 0.54 s | **0.244 s** | **2.2×** |
| Array ops             | 0.11 s | **0.049 s** | **2.2×** |
| Bubble sort           | 0.25 s | **0.155 s** | **1.6×** |
| Dict ops              | 0.94 s |  0.924 s   | **~1×** (hash-table bound) |

---

## Throughput summary (post-optimisation)

| Category | Metric |
|----------|--------|
| Loop iterations / second | ~19.2 M / s |
| Fibonacci calls / second (recursive) | ~15.0 M calls / s |
| Recursive function calls / second (Ackermann) | ~2.7 M calls / s |
| Array reads / second | ~2.04 M / s |
| Dict lookups / second | ~10.6 K / s |

---

## Optimisation techniques applied

### Session 1 (previous)
1. **Computed-goto dispatch** (`&&label` GCC extension): 15–25% vs `switch` dispatch.
2. **GC removed from hot DISPATCH macro**: GC now triggered lazily from allocation sites only.
3. **OP_CALL single-pass param binding**: eliminated redundant `env_get` per parameter.
4. **Env slab pool** (512-entry): `env_new`/`env_free` O(1) for standard-capacity envs.
5. **Skip OP_PUSH_SCOPE for empty blocks**: compiler skips `malloc`/`free` for blocks without declarations.
6. **`chunk->no_env` flag**: functions with only locals skip `env_new()` entirely on call.
7. **Inline name cache** for `OP_LOAD_NAME`/`OP_STORE_NAME`: `(name_env, name_slot)` cached after first lookup.
8. **Interned name constants**: pointer equality before `strcmp`.

### Session 2 (previous)
9. **Frame size reduction** (`VM_LOCALS_MAX` 256→64, removed `local_names[256]`):
   - `CallFrame` shrunk from ~4.1 KB to ~0.55 KB.
   - Total `vm->frames[2048]` shrunk from **8.4 MB → 1.1 MB** — fits in L3 cache for recursive workloads.
10. **Minimal `memset` per call** (`chunk->local_count_max`):
    - Compiler now stores the maximum local slot used in each function chunk.
    - `OP_CALL` zeros only slots `[param_count, local_count_max)` instead of all 256.
    - For `fib(n)` (1 param, 0 extra locals): **zero bytes** memset per call.
    - For `ackermann(m,n)` (2 params, 0 extra locals): **zero bytes** memset per call.
11. **JIT: `OP_PUSH_INT_IMM` support**: small integer literals (e.g. `1`) no longer abort the JIT trace recorder.
    - `loop_count.pr` previously had `i += 1` → `OP_PUSH_INT_IMM 1` → **JIT abort**. Now JITs at native speed.
12. **JIT: frame-local variable support** (`OP_LOAD_LOCAL`, `OP_STORE_LOCAL`, `OP_INC_LOCAL`, `OP_DEC_LOCAL`):
    - Loops inside functions now JIT with direct `frame->locals[slot]` access, bypassing hash-table lookups.
    - `JitTrace.var_local_slots[]` maps JIT register slots to either `locals[i]` (frame) or `env_get(name)` (global).
13. **vm.c at `-O2`** (`-fno-optimize-sibling-calls -fstack-reuse=none`):
    - Upgraded from `-O1` to `-O2` with targeted flags disabling the two passes that previously caused dispatch-loop miscompilation.
14. **JIT hot threshold lowered** (200→50): traces compile sooner, amortised compilation cost drops.

### Session 3 (this session)
15. **Compiler `-O3`** for all non-vm files (was `-O2`):
    - Added `-finline-functions` for all translation units.
    - `vm.c` kept at `-O2 -fno-optimize-sibling-calls -fstack-reuse=none` to avoid dispatch-loop miscompilation.
    - NixOS sandbox suppresses `-march=native`, so CPU-specific micro-tuning is not available in this environment.
16. **JIT: 12 specialised INT opcodes** added to trace recorder:
    - `OP_ADD_INT`, `OP_SUB_INT`, `OP_MUL_INT`, `OP_DIV_INT`, `OP_MOD_INT`, `OP_NEG_INT`,
      `OP_LT_INT`, `OP_LE_INT`, `OP_GT_INT`, `OP_GE_INT`, `OP_EQ_INT`, `OP_NE_INT`.
    - Previously any loop using these specialised opcodes caused an immediate **JIT trace abort**;
      now they are recorded and emit native x86-64 arithmetic/compare instructions.
17. **JIT hot threshold lowered** (50→10): traces compile after only 10 loop-back edges (was 50).
18. **Makefile** new targets: `lto-simple` (whole-program LTO), `pgo-gen/pgo-train/pgo-use` (PGO pipeline), `bench` (sequential benchmark runner).
19. **Test suite** expanded: `test_match.pr` (22 assertions), `test_error_handling.pr` — all pass.
20. **Examples** added: `primes.pr` (prime sieve + factorisation), `state_machine.pr` (closure-based FSM with traffic-light, door-lock, order-processing, and vending-machine demos), `tree.pr` (closure-based BST with inorder traversal, height, min/max, word-frequency counter).
21. **Docs** updated: `docs/performance.md` rewritten with JIT internals, build-flag guide, and PGO/LTO instructions.
22. **Icon**: `prism.svg` — `.pr` file-type icon with gradient prism glyph.

---

## Notes

### Language constraints
| Issue | Detail |
|-------|--------|
| `arr` is a reserved keyword | Use any other identifier for array variables |
| `{}` creates an empty **set**, not a dict | Seed with `{"_seed": 0}` to get a dict |
| Call-stack depth limit | `VM_FRAME_MAX = 2048`; `VM_LOCALS_MAX = 64` local slots per frame |
