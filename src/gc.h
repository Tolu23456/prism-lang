#ifndef GC_H
#define GC_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

#define GC_GEN_YOUNG 0
#define GC_GEN_OLD   1

#define GC_SMALL_INT_MIN (-5)
#define GC_SMALL_INT_MAX  255

typedef enum {
    GC_WORKLOAD_SCRIPT,
    GC_WORKLOAD_REPL,
    GC_WORKLOAD_GUI,
    GC_WORKLOAD_BENCH,
} GCWorkload;

typedef enum {
    GC_POLICY_BALANCED,
    GC_POLICY_THROUGHPUT,
    GC_POLICY_LOW_LATENCY,
    GC_POLICY_DEBUG,
    GC_POLICY_STRESS,
    GC_POLICY_ADAPTIVE,
} GCPolicy;

typedef struct InternBucket {
    char   *key;
    Value val;
    struct InternBucket *next;
} InternBucket;

#define GC_ALLOC_SITE_CAP 4096

typedef struct {
    const char *file;
    int         line;
    size_t      count;
    size_t      type_counts[VAL_BUILTIN+1];
} AllocSite;

typedef struct GCStats {
    size_t bytes_allocated, bytes_freed, live_objects, total_allocations, total_frees;
    size_t collections_run, minor_collections, major_collections;
    size_t roots_marked, objects_marked;
    size_t type_live[VAL_BUILTIN + 1], type_allocated[VAL_BUILTIN + 1], type_freed[VAL_BUILTIN + 1];
    size_t young_live, old_live, objects_promoted;
    size_t alloc_since_last, survived_last;
    size_t immortal_count, intern_count, intern_bytes_saved;
} GCStats;

#define GC_ROOT_STACK_MAX 4096

typedef struct PrismGC {
    ValueStruct *objects;
    GCStats  stats;
    GCPolicy policy;
    size_t   next_collection;
    size_t   young_count, old_count, minors_since_major, major_interval;
    double   survival_ema;
    GCWorkload workload;
    InternBucket **intern_buckets;
    size_t         intern_cap, intern_count, intern_bytes_saved;
    AllocSite alloc_sites[GC_ALLOC_SITE_CAP];
    size_t    alloc_sites_used;
    Value root_stack[GC_ROOT_STACK_MAX];
    int     root_stack_top;
    bool initialized, log_enabled, stress_enabled, sweep_enabled, stats_on_shutdown, mem_report_enabled;
} PrismGC;

typedef struct Env Env;
typedef struct VM  VM;
typedef struct Chunk Chunk;

PrismGC *gc_global(void);
void gc_init(PrismGC *gc);
void gc_configure_from_env(PrismGC *gc);
void gc_shutdown(PrismGC *gc);
void gc_track_value(Value value);
void gc_untrack_value(Value value);
void gc_set_alloc_site(const char *file, int line);
void gc_mark_value(PrismGC *gc, Value value);
void gc_mark_env(PrismGC *gc, Env *env);
void gc_mark_vm(PrismGC *gc, VM *vm);
void gc_mark_chunk(PrismGC *gc, Chunk *chunk);
void gc_reset_marks(PrismGC *gc);
void gc_collect_audit(PrismGC *gc, Env *env, VM *vm, Chunk *chunk);
size_t gc_collect_sweep(PrismGC *gc, Env *env, VM *vm, Chunk *chunk);
size_t gc_collect_minor(PrismGC *gc, Env *env, VM *vm, Chunk *chunk);
size_t gc_collect_major(PrismGC *gc, Env *env, VM *vm, Chunk *chunk);
void gc_push_root(PrismGC *gc, Value v);
void gc_pop_root(PrismGC *gc);
Value gc_intern_string(PrismGC *gc, const char *s);
const char *gc_intern_cstr(PrismGC *gc, const char *s);
void gc_set_policy(PrismGC *gc, GCPolicy policy);
void gc_set_workload(PrismGC *gc, GCWorkload workload);
const char *gc_policy_name(GCPolicy policy);
const char *gc_workload_name(GCWorkload workload);
void gc_print_stats(PrismGC *gc);
void gc_print_mem_report(PrismGC *gc);
Value gc_stats_dict(PrismGC *gc);
Value gc_set_soft_limit(PrismGC *gc, const char *text);

#endif
