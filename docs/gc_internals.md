# GC Internals

This document describes the internal implementation of Prism's garbage collector.

## Overview

Prism uses a **tri-color mark-and-sweep** GC with **generational extensions**. The GC is non-moving (objects stay at their original address), making it C-interop safe.

## Object Tracking

Every `Value*` allocated via `gc_track_value()` is added to a singly-linked list headed by `g_gc.objects`. The `gc_prev` / `gc_next` fields on each `Value` form this list.

```
g_gc.objects → [V1] → [V2] → [V3] → ... → NULL
```

During sweep, unmarked objects are removed from the list and freed.

## Generational Design

### Generations

- **Young (gen=0)**: Newly allocated objects. Collected frequently.
- **Old (gen=1)**: Survived ≥1 minor GC. Collected infrequently.

### Minor Collection

1. Mark all roots (globals, VM stack, call frames).
2. During marking, skip old objects that have no write-barrier entries (they're assumed to have only old-to-old references unless the remembered set says otherwise).
3. Sweep: free any young objects not marked. Promote survivors (young → old).
4. Clear young marks.

**Cost:** Proportional to the number of young objects, not total live set.

### Major Collection

1. Mark all roots.
2. Mark full transitive closure (young + old).
3. Sweep all unmarked objects regardless of generation.
4. Reset all marks.

**Frequency:** Every N minor GCs (default: N=8, adaptive).

### Write Barriers

When `env_set()` modifies a slot in an old-generation environment to point to a young-generation value, a write barrier fires:

```c
void gc_write_barrier(PrismGC *gc, Value *old_container, Value *new_val) {
    if (old_container->gc_gen == GC_GEN_OLD
        && new_val && new_val->gc_gen == GC_GEN_YOUNG) {
        /* Record in remembered set */
        gc_remember(gc, old_container);
    }
}
```

The remembered set is a fixed-size hash set of old objects that may point to young objects. Minor GC scans these entries as additional roots.

## Mark Phase

`gc_mark_value(gc, v)` performs recursive marking with cycle protection:

```c
void gc_mark_value(PrismGC *gc, Value *v) {
    if (!v || v->gc_marked) return;  /* already marked or NULL */
    v->gc_marked = 1;
    switch (v->type) {
    case VAL_ARRAY:
        for (int i = 0; i < v->array.len; i++)
            gc_mark_value(gc, v->array.items[i]);
        break;
    case VAL_DICT:
        for each entry: gc_mark_value(gc, key); gc_mark_value(gc, val);
        break;
    ...
    }
}
```

**Roots marked:**
- All variables in global `Env` chain.
- All values on the VM evaluation stack.
- All values in call frame local slots.
- All constants in every `Chunk` in all call frames.
- Temporary root stack (`gc_push_root` / `gc_pop_root`).

## Sweep Phase

```c
Value *prev = NULL;
Value *curr = gc->objects;
while (curr) {
    Value *next = curr->gc_next;
    if (!curr->gc_marked && !curr->immortal) {
        /* Unlink and free */
        if (prev) prev->gc_next = next;
        else gc->objects = next;
        value_free_internal(curr);
        gc->stats.bytes_freed += ...;
    } else {
        curr->gc_marked = 0;   /* reset for next cycle */
        /* Generation promotion */
        if (curr->gc_gen == GC_GEN_YOUNG) curr->gc_gen = GC_GEN_OLD;
        prev = curr;
    }
    curr = next;
}
```

## String Interning

Short strings (≤128 bytes) are interned on first creation:

```
"hello" → fnv1a hash → bucket slot → InternBucket* → Value* (immortal)
```

- Interned `Value*` are immortal (never swept).
- Dictionary key lookups can use pointer equality when both keys were interned.
- `gc_intern_cstr()` returns a canonical `const char*` pointer for fast strcmp-free equality.

**Statistics:** `gc.stats.intern_count`, `gc.stats.intern_bytes_saved`.

## Small-Integer Cache

Integers from -5 to 255 are pre-allocated as immortal singletons:

```c
static Value g_small_ints[GC_SMALL_INT_MAX - GC_SMALL_INT_MIN + 1];
```

`value_int(n)` for n in [-5, 255] returns a pointer into this cache array (immortal, refcount never drops to zero). This eliminates the most common source of heap allocations in numeric code.

## Allocation Site Tracking

When `PRISM_MEM_REPORT=1`:
- The interpreter calls `gc_set_alloc_site(file, line)` before evaluating each AST node.
- `gc_track_value()` records the current site in an open-address hash table.
- On shutdown, `gc_print_mem_report()` prints the top-N allocation hotspots.

This helps identify which Prism source lines create the most GC pressure.

## Adaptive Policy

The `GC_POLICY_ADAPTIVE` policy monitors survival rate using an exponential moving average:

```
ema = alpha * (survived / young_count) + (1 - alpha) * ema
```

- If EMA > 0.5 (many objects survive), increase `major_interval` (collect less often).
- If EMA < 0.2 (most objects die), decrease `major_interval` (collect more often).
- `next_collection` threshold adjusts to match observed allocation rate.

## Policies

| Policy | Description | Use case |
|--------|-------------|----------|
| `balanced` | Default: balance throughput and latency | General purpose |
| `throughput` | Large batches, few pauses | Batch processing |
| `low_latency` | Frequent small collections | GUI / interactive |
| `adaptive` | Auto-tune based on survival rates | Unknown workload |
| `stress` | Collect on every allocation | Debugging memory bugs |
| `debug` | Like stress, with extra logging | Debugging |

## API Reference

```c
/* Lifecycle */
PrismGC *gc_global(void);
void gc_init(PrismGC *gc);
void gc_shutdown(PrismGC *gc);

/* Collections */
size_t gc_collect_minor(PrismGC *gc, Env*, VM*, Chunk*);
size_t gc_collect_major(PrismGC *gc, Env*, VM*, Chunk*);
size_t gc_collect_sweep(PrismGC *gc, Env*, VM*, Chunk*);  /* compat */

/* Root protection */
void gc_push_root(PrismGC *gc, Value *v);
void gc_pop_root(PrismGC *gc);

/* Interning */
Value      *gc_intern_string(PrismGC *gc, const char *s);
const char *gc_intern_cstr(PrismGC *gc, const char *s);

/* Stats */
void   gc_print_stats(PrismGC *gc);
Value *gc_stats_dict(PrismGC *gc);     /* returns dict accessible from Prism */
```
