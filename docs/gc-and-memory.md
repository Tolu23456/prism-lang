# Prism GC & Memory Management

Prism uses a multi-layered memory management system combining reference
counting with a generational mark-sweep garbage collector.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    Value Layer                      │
│  • Reference counting (retain/release)              │
│  • Immortal singletons: null, true, false,          │
│    unknown, integers −5..255                        │
│  • String interning: identical strings share one    │
│    heap allocation (O(1) equality comparison)       │
└─────────────────┬───────────────────────────────────┘
                  │ all Values are tracked by
┌─────────────────▼───────────────────────────────────┐
│              Generational GC (src/gc.c)             │
│                                                     │
│  Young generation  ─►  Old generation               │
│  (new objects)          (survived minors)           │
│                                                     │
│  Minor collect:  young only, fast                   │
│  Major collect:  full heap, cycle-capable           │
│  Adaptive policy: survival-rate EMA tunes trigger   │
└─────────────────────────────────────────────────────┘
```

## Reference Counting

Every `Value*` has a `refcount`. Common patterns:

```c
value_retain(v)    // increment refcount
value_release(v)   // decrement; free when 0
```

Immortal singletons skip ref-counting entirely — they are pre-allocated
at startup and never freed.

## Environment Lifetimes (Closures)

Function call environments (`Env`) use reference counting to keep
closure-captured scopes alive even after the function returns:

```prism
func make_adder(n) {
    return x => x + n    # captures 'n' from make_adder's env
}
let add5 = make_adder(5)  # make_adder's env stays alive (refcount=1)
output(add5(3))            # 8 — 'n' is still accessible
```

The environment chain uses parent-pointer ownership:
- Each child env retains its parent
- Releasing a child decrements the parent's refcount
- The global (root) env is freed exclusively by the interpreter shutdown

## Generational Collection

| Phase | Trigger | Scope |
|-------|---------|-------|
| Minor GC | Allocation threshold | Young generation only |
| Major GC | Every 8 minor GCs (default) | Full heap |
| Final sweep | Interpreter shutdown | All remaining objects |

Major collection reclaims **cycles** — reference cycles between arrays,
dicts, and closures are correctly collected.

## Adaptive Policy

The `GC_POLICY_ADAPTIVE` mode (default) monitors the young-generation
survival rate using an exponential moving average (EMA). If survival
rate is high (few objects dying young), the threshold is raised to
collect less often. If low, the threshold is lowered for more frequent
small collections.

## CLI Controls

```bash
./prism --mem-report prog.pr  # full memory diagnostic report
./prism --gc-sweep prog.pr    # enable manual sweep flag (no-op, now default)
./prism --bench prog.pr       # show VM timing
```

### `--mem-report` Output

```
=== Prism Memory Report ===
Total values:     1,024
Young gen:        512 (512 bytes avg)
Old gen:          256
Interned strings: 128 (FNV-1a table, 0 collisions)
GC collections:   minor=8, major=1
Adaptive EMA:     survival_rate=0.12, threshold=4096

Top allocation sites:
  interpreter.c:2388  array     1024 allocs
  interpreter.c:1234  string     512 allocs
  ...
```

## Memory Module (Prism-level)

```prism
memory.stats()              # print GC stats
memory.collect()            # force a full collection
memory.limit("512mb")       # set heap limit
memory.profile()            # enable per-allocation profiling
```

## String Interning

All identifier strings and string literals used as dict keys are
automatically interned at lex time via FNV-1a hash table. Interned
strings:

- Share a single `Value*` allocation (zero duplicates)
- Compare in O(1) via pointer equality (no `strcmp`)
- Are immortal — they bypass GC and ref-counting

## Immortal Singletons

These values are pre-created at startup and never freed:

| Value | Pool |
|-------|------|
| `null` | single instance |
| `true` | single instance |
| `false` | single instance |
| `unknown` | single instance |
| integers −5 to 255 | pre-allocated array |

`value_null()`, `value_bool(true)`, `value_int(42)` (if 42 is in range)
all return a pointer to the pre-existing immortal value — zero allocation.

## AddressSanitizer Build

```bash
make sanitize
ASAN_OPTIONS=detect_leaks=1 ./prism-san examples/hello.pr
```

This builds with `-fsanitize=address,undefined` and enables leak detection
via LeakSanitizer (Linux only).
