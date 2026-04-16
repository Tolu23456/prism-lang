#ifndef GC_H
#define GC_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

typedef enum {
    GC_POLICY_BALANCED,
    GC_POLICY_THROUGHPUT,
    GC_POLICY_LOW_LATENCY,
    GC_POLICY_DEBUG,
    GC_POLICY_STRESS
} GCPolicy;

typedef struct GCStats {
    size_t bytes_allocated;
    size_t bytes_freed;
    size_t live_objects;
    size_t total_allocations;
    size_t total_frees;
    size_t collections_run;
    size_t roots_marked;
    size_t objects_marked;
    size_t type_live[VAL_BUILTIN + 1];
    size_t type_allocated[VAL_BUILTIN + 1];
    size_t type_freed[VAL_BUILTIN + 1];
} GCStats;

typedef struct GC {
    Value *objects;
    GCStats stats;
    GCPolicy policy;
    size_t next_collection;
    bool initialized;
    bool log_enabled;
    bool stress_enabled;
    bool stats_on_shutdown;
} GC;

typedef struct Env Env;
typedef struct VM VM;
typedef struct Chunk Chunk;

GC *gc_global(void);
void gc_init(GC *gc);
void gc_configure_from_env(GC *gc);
void gc_shutdown(GC *gc);

void gc_track_value(Value *value);
void gc_untrack_value(Value *value);

void gc_mark_value(GC *gc, Value *value);
void gc_mark_env(GC *gc, Env *env);
void gc_mark_vm(GC *gc, VM *vm);
void gc_mark_chunk(GC *gc, Chunk *chunk);
void gc_reset_marks(GC *gc);
void gc_collect_audit(GC *gc, Env *env, VM *vm, Chunk *chunk);

void gc_set_policy(GC *gc, GCPolicy policy);
const char *gc_policy_name(GCPolicy policy);
void gc_print_stats(GC *gc);

#endif
