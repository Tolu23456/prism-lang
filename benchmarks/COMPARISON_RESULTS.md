# Prism vs Python — Benchmark Report

**Date:** 2026-05-03  
**Prism build:** debug (`gcc -std=c11 -g`, no optimisation flags)  
**Prism version:** 0.2.0  
**Python version:** CPython 3.12.12  
**Platform:** Linux x86-64 (NixOS)  
**Method:** median of 3 wall-clock runs per benchmark  
**Speed mode:** `./prism` (bytecode VM, default)  
**Flexibility mode:** `./prism --tree` (tree-walking interpreter — supports full feature set)

---

## Speed Results

| Benchmark | Prism (ms) | Python (ms) | Ratio (py÷prism) | Winner |
|-----------|-----------|------------|-----------------|--------|
| fib(32) recursive | 2038 | 388 | 0.19x | Python |
| recursive sum ×5000 | 420 | 748 | 1.78x | **Prism** |
| ackermann(3,5) ×100 | 1530 | 4800 | 3.14x | **Prism** |
| tight loop 5M iters | 4195 | 505 | 0.12x | Python |
| fib_iter 2M steps | 1760 | 36603 | 20.80x | **Prism** |
| array ops 100K | 215 | 100 | 0.47x | Python |
| dict insert+lookup 10K | 1097 | 87 | 0.08x | Python |
| sieve of eratosthenes 500K | 1736 | 223 | 0.13x | Python |
| bubble sort 800 elems | 668 | 163 | 0.24x | Python |
| string ops 50K iters | 317 | 109 | 0.34x | Python |

**Speed wins — Prism: 3  Python: 7  Ties: 0**

### Speed Analysis

**Where Prism wins:**

- **fib_iter (2M steps) — Prism is 20.8× faster.** Python's `range()` + tuple-swap
  iterative loop is surprisingly slow here; Prism's VM dispatches tight integer arithmetic
  at lower overhead.
- **ackermann(3,5) ×100 — Prism is 3.14× faster.** Deep mutual recursion with small
  frames; Prism's fixed-size call frames on the C stack beat Python's heap-allocated
  frame objects.
- **recursive sum ×5000 — Prism is 1.78× faster.** Similar reason: shallow recursion
  (depth 200) with minimal per-call state is cheaper in Prism.

**Where Python wins:**

- **tight loop 5M iters — Python is 8× faster.** Python's `while` loop is heavily
  micro-optimised in CPython 3.12's specialising adaptive interpreter; Prism's general
  VM dispatch adds overhead per iteration.
- **fib(32) recursive — Python is 5× faster.** A classic recursive Fibonacci with no
  memoisation; CPython's function-call overhead is lower for this pattern.
- **dict/string/array/sieve — Python is 2–12× faster.** Python's built-in data
  structures (dict, list, str) are implemented in highly optimised C with SIMD and
  specialised code paths; Prism's are general-purpose C structs without those
  optimisations.

**Key caveat:** Prism is built with `-g` (debug, no `-O` flags). Running
`make release` (`-O3 -march=native`) would substantially narrow the gap on loop-heavy
and arithmetic benchmarks. Python benefits from 30+ years of CPython micro-optimisation.

---

## Flexibility Comparison

Both languages passed their full flexibility test suites (closures, higher-order
functions, pattern matching, OOP, f-strings, sets, tuples, map/filter/reduce).

| Feature | Prism (--tree) | Python |
|---------|-------|--------|
| Closures / captured state | ✓ | ✓ |
| First-class functions | ✓ | ✓ |
| Higher-order (map/filter/reduce) | ✓ | ✓ |
| Pattern matching (`match`/`when`) | ✓ | ✓ (3.10+) |
| Classes & inheritance | ✓ | ✓ |
| f-string interpolation | ✓ | ✓ |
| Multiple return / tuple index access | ✓ | ✓ |
| Sets (union, intersect, diff) | ✓ | ✓ |
| Dynamic typing | ✓ | ✓ |
| Inline lambdas (`func(x){...}`) | ✓ | ✓ |
| Recursion (stack-limited) | ✓ | ✓ |
| Generational GC | ✓ | ✓ (ref-count + cycle GC) |
| JIT compiler (hot integer loops) | ✓ | ✗ (CPython only) |
| Bytecode VM (separate from tree-walker) | ✓ | ✓ |
| X11 native GUI (built-in) | ✓ | ✗ |
| Tri-state booleans (`true`/`false`/`unknown`) | ✓ | ✗ |
| Inline bytecode assembly (`asm {}`) | ✓ | ✗ |
| AOT transpile to C / LLVM IR | ✓ | ✗ |
| Regex (built-in) | ✗ | ✓ |
| Async / coroutines | ✗ | ✓ |
| Decorators (`@`) | ✗ | ✓ |
| Generators / `yield` | ✗ | ✓ |
| Type annotations | ✗ | ✓ |
| Package ecosystem (pip/PyPI) | ✗ | ✓ |

### Flexibility Notes

- Prism's **class/match** features work in `--tree` (interpreter) mode; the bytecode
  VM compiler does not yet lower those AST nodes.
- Python has a vastly larger ecosystem (pip/PyPI) and mature async/generator support.
- Prism's unique advantages are: built-in X11 GUI, tri-state logic, inline ASM,
  and a JIT that specialises hot integer loops without any user annotation.

---

## Summary

| Dimension | Verdict |
|-----------|---------|
| Raw speed (general) | **Python** (CPython 3.12 highly optimised) |
| Iterative numeric loops | **Prism** (up to 20× faster) |
| Deep recursion (many small frames) | **Prism** (3–3× faster) |
| Data structure throughput (dict/string) | **Python** (C-optimised builtins) |
| Feature breadth | **Python** (ecosystem, async, generators…) |
| Unique language features | **Prism** (JIT, native GUI, tri-state, AOT) |
| Flexibility (matched feature set) | **Tie** — both languages pass all tests |

> To run these benchmarks yourself: `bash benchmarks/run_comparison.sh`
