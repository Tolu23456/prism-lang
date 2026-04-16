# Prism AGC Pipeline

## Objective

Design and build an Automatic Garbage Collector (AGC) for Prism that replaces fragile manual runtime value lifetime management with an automatic, cycle-safe, observable, and optimizable memory system.

Prism is written in C, so memory is not automatically managed by the host language. The current runtime uses reference counting through `value_retain()` and `value_release()`. That works for simple ownership, but it becomes harder to maintain as Prism grows toward a bytecode VM, closures, classes, nested data structures, GUI features, modules, and long-running programs.

The goal is not just to free memory automatically. The goal is to make Prism's runtime safer, easier to evolve, and eventually faster under real workloads.

---

## Current Memory Model

Important current files:

- `src/value.h` / `src/value.c` — runtime value definitions, constructors, reference counting, arrays, dicts, sets, tuples, functions, builtins
- `src/ast.h` / `src/ast.c` — AST node allocation and recursive cleanup
- `src/interpreter.c` — environments, variable storage, tree-walking runtime
- `src/vm.c` — stack-based bytecode VM runtime
- `src/chunk.c` / `src/chunk.h` — bytecode and constant storage

Current `Value` objects include:

- null
- int
- float
- complex
- string
- bool
- array
- dict
- set
- tuple
- function
- builtin

Current memory strategy:

```c
Value *value_retain(Value *v);
void value_release(Value *v);
```

Each `Value` currently has:

```c
int ref_count;
```

This means Prism manually tracks how many places own each runtime value.

---

## Why Prism Needs AGC

Reference counting is simple, but it has limits.

### Problems with reference counting

1. It requires retain/release discipline everywhere.
2. It is easy to leak memory if a release is missed.
3. It is easy to crash if a value is released too early.
4. It cannot naturally collect cycles.
5. It becomes noisy inside the VM because stack operations create many temporary values.
6. It makes closures, classes, object graphs, modules, and long-running programs harder to implement safely.

Example cycle that reference counting struggles with:

```prism
let a = []
a.add(a)
```

If `a` is no longer reachable, reference counting may still keep it alive because the array references itself. A tracing garbage collector can detect that the whole cycle is unreachable and free it.

---

## Target Memory Architecture

Prism should use a hybrid memory architecture:

```text
Prism Memory System
│
├── Runtime AGC
│   ├── manages runtime Value objects
│   ├── traces reachable values
│   ├── frees unreachable values
│   ├── handles cycles
│   ├── observes VM stack roots
│   ├── observes interpreter environment roots
│   └── reports useful memory statistics
│
├── AST Arena
│   ├── fast parser/compiler allocations
│   ├── bulk-free after parse/compile/run phase
│   └── avoids per-node GC overhead
│
└── Optional Optimizations
    ├── generational collection
    ├── incremental collection
    ├── string interning
    ├── small value caching
    ├── object pools
    └── allocation profiling
```

The AGC should manage runtime values. The AST should eventually move to an arena allocator because AST memory has a simpler lifetime than runtime values.

---

## AGC v1: Stop-the-World Mark-and-Sweep

The first real collector should be a precise stop-the-world mark-and-sweep collector.

This is the safest first implementation because it is understandable, debuggable, and naturally cycle-safe.

### High-level flow

```text
1. Pause execution at a safe point
2. Mark all root values
3. Recursively mark values reachable from roots
4. Sweep the heap and free unmarked values
5. Resume execution
```

---

## New Files

Add:

```text
src/gc.h
src/gc.c
```

Possible later additions:

```text
src/gc_debug.h
src/gc_debug.c
src/arena.h
src/arena.c
```

---

## Core GC Data Structures

### GC object metadata on `Value`

Replace or supplement `ref_count` with GC metadata:

```c
struct Value {
    ValueType type;

    unsigned char marked;
    struct Value *gc_next;

    union {
        long long int_val;
        double float_val;
        struct { double real; double imag; } complex_val;
        char *str_val;
        int bool_val;
        ValueArray array;
        ValueDict dict;
        ValueArray set;
        ValueArray tuple;

        struct {
            char    *name;
            Param   *params;
            int      param_count;
            ASTNode *body;
            Env     *closure;
        } func;

        struct {
            char      *name;
            BuiltinFn  fn;
        } builtin;
    };
};
```

### GC state

```c
typedef struct GC {
    Value *objects;

    size_t bytes_allocated;
    size_t next_collection;

    int gray_count;
    int gray_capacity;
    Value **gray_stack;

    int debug_log;
    size_t collections_run;
    size_t objects_allocated;
    size_t objects_freed;
} GC;
```

The GC tracks all allocated runtime values through a linked list.

---

## Public GC API

`src/gc.h` should expose functions like:

```c
#ifndef GC_H
#define GC_H

#include <stddef.h>
#include "value.h"

void gc_init(GC *gc);
void gc_free_all(GC *gc);

Value *gc_alloc_value(GC *gc, ValueType type);

void gc_mark_value(GC *gc, Value *value);
void gc_mark_env(GC *gc, Env *env);
void gc_collect(GC *gc);

void gc_push_temp(GC *gc, Value *value);
void gc_pop_temp(GC *gc);

#endif
```

The temporary root API is important because some values may be created during expression evaluation before they are stored in an environment, collection, or VM stack.

---

## Root Sources

The collector must know where live values can start from.

### Interpreter roots

- global environment
- current local environment
- closure environments
- temporary evaluation values
- builtin values stored in globals

### VM roots

- VM stack
- VM globals
- active call frames
- currently executing function
- constant pool values
- temporary values during bytecode operations

### Collection roots

If a root points to a collection, the collector must mark everything inside it:

- array items
- dict keys and values
- set items
- tuple items
- function closure environment

---

## Mark Phase

Mark a single value:

```c
void gc_mark_value(GC *gc, Value *value) {
    if (!value) return;
    if (value->marked) return;

    value->marked = 1;
    gc_push_gray(gc, value);
}
```

Trace references:

```c
void gc_trace_references(GC *gc) {
    while (gc->gray_count > 0) {
        Value *value = gc->gray_stack[--gc->gray_count];
        gc_blacken_value(gc, value);
    }
}
```

Mark child values:

```c
void gc_blacken_value(GC *gc, Value *value) {
    switch (value->type) {
        case VAL_ARRAY:
            for (int i = 0; i < value->array.len; i++) {
                gc_mark_value(gc, value->array.items[i]);
            }
            break;

        case VAL_DICT:
            for (int i = 0; i < value->dict.len; i++) {
                gc_mark_value(gc, value->dict.entries[i].key);
                gc_mark_value(gc, value->dict.entries[i].val);
            }
            break;

        case VAL_SET:
            for (int i = 0; i < value->set.len; i++) {
                gc_mark_value(gc, value->set.items[i]);
            }
            break;

        case VAL_TUPLE:
            for (int i = 0; i < value->tuple.len; i++) {
                gc_mark_value(gc, value->tuple.items[i]);
            }
            break;

        case VAL_FUNCTION:
            gc_mark_env(gc, value->func.closure);
            break;

        default:
            break;
    }
}
```

---

## Sweep Phase

After marking reachable values, sweep frees anything unmarked.

```c
void gc_sweep(GC *gc) {
    Value *previous = NULL;
    Value *object = gc->objects;

    while (object != NULL) {
        if (object->marked) {
            object->marked = 0;
            previous = object;
            object = object->gc_next;
        } else {
            Value *unreached = object;
            object = object->gc_next;

            if (previous != NULL) {
                previous->gc_next = object;
            } else {
                gc->objects = object;
            }

            gc_free_value(gc, unreached);
        }
    }
}
```

Important rule:

`gc_free_value()` should free the object's internal buffers, but it should not recursively release child `Value*` objects.

For example:

```c
case VAL_ARRAY:
    free(value->array.items);
    break;
```

not:

```c
case VAL_ARRAY:
    for (...) value_release(item);
    free(value->array.items);
    break;
```

The sweep phase decides global object lifetime.

---

## Migration Pipeline

### Phase 1 — Scaffold GC files

- Add `src/gc.h`
- Add `src/gc.c`
- Define `GC`
- Implement `gc_init()`
- Implement `gc_free_all()`
- Implement `gc_alloc_value()`
- Do not replace reference counting yet

Acceptance:

- Project builds
- Existing behavior remains unchanged

---

### Phase 2 — Add GC metadata to `Value`

- Add `marked`
- Add `gc_next`
- Keep `ref_count` temporarily for compatibility

Acceptance:

- Existing runtime still works
- No constructor behavior changes yet

---

### Phase 3 — Route value allocation through GC

- Change internal `val_new()` to allocate through the GC
- Add a runtime-owned `GC` pointer to interpreter and VM
- Decide whether constructors receive `GC *gc` directly or use a temporary global current GC during migration

Preferred long-term form:

```c
Value *value_string(GC *gc, const char *s);
```

Temporary lower-risk form:

```c
Value *value_string(const char *s);
```

with an internal current runtime GC.

Acceptance:

- Runtime values are tracked by GC object list
- Existing interpreter and VM still run simple programs

---

### Phase 4 — Mark interpreter roots

- Mark global environment
- Mark current local environment
- Mark function closure environments
- Mark temporary evaluation values where needed

Acceptance:

- Values stored in variables survive collection
- Temporary values do not disappear during expression evaluation

---

### Phase 5 — Mark VM roots

- Mark VM stack
- Mark VM globals
- Mark active call frame values
- Mark constants in bytecode chunks if they are `Value*`

Acceptance:

- VM execution survives forced collections
- Arithmetic, variables, arrays, dicts, strings, and function calls behave correctly

---

### Phase 6 — Convert `value_release()` behavior

During transition:

```c
Value *value_retain(Value *v) {
    return v;
}

void value_release(Value *v) {
    (void)v;
}
```

Then remove retain/release calls gradually where they are no longer useful.

Acceptance:

- No double-free behavior
- GC is responsible for runtime value lifetime

---

### Phase 7 — Add debug and stress modes

Add CLI/runtime flags later:

```bash
./prism --gc-stats file.pm
./prism --gc-log file.pm
./prism --gc-stress file.pm
```

Debug output example:

```text
[gc] begin collection #12
[gc] marked 441 objects
[gc] freed 219 objects
[gc] heap: 88 KB -> 52 KB
```

Acceptance:

- Memory behavior is visible during development
- Stress mode can force collection on frequent allocations

---

### Phase 8 — Add AST arena allocator

AST memory does not need the same lifecycle as runtime values.

Add:

```text
src/arena.h
src/arena.c
```

Use it for parser/compiler allocations:

```text
parse file -> allocate AST nodes in arena -> run/compile -> free whole arena
```

Acceptance:

- AST cleanup becomes simpler
- Parser allocation becomes faster
- Runtime GC does not need to manage AST nodes

---

## Can This Be Made Even Better?

Yes. The mark-and-sweep AGC is the foundation, not the final dream version.

Here is how Prism can make it even better.

### 1. Generational GC

Most objects die young. Prism can split memory into generations:

```text
Young generation — new temporary objects
Old generation   — objects that survived multiple collections
Pinned objects   — native/runtime objects that should not move
```

This improves speed because Prism can collect young objects often without scanning the entire heap every time.

### 2. Incremental GC

Instead of pausing execution for a full collection, Prism can do a little GC work between interpreter/VM steps.

This helps:

- REPL responsiveness
- GUI programs
- long-running scripts
- future servers or event loops

### 3. Precise root maps for VM call frames

The VM can eventually know exactly which stack slots are live values at each bytecode location.

That avoids scanning unnecessary memory and makes the collector more accurate and faster.

### 4. Write barriers

For generational and incremental GC, Prism will need write barriers when old objects point to young objects.

Example:

```c
array->items[i] = new_value;
gc_write_barrier(gc, array, new_value);
```

This lets the collector stay correct without scanning the entire old generation constantly.

### 5. String interning

Prism can store identical strings once:

```prism
"hello"
"hello"
```

Both can point to one interned string object.

Benefits:

- less memory use
- faster equality checks
- faster dictionary keys
- better method/property lookup later

### 6. Small immutable value caching

Cache common values:

- `true`
- `false`
- `unknown`
- `null`
- small integers such as `-1` through `255`

This reduces allocations for common values.

### 7. Allocation profiling

Prism can report memory by type:

```text
strings:   1203 objects, 92 KB
arrays:     155 objects, 44 KB
dicts:       39 objects, 21 KB
functions:   12 objects, 9 KB
```

This would make Prism easier to optimize.

### 8. Heap verification mode

Add an internal checker that validates:

- every object in the heap list is valid
- every collection points to valid `Value*` objects
- no object is freed twice
- no root points outside the managed heap

This is especially useful while building the VM.

### 9. Optional compacting collector later

A compacting collector moves live objects together to reduce fragmentation.

This is powerful but requires updating all pointers, so it should come much later.

A safe path is:

```text
v1 mark-and-sweep
v2 generational non-moving
v3 incremental non-moving
v4 optional compacting young generation
```

### 10. Memory policy modes

Prism can expose runtime memory modes:

```bash
./prism --gc=throughput file.pm
./prism --gc=low-latency file.pm
./prism --gc=debug file.pm
./prism --gc=stress file.pm
```

This lets Prism tune collection behavior for different workloads.

---

## Recommended Final Direction

Build the collector in this order:

```text
1. Stop-the-world mark-and-sweep AGC
2. Debug/stress/statistics tooling
3. AST arena allocator
4. Generational young/old heap
5. Incremental collection
6. String interning and small value caching
7. Precise VM root maps
8. Optional compaction
```

This gives Prism a realistic path from simple correctness to a genuinely advanced memory system.

The first implementation should focus on correctness and visibility. Once it is stable, Prism can become faster and smarter without rewriting the entire runtime again.
