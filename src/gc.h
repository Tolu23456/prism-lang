#ifndef GC_H
#define GC_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

/* ------------------------------------------------------------------ generation tags */
#define GC_GEN_YOUNG 0
#define GC_GEN_OLD   1

/* ------------------------------------------------------------------ small-int cache bounds */
#define GC_SMALL_INT_MIN (-5)
#define GC_SMALL_INT_MAX  255

/* ------------------------------------------------------------------ workload hint */
typedef enum {
    GC_WORKLOAD_SCRIPT,     /* default: file execution            */
    GC_WORKLOAD_REPL,       /* interactive REPL                   */
    GC_WORKLOAD_GUI,        /* GUI application (avoid pauses)     */
    GC_WORKLOAD_BENCH,      /* benchmark mode (max throughput)    */
} GCWorkload;

/* ------------------------------------------------------------------ collection policy */
typedef enum {
    GC_POLICY_BALANCED,
    GC_POLICY_THROUGHPUT,
    GC_POLICY_LOW_LATENCY,
    GC_POLICY_DEBUG,
    GC_POLICY_STRESS,
    GC_POLICY_ADAPTIVE,     /* auto-tune based on observed behaviour */
} GCPolicy;

/* ------------------------------------------------------------------ string intern table */
typedef struct InternBucket {
    char   *key;             /* owned copy of the interned string   */
    Value  *val;             /* immortal Value* for this string     */
    struct InternBucket *next;
} InternBucket;

/* ------------------------------------------------------------------ allocation site table
 *
 * Tracks how many Value allocations originated from each (file, line)
 * pair in the Prism source.  The interpreter calls gc_set_alloc_site()
 * before each eval step; gc_track_value() records the current site.
 * Used by --mem-report to print the top allocation hotspots.
 * ------------------------------------------------------------------ */
#define GC_ALLOC_SITE_CAP 4096   /* must be a power of two */

typedef struct {
    const char *file;                       /* pointer into argv — stable lifetime   */
    int         line;                       /* Prism source line (0 = empty slot)    */
    size_t      count;                      /* total allocations from this site      */
    size_t      type_counts[VAL_BUILTIN+1]; /* per-type breakdown                    */
} AllocSite;

/* ------------------------------------------------------------------ stats */
typedef struct GCStats {
    /* byte / object accounting */
    size_t bytes_allocated;
    size_t bytes_freed;
    size_t live_objects;
    size_t total_allocations;
    size_t total_frees;

    /* collection counters */
    size_t collections_run;   /* total collections (audit + sweep)   */
    size_t minor_collections; /* generational minor GCs              */
    size_t major_collections; /* generational major GCs              */

    /* mark-phase stats (reset each collection) */
    size_t roots_marked;
    size_t objects_marked;

    /* per-type breakdowns */
    size_t type_live[VAL_BUILTIN + 1];
    size_t type_allocated[VAL_BUILTIN + 1];
    size_t type_freed[VAL_BUILTIN + 1];

    /* generational counters */
    size_t young_live;
    size_t old_live;
    size_t objects_promoted;   /* total young->old promotions         */

    /* adaptive metrics */
    size_t alloc_since_last;   /* allocations since last collection   */
    size_t survived_last;      /* objects that survived last sweep    */

    /* immortal singleton accounting */
    size_t immortal_count;     /* total immortal values (not tracked) */

    /* intern table accounting */
    size_t intern_count;       /* number of interned strings          */
    size_t intern_bytes_saved; /* bytes saved by sharing              */
} GCStats;

/* ------------------------------------------------------------------ PrismGC state */
typedef struct PrismGC {
    /* object list */
    Value   *objects;          /* head of tracked-value linked list   */

    /* statistics */
    GCStats  stats;

    /* policy and thresholds */
    GCPolicy policy;
    size_t   next_collection;  /* alloc-count trigger for next PrismGC     */

    /* generational state */
    size_t   young_count;      /* live young-generation objects       */
    size_t   old_count;        /* live old-generation objects         */
    size_t   minors_since_major;
    size_t   major_interval;   /* run major every N minors (default 8)*/

    /* adaptive tuning */
    double   survival_ema;     /* exponential moving average (0–1)    */
    GCWorkload workload;

    /* string intern table */
    InternBucket **intern_buckets;
    size_t         intern_cap;
    size_t         intern_count;
    size_t         intern_bytes_saved;

    /* allocation site table (open-address hash, power-of-two cap) */
    AllocSite alloc_sites[GC_ALLOC_SITE_CAP];
    size_t    alloc_sites_used;   /* number of occupied slots */

    /* flags */
    bool initialized;
    bool log_enabled;
    bool stress_enabled;
    bool sweep_enabled;
    bool stats_on_shutdown;
    bool mem_report_enabled;
} PrismGC;

/* forward declarations */
typedef struct Env Env;
typedef struct VM  VM;
typedef struct Chunk Chunk;

/* ------------------------------------------------------------------ lifecycle */
PrismGC         *gc_global(void);
void        gc_init(PrismGC *gc);
void        gc_configure_from_env(PrismGC *gc);
void        gc_shutdown(PrismGC *gc);

/* ------------------------------------------------------------------ tracking */
void        gc_track_value(Value *value);
void        gc_untrack_value(Value *value);

/* ------------------------------------------------------------------ allocation site */
void        gc_set_alloc_site(const char *file, int line);

/* ------------------------------------------------------------------ marking */
void        gc_mark_value(PrismGC *gc, Value *value);
void        gc_mark_env(PrismGC *gc, Env *env);
void        gc_mark_vm(PrismGC *gc, VM *vm);
void        gc_mark_chunk(PrismGC *gc, Chunk *chunk);
void        gc_reset_marks(PrismGC *gc);

/* ------------------------------------------------------------------ collection */
void        gc_collect_audit(PrismGC *gc, Env *env, VM *vm, Chunk *chunk);
size_t      gc_collect_sweep(PrismGC *gc, Env *env, VM *vm, Chunk *chunk); /* compat: full sweep */
size_t      gc_collect_minor(PrismGC *gc, Env *env, VM *vm, Chunk *chunk); /* young only         */
size_t      gc_collect_major(PrismGC *gc, Env *env, VM *vm, Chunk *chunk); /* all generations    */

/* ------------------------------------------------------------------ string interning */
Value      *gc_intern_string(PrismGC *gc, const char *s);

/* ------------------------------------------------------------------ policy / reporting */
void        gc_set_policy(PrismGC *gc, GCPolicy policy);
void        gc_set_workload(PrismGC *gc, GCWorkload workload);
const char *gc_policy_name(GCPolicy policy);
const char *gc_workload_name(GCWorkload workload);
void        gc_print_stats(PrismGC *gc);
void        gc_print_mem_report(PrismGC *gc);

#endif
