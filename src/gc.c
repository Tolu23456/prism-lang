#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "gc.h"
#include "interpreter.h"
#include "vm.h"
#include "chunk.h"

/* ================================================================== global PrismGC instance */

static PrismGC g_gc;

/* ================================================================== allocation site globals
 *
 * The interpreter calls gc_set_alloc_site(file, line) at the top of
 * each eval_node dispatch.  gc_track_value() reads these two globals
 * and records the site.  No signatures are changed elsewhere.
 * ================================================================== */

static const char *s_alloc_file = "?";
static int         s_alloc_line = 0;

void gc_set_alloc_site(const char *file, int line) {
    s_alloc_file = file ? file : "?";
    s_alloc_line = line;
}

/* Find or create the AllocSite entry for (file, line).
 * Uses open-address linear probing keyed on line number. */
static AllocSite *alloc_site_find(PrismGC *gc, const char *file, int line) {
    if (line <= 0) return NULL;
    uint32_t hash = (uint32_t)line * 2654435761u;
    size_t   slot = hash & (GC_ALLOC_SITE_CAP - 1);

    for (size_t i = 0; i < GC_ALLOC_SITE_CAP; i++) {
        AllocSite *s = &gc->alloc_sites[slot];
        if (s->line == 0) {
            /* empty slot — claim it */
            s->file = file;
            s->line = line;
            gc->alloc_sites_used++;
            return s;
        }
        if (s->line == line && s->file == file) return s;
        slot = (slot + 1) & (GC_ALLOC_SITE_CAP - 1);
    }
    return NULL; /* table full — silently drop (very unlikely) */
}

static void alloc_site_bump(PrismGC *gc, ValueType type) {
    AllocSite *s = alloc_site_find(gc, s_alloc_file, s_alloc_line);
    if (!s) return;
    s->count++;
    if ((int)type >= 0 && type <= VAL_BUILTIN)
        s->type_counts[type]++;
}

/* ================================================================== intern table
 *
 * Open-chaining hash map: string → immortal Value*.
 * Identical strings share one Value, saving memory and enabling
 * pointer-equality fast-paths for dict key lookups.
 * ================================================================== */

#define INTERN_INIT_CAP  512
#define INTERN_MAX_STR   128   /* only intern strings <= this length */

static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
    return h;
}

static void intern_init(PrismGC *gc) {
    gc->intern_cap     = INTERN_INIT_CAP;
    gc->intern_count   = 0;
    gc->intern_bytes_saved = 0;
    gc->intern_buckets = calloc(gc->intern_cap, sizeof(InternBucket *));
}

static void intern_free(PrismGC *gc) {
    if (!gc->intern_buckets) return;
    for (size_t i = 0; i < gc->intern_cap; i++) {
        InternBucket *b = gc->intern_buckets[i];
        while (b) {
            InternBucket *next = b->next;
            free(b->key);
            /* val is immortal — free the Value struct directly */
            free(b->val->str_val);
            free(b->val);
            free(b);
            b = next;
        }
    }
    free(gc->intern_buckets);
    gc->intern_buckets = NULL;
}

Value *gc_intern_string(PrismGC *gc, const char *s) {
    if (!gc || !s) return NULL;

    /* don't intern very long strings */
    size_t len = strlen(s);
    if (len > INTERN_MAX_STR || !gc->intern_buckets) {
        /* fall through to normal heap allocation */
        Value *v = calloc(1, sizeof(Value));
        v->type        = VAL_STRING;
        v->ref_count   = 1;
        v->gc_immortal = 0;
        v->gc_generation = GC_GEN_YOUNG;
        v->str_val     = strdup(s);
        gc_track_value(v);
        return v;
    }

    uint32_t hash   = fnv1a(s);
    size_t   slot   = hash & (gc->intern_cap - 1);
    InternBucket *b = gc->intern_buckets[slot];

    /* lookup */
    while (b) {
        if (strcmp(b->key, s) == 0) {
            gc->intern_bytes_saved += sizeof(Value) + len + 1;
            gc->stats.intern_bytes_saved += sizeof(Value) + len + 1;
            return b->val;
        }
        b = b->next;
    }

    /* not found — create immortal Value and add to table */
    Value *v = calloc(1, sizeof(Value));
    v->type        = VAL_STRING;
    v->ref_count   = 1;
    v->gc_immortal = 1;    /* immortal: never freed by PrismGC or value_release */
    v->gc_generation = GC_GEN_OLD; /* treat as old — won't be swept */
    v->str_val     = strdup(s);
    /* NOT gc_track_value — immortals bypass the object list */

    InternBucket *nb = malloc(sizeof(InternBucket));
    nb->key  = strdup(s);
    nb->val  = v;
    nb->next = gc->intern_buckets[slot];
    gc->intern_buckets[slot] = nb;

    gc->intern_count++;
    gc->stats.intern_count++;
    gc->stats.immortal_count++;

    return v;
}

const char *gc_intern_cstr(PrismGC *gc, const char *s) {
    if (!s) return s;
    Value *v = gc_intern_string(gc, s);
    return v ? v->str_val : s;
}

/* ================================================================== helpers */

static size_t gc_estimate_value_size(Value *value) {
    if (!value) return 0;
    size_t size = sizeof(Value);
    switch (value->type) {
        case VAL_STRING:
            size += value->str_val ? strlen(value->str_val) + 1 : 0;
            break;
        case VAL_ARRAY:
            size += (size_t)value->array.cap * sizeof(Value *);
            break;
        case VAL_DICT:
            size += (size_t)value->dict.cap * sizeof(DictEntry);
            break;
        case VAL_SET:
            size += (size_t)value->set.cap * sizeof(Value *);
            break;
        case VAL_TUPLE:
            size += (size_t)value->tuple.cap * sizeof(Value *);
            break;
        case VAL_FUNCTION:
            size += value->func.name ? strlen(value->func.name) + 1 : 0;
            break;
        case VAL_BUILTIN:
            size += value->builtin.name ? strlen(value->builtin.name) + 1 : 0;
            break;
        default:
            break;
    }
    return size;
}

static GCPolicy gc_policy_from_string(const char *name) {
    if (!name || name[0] == '\0') return GC_POLICY_BALANCED;
    if (strcmp(name, "throughput")  == 0) return GC_POLICY_THROUGHPUT;
    if (strcmp(name, "low-latency") == 0) return GC_POLICY_LOW_LATENCY;
    if (strcmp(name, "debug")       == 0) return GC_POLICY_DEBUG;
    if (strcmp(name, "stress")      == 0) return GC_POLICY_STRESS;
    if (strcmp(name, "adaptive")    == 0) return GC_POLICY_ADAPTIVE;
    return GC_POLICY_BALANCED;
}

static const char *gc_type_name(ValueType type) {
    return value_type_name(type);
}

/* Free the internal buffers of a tracked value, then the Value itself.
 * Must NOT recursively release child Value* — sweep decides lifetime. */
static void gc_free_tracked_value(Value *value) {
    if (!value || value->gc_immortal) return;

    switch (value->type) {
        case VAL_STRING:   free(value->str_val);         break;
        case VAL_ARRAY:    free(value->array.items);     break;
        case VAL_DICT:     free(value->dict.entries);    break;
        case VAL_SET:      free(value->set.items);       break;
        case VAL_TUPLE:    free(value->tuple.items);     break;
        case VAL_FUNCTION: free(value->func.name);       break;
        case VAL_BUILTIN:  free(value->builtin.name);    break;
        default: break;
    }
    free(value);
}

/* Reclaim all remaining tracked values (called at shutdown). */
static void gc_reclaim_remaining(PrismGC *gc) {
    if (!gc) return;
    size_t reclaimed = 0;
    Value *value = gc->objects;
    while (value) {
        Value *next = value->gc_next;
        if (value->type <= VAL_BUILTIN && gc->stats.type_live[value->type] > 0)
            gc->stats.type_live[value->type]--;
        gc_free_tracked_value(value);
        reclaimed++;
        value = next;
    }
    if (reclaimed > 0) {
        gc->stats.total_frees += reclaimed;
        gc->stats.live_objects = gc->stats.live_objects > reclaimed
                               ? gc->stats.live_objects - reclaimed : 0;
    }
    gc->objects = NULL;
    if (reclaimed > 0 && (gc->log_enabled || gc->stats_on_shutdown))
        fprintf(stderr, "[gc] reclaimed %zu remaining tracked object(s) at shutdown\n", reclaimed);
}

/* ================================================================== adaptive threshold
 *
 * After each collection we compute the survival rate (objects that were
 * reachable / total tracked before collection) and use an exponential
 * moving average (α = 0.30) to smooth it.  The collection threshold is
 * then nudged up when objects tend to survive (long-lived program) or
 * down when they die quickly (many temporaries).
 * ================================================================== */

static void gc_adaptive_tune(PrismGC *gc, size_t before, size_t freed) {
    if (gc->policy != GC_POLICY_ADAPTIVE && gc->policy != GC_POLICY_BALANCED) return;
    if (before == 0) return;

    size_t survived = before > freed ? before - freed : 0;
    double survival  = (double)survived / (double)before;

    gc->survival_ema = 0.70 * gc->survival_ema + 0.30 * survival;
    gc->stats.survived_last = survived;

    /* adjust threshold */
    if (gc->survival_ema > 0.70) {
        gc->next_collection = gc->next_collection * 3 / 2;   /* objects live long → collect less often */
    } else if (gc->survival_ema < 0.30) {
        gc->next_collection = gc->next_collection * 3 / 4;   /* lots of garbage → collect more often   */
    }

    /* clamp: 64 KB … 64 MB */
    if (gc->next_collection < 64 * 1024)
        gc->next_collection = 64 * 1024;
    if (gc->next_collection > 64 * 1024 * 1024)
        gc->next_collection = 64 * 1024 * 1024;
}

/* ================================================================== lifecycle */

PrismGC *gc_global(void) {
    if (!g_gc.initialized) {
        gc_init(&g_gc);
        gc_configure_from_env(&g_gc);
    }
    return &g_gc;
}

void gc_init(PrismGC *gc) {
    if (!gc) return;
    memset(gc, 0, sizeof(*gc));
    gc->policy          = GC_POLICY_ADAPTIVE;
    gc->next_collection = 1024 * 1024;  /* 1 MB */
    gc->major_interval  = 8;            /* major PrismGC every 8 minors */
    gc->survival_ema    = 0.50;         /* start with neutral assumption */
    gc->workload        = GC_WORKLOAD_SCRIPT;
    gc->initialized     = true;
    intern_init(gc);
}

void gc_configure_from_env(PrismGC *gc) {
    if (!gc) return;

    const char *policy = getenv("PRISM_GC_POLICY");
    gc_set_policy(gc, gc_policy_from_string(policy));

    const char *log    = getenv("PRISM_GC_LOG");
    const char *stress = getenv("PRISM_GC_STRESS");
    const char *stats  = getenv("PRISM_GC_STATS");
    const char *sweep  = getenv("PRISM_GC_SWEEP");

    gc->log_enabled      = log    && strcmp(log,    "0") != 0;
    gc->stress_enabled   = stress && strcmp(stress, "0") != 0;
    gc->sweep_enabled    = sweep  && strcmp(sweep,  "0") != 0;
    gc->stats_on_shutdown = stats && strcmp(stats,  "0") != 0;

    if (gc->policy == GC_POLICY_DEBUG) {
        gc->log_enabled       = true;
        gc->stats_on_shutdown = true;
    }
    if (gc->policy == GC_POLICY_STRESS) {
        gc->stress_enabled    = true;
        gc->sweep_enabled     = true;
        gc->log_enabled       = true;
        gc->stats_on_shutdown = true;
    }
}

void gc_shutdown(PrismGC *gc) {
    if (!gc || !gc->initialized) return;
    if (gc->stats_on_shutdown || gc->log_enabled) gc_print_stats(gc);
    if (gc->mem_report_enabled) gc_print_mem_report(gc);

    if (gc->objects && gc->log_enabled)
        fprintf(stderr, "[gc] shutdown with %zu tracked live object(s)\n", gc->stats.live_objects);

    gc_reclaim_remaining(gc);
    intern_free(gc);
}

/* ================================================================== tracking */

void gc_track_value(Value *value) {
    if (!value || value->gc_immortal) return;

    PrismGC *gc = gc_global();
    value->gc_marked     = 0;
    value->gc_generation = GC_GEN_YOUNG;
    value->gc_next       = gc->objects;
    value->gc_prev       = NULL;
    if (gc->objects) gc->objects->gc_prev = value;
    gc->objects          = value;

    size_t size = gc_estimate_value_size(value);
    gc->stats.bytes_allocated    += size;
    gc->stats.live_objects++;
    gc->stats.total_allocations++;
    gc->stats.alloc_since_last++;
    gc->young_count++;
    gc->stats.young_live++;

    if (value->type <= VAL_BUILTIN) {
        gc->stats.type_live[value->type]++;
        gc->stats.type_allocated[value->type]++;
    }

    /* record allocation site */
    alloc_site_bump(gc, value->type);

    if (gc->log_enabled)
        fprintf(stderr, "[gc] track %s obj=%p gen=young live=%zu\n",
                gc_type_name(value->type), (void *)value, gc->stats.live_objects);
}

void gc_untrack_value(Value *value) {
    if (!value || value->gc_immortal) return;

    PrismGC *gc = gc_global();

    /* O(1) splice-out via doubly-linked list */
    if (value->gc_prev) value->gc_prev->gc_next = value->gc_next;
    else                gc->objects              = value->gc_next;
    if (value->gc_next) value->gc_next->gc_prev = value->gc_prev;

    {
        size_t size = gc_estimate_value_size(value);
        gc->stats.bytes_freed   += size;
        if (gc->stats.live_objects > 0) gc->stats.live_objects--;
        gc->stats.total_frees++;

        if (value->gc_generation == GC_GEN_YOUNG) {
            if (gc->young_count > 0) gc->young_count--;
            if (gc->stats.young_live > 0) gc->stats.young_live--;
        } else {
            if (gc->old_count > 0) gc->old_count--;
            if (gc->stats.old_live > 0) gc->stats.old_live--;
        }

        if (value->type <= VAL_BUILTIN) {
            if (gc->stats.type_live[value->type] > 0) gc->stats.type_live[value->type]--;
            gc->stats.type_freed[value->type]++;
        }
    }

    value->gc_next   = NULL;
    value->gc_prev   = NULL;
    value->gc_marked = 0;
}

/* ================================================================== marking */

void gc_mark_value(PrismGC *gc, Value *value) {
    if (!gc || !value || value->gc_marked || value->gc_immortal) return;

    value->gc_marked = 1;
    gc->stats.objects_marked++;

    switch (value->type) {
        case VAL_ARRAY:
            for (int i = 0; i < value->array.len; i++)
                gc_mark_value(gc, value->array.items[i]);
            break;
        case VAL_DICT:
            for (int i = 0; i < value->dict.len; i++) {
                gc_mark_value(gc, value->dict.entries[i].key);
                gc_mark_value(gc, value->dict.entries[i].val);
            }
            break;
        case VAL_SET:
            for (int i = 0; i < value->set.len; i++)
                gc_mark_value(gc, value->set.items[i]);
            break;
        case VAL_TUPLE:
            for (int i = 0; i < value->tuple.len; i++)
                gc_mark_value(gc, value->tuple.items[i]);
            break;
        case VAL_FUNCTION:
            gc_mark_env(gc, value->func.closure);
            break;
        default:
            break;
    }
}

void gc_mark_env(PrismGC *gc, Env *env) {
    if (!gc || !env) return;
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->cap; i++) {
            if (e->slots[i].key) {
                gc->stats.roots_marked++;
                gc_mark_value(gc, e->slots[i].val);
            }
        }
    }
}

void gc_mark_vm(PrismGC *gc, VM *vm) {
    if (!gc || !vm) return;
    for (int i = 0; i < vm->stack_top; i++) {
        gc->stats.roots_marked++;
        gc_mark_value(gc, vm->stack[i]);
    }
    gc_mark_env(gc, vm->globals);
    for (int i = 0; i < vm->frame_count; i++) {
        gc_mark_env(gc, vm->frames[i].env);
        gc_mark_chunk(gc, vm->frames[i].chunk);
    }
}

void gc_mark_chunk(PrismGC *gc, Chunk *chunk) {
    if (!gc || !chunk) return;
    for (int i = 0; i < chunk->const_count; i++) {
        gc->stats.roots_marked++;
        gc_mark_value(gc, chunk->constants[i]);
    }
}

void gc_reset_marks(PrismGC *gc) {
    if (!gc) return;
    for (Value *v = gc->objects; v; v = v->gc_next) v->gc_marked = 0;
    gc->stats.roots_marked  = 0;
    gc->stats.objects_marked = 0;
}

/* ================================================================== collection — audit (non-destructive) */

void gc_collect_audit(PrismGC *gc, Env *env, VM *vm, Chunk *chunk) {
    if (!gc) return;

    gc_reset_marks(gc);
    gc_mark_env(gc, env);
    gc_mark_vm(gc, vm);
    gc_mark_chunk(gc, chunk);
    gc->stats.collections_run++;
    gc->stats.alloc_since_last = 0;

    size_t unreachable = 0;
    for (Value *v = gc->objects; v; v = v->gc_next)
        if (!v->gc_marked) unreachable++;

    if (gc->log_enabled || gc->policy == GC_POLICY_DEBUG || gc->policy == GC_POLICY_STRESS)
        fprintf(stderr, "[gc] audit #%zu roots=%zu marked=%zu unreachable-candidates=%zu live=%zu\n",
                gc->stats.collections_run,
                gc->stats.roots_marked,
                gc->stats.objects_marked,
                unreachable,
                gc->stats.live_objects);

    if (gc->sweep_enabled) {
        /* use minor unless it's time for a major */
        if (gc->minors_since_major >= gc->major_interval)
            gc_collect_major(gc, env, vm, chunk);
        else
            gc_collect_minor(gc, env, vm, chunk);
    } else {
        gc_reset_marks(gc);
    }
}

/* ================================================================== collection — minor (young generation only)
 *
 * Objects that are reachable are promoted from young → old.
 * Unreachable young objects are freed.
 * Old objects are left untouched.
 * ================================================================== */

size_t gc_collect_minor(PrismGC *gc, Env *env, VM *vm, Chunk *chunk) {
    if (!gc) return 0;

    gc_reset_marks(gc);
    gc_mark_env(gc, env);
    gc_mark_vm(gc, vm);
    gc_mark_chunk(gc, chunk);

    size_t before_young = gc->young_count;
    size_t swept        = 0;
    size_t promoted     = 0;

    Value **p         = &gc->objects;
    Value  *prev_kept = NULL;   /* last node kept so far — used to update gc_prev */
    while (*p) {
        Value *v = *p;

        if (v->gc_generation == GC_GEN_OLD) {
            /* old objects: reset mark flag, keep, update gc_prev */
            v->gc_marked = 0;
            v->gc_prev   = prev_kept;
            prev_kept    = v;
            p = &v->gc_next;
            continue;
        }

        /* young object */
        if (v->gc_marked) {
            /* reachable young → promote to old */
            v->gc_marked     = 0;
            v->gc_generation = GC_GEN_OLD;
            if (gc->young_count > 0) gc->young_count--;
            gc->old_count++;
            if (gc->stats.young_live > 0) gc->stats.young_live--;
            gc->stats.old_live++;
            gc->stats.objects_promoted++;
            promoted++;
            v->gc_prev = prev_kept;
            prev_kept  = v;
            p = &v->gc_next;
        } else {
            /* unreachable young → free */
            *p = v->gc_next;
            if (v->gc_next) v->gc_next->gc_prev = prev_kept;

            if (v->type <= VAL_BUILTIN) {
                if (gc->stats.type_live[v->type] > 0) gc->stats.type_live[v->type]--;
                gc->stats.type_freed[v->type]++;
            }
            if (gc->stats.live_objects > 0) gc->stats.live_objects--;
            if (gc->young_count > 0) gc->young_count--;
            if (gc->stats.young_live > 0) gc->stats.young_live--;
            gc->stats.total_frees++;

            gc_free_tracked_value(v);
            swept++;
        }
    }

    gc->stats.minor_collections++;
    gc->minors_since_major++;
    gc->stats.alloc_since_last = 0;
    gc_adaptive_tune(gc, before_young, swept);

    if (gc->log_enabled || gc->policy == GC_POLICY_DEBUG || gc->policy == GC_POLICY_STRESS)
        fprintf(stderr, "[gc] minor #%zu swept=%zu promoted=%zu young=%zu old=%zu survival=%.0f%%\n",
                gc->stats.minor_collections,
                swept, promoted,
                gc->young_count, gc->old_count,
                gc->survival_ema * 100.0);

    gc_reset_marks(gc);
    return swept;
}

/* ================================================================== collection — major (all generations)
 *
 * Full mark-and-sweep across young and old objects.
 * ================================================================== */

size_t gc_collect_major(PrismGC *gc, Env *env, VM *vm, Chunk *chunk) {
    if (!gc) return 0;

    gc_reset_marks(gc);
    gc_mark_env(gc, env);
    gc_mark_vm(gc, vm);
    gc_mark_chunk(gc, chunk);

    size_t before = gc->stats.live_objects;
    size_t swept  = 0;

    Value **p         = &gc->objects;
    Value  *prev_kept = NULL;
    while (*p) {
        Value *v = *p;
        if (v->gc_marked) {
            v->gc_marked = 0;
            v->gc_prev   = prev_kept;
            prev_kept    = v;
            p = &v->gc_next;
        } else {
            *p = v->gc_next;
            if (v->gc_next) v->gc_next->gc_prev = prev_kept;

            if (v->type <= VAL_BUILTIN) {
                if (gc->stats.type_live[v->type] > 0) gc->stats.type_live[v->type]--;
                gc->stats.type_freed[v->type]++;
            }
            if (v->gc_generation == GC_GEN_YOUNG) {
                if (gc->young_count > 0) gc->young_count--;
                if (gc->stats.young_live > 0) gc->stats.young_live--;
            } else {
                if (gc->old_count > 0) gc->old_count--;
                if (gc->stats.old_live > 0) gc->stats.old_live--;
            }
            if (gc->stats.live_objects > 0) gc->stats.live_objects--;
            gc->stats.total_frees++;

            gc_free_tracked_value(v);
            swept++;
        }
    }

    gc->stats.major_collections++;
    gc->minors_since_major = 0;
    gc->stats.alloc_since_last = 0;
    gc_adaptive_tune(gc, before, swept);

    if (gc->log_enabled || gc->policy == GC_POLICY_DEBUG || gc->policy == GC_POLICY_STRESS)
        fprintf(stderr, "[gc] major #%zu swept=%zu young=%zu old=%zu survival=%.0f%%\n",
                gc->stats.major_collections,
                swept,
                gc->young_count, gc->old_count,
                gc->survival_ema * 100.0);

    gc_reset_marks(gc);
    return swept;
}

/* ================================================================== collection — compat wrapper */

size_t gc_collect_sweep(PrismGC *gc, Env *env, VM *vm, Chunk *chunk) {
    /* kept for API compatibility: delegates to major collection */
    return gc_collect_major(gc, env, vm, chunk);
}

/* ================================================================== policy & workload */

void gc_set_policy(PrismGC *gc, GCPolicy policy) {
    if (!gc) return;
    gc->policy = policy;

    switch (policy) {
        case GC_POLICY_THROUGHPUT:
            gc->next_collection = 8 * 1024 * 1024;
            gc->major_interval  = 16;
            break;
        case GC_POLICY_LOW_LATENCY:
            gc->next_collection = 256 * 1024;
            gc->major_interval  = 4;
            break;
        case GC_POLICY_DEBUG:
        case GC_POLICY_STRESS:
            gc->next_collection = 1;
            gc->major_interval  = 1;
            break;
        case GC_POLICY_ADAPTIVE:
            gc->next_collection = 1024 * 1024;
            gc->major_interval  = 8;
            break;
        case GC_POLICY_BALANCED:
        default:
            gc->next_collection = 1024 * 1024;
            gc->major_interval  = 8;
            break;
    }
}

void gc_set_workload(PrismGC *gc, GCWorkload workload) {
    if (!gc) return;
    gc->workload = workload;

    /* workload hints auto-select an appropriate policy and initial threshold */
    switch (workload) {
        case GC_WORKLOAD_REPL:
            /* interactive: prioritize responsiveness */
            if (gc->policy == GC_POLICY_ADAPTIVE || gc->policy == GC_POLICY_BALANCED) {
                gc->next_collection = 256 * 1024;
                gc->major_interval  = 4;
            }
            break;
        case GC_WORKLOAD_GUI:
            /* GUI app: avoid visible pauses, use low-latency thresholds */
            if (gc->policy == GC_POLICY_ADAPTIVE || gc->policy == GC_POLICY_BALANCED) {
                gc->next_collection = 512 * 1024;
                gc->major_interval  = 4;
            }
            break;
        case GC_WORKLOAD_BENCH:
            /* benchmark: maximise throughput */
            if (gc->policy == GC_POLICY_ADAPTIVE || gc->policy == GC_POLICY_BALANCED) {
                gc->next_collection = 8 * 1024 * 1024;
                gc->major_interval  = 16;
            }
            break;
        case GC_WORKLOAD_SCRIPT:
        default:
            /* script execution: balanced default, let adaptive tuning take over */
            break;
    }
}

const char *gc_policy_name(GCPolicy policy) {
    switch (policy) {
        case GC_POLICY_THROUGHPUT:  return "throughput";
        case GC_POLICY_LOW_LATENCY: return "low-latency";
        case GC_POLICY_DEBUG:       return "debug";
        case GC_POLICY_STRESS:      return "stress";
        case GC_POLICY_ADAPTIVE:    return "adaptive";
        case GC_POLICY_BALANCED:
        default:                    return "balanced";
    }
}

const char *gc_workload_name(GCWorkload workload) {
    switch (workload) {
        case GC_WORKLOAD_REPL:  return "repl";
        case GC_WORKLOAD_GUI:   return "gui";
        case GC_WORKLOAD_BENCH: return "bench";
        case GC_WORKLOAD_SCRIPT:
        default:                return "script";
    }
}

/* ================================================================== reporting */

void gc_print_stats(PrismGC *gc) {
    if (!gc) return;

    fprintf(stderr,
        "[gc] policy=%-10s workload=%-6s collections=%zu (minor=%zu major=%zu)\n"
        "[gc] live=%-6zu allocations=%-8zu frees=%-8zu\n"
        "[gc] bytes_allocated=%zu bytes_freed=%zu\n"
        "[gc] young=%-5zu old=%-5zu promoted=%zu survival_ema=%.0f%%\n"
        "[gc] immortals=%zu interned_strings=%zu intern_bytes_saved=%zu\n",
        gc_policy_name(gc->policy),
        gc_workload_name(gc->workload),
        gc->stats.collections_run,
        gc->stats.minor_collections,
        gc->stats.major_collections,
        gc->stats.live_objects,
        gc->stats.total_allocations,
        gc->stats.total_frees,
        gc->stats.bytes_allocated,
        gc->stats.bytes_freed,
        gc->young_count,
        gc->old_count,
        gc->stats.objects_promoted,
        gc->survival_ema * 100.0,
        gc->stats.immortal_count,
        gc->stats.intern_count,
        gc->stats.intern_bytes_saved);

    for (int i = 0; i <= VAL_BUILTIN; i++) {
        if (gc->stats.type_allocated[i] || gc->stats.type_live[i]) {
            fprintf(stderr, "[gc]   %-8s live=%-6zu allocated=%-8zu freed=%zu\n",
                    gc_type_name((ValueType)i),
                    gc->stats.type_live[i],
                    gc->stats.type_allocated[i],
                    gc->stats.type_freed[i]);
        }
    }
}

/* ================================================================== memory report
 *
 * Detailed diagnostic output shown by --mem-report.  Covers:
 *   - Per-type allocation breakdown with byte estimates
 *   - Generational summary
 *   - Intern table stats (strings shared, bytes saved)
 *   - Adaptive tuning summary (survival rate, threshold)
 *   - Qualitative health indicators
 * ================================================================== */

void gc_print_mem_report(PrismGC *gc) {
    if (!gc) return;

    fprintf(stderr, "\n");
    fprintf(stderr, "=== Prism Memory Report ===\n");
    fprintf(stderr, "\n");

    /* ---- overview ---- */
    fprintf(stderr, "Runtime policy : %s\n", gc_policy_name(gc->policy));
    fprintf(stderr, "Workload hint  : %s\n", gc_workload_name(gc->workload));
    fprintf(stderr, "Live objects   : %zu\n", gc->stats.live_objects);
    fprintf(stderr, "Bytes allocated: %zu (%.1f KB)\n",
            gc->stats.bytes_allocated,
            (double)gc->stats.bytes_allocated / 1024.0);
    fprintf(stderr, "Bytes freed    : %zu (%.1f KB)\n",
            gc->stats.bytes_freed,
            (double)gc->stats.bytes_freed / 1024.0);
    fprintf(stderr, "\n");

    /* ---- collections ---- */
    fprintf(stderr, "Collections    : %zu total  (%zu minor + %zu major)\n",
            gc->stats.collections_run,
            gc->stats.minor_collections,
            gc->stats.major_collections);
    fprintf(stderr, "Objects freed  : %zu\n", gc->stats.total_frees);
    fprintf(stderr, "Promotions     : %zu  (young -> old)\n", gc->stats.objects_promoted);
    fprintf(stderr, "\n");

    /* ---- adaptive metrics ---- */
    fprintf(stderr, "Survival EMA   : %.1f%%  (higher = more long-lived objects)\n",
            gc->survival_ema * 100.0);
    fprintf(stderr, "Next threshold : %zu bytes\n", gc->next_collection);
    if (gc->survival_ema > 0.75)
        fprintf(stderr, "Adaptive hint  : most objects are long-lived; threshold increased\n");
    else if (gc->survival_ema < 0.25)
        fprintf(stderr, "Adaptive hint  : many short-lived temporaries; threshold decreased\n");
    else
        fprintf(stderr, "Adaptive hint  : balanced mix of short- and long-lived objects\n");
    fprintf(stderr, "\n");

    /* ---- generational breakdown ---- */
    fprintf(stderr, "Generational breakdown:\n");
    fprintf(stderr, "  Young  : %zu live  (recently allocated, not yet promoted)\n", gc->young_count);
    fprintf(stderr, "  Old    : %zu live  (survived at least one minor PrismGC)\n",       gc->old_count);
    fprintf(stderr, "\n");

    /* ---- per-type breakdown ---- */
    fprintf(stderr, "Per-type allocation summary:\n");
    fprintf(stderr, "  %-10s  %8s  %8s  %8s  %8s\n",
            "type", "live", "allocated", "freed", "est.bytes");
    fprintf(stderr, "  ----------  --------  --------  --------  ---------\n");

    size_t total_est = 0;
    for (int i = 0; i <= VAL_BUILTIN; i++) {
        if (gc->stats.type_allocated[i] == 0) continue;
        size_t est = gc->stats.type_live[i] * sizeof(Value);
        /* add per-type overhead estimates */
        if (i == VAL_STRING)   est += gc->stats.type_live[i] * 32;
        if (i == VAL_ARRAY)    est += gc->stats.type_live[i] * 64;
        if (i == VAL_DICT)     est += gc->stats.type_live[i] * 128;
        if (i == VAL_FUNCTION) est += gc->stats.type_live[i] * 80;
        total_est += est;
        fprintf(stderr, "  %-10s  %8zu  %8zu  %8zu  %6zu B\n",
                gc_type_name((ValueType)i),
                gc->stats.type_live[i],
                gc->stats.type_allocated[i],
                gc->stats.type_freed[i],
                est);
    }
    fprintf(stderr, "  ----------  --------  --------  --------  ---------\n");
    fprintf(stderr, "  %-10s  %8zu  %8zu  %8zu  %6zu B\n",
            "TOTAL",
            gc->stats.live_objects,
            gc->stats.total_allocations,
            gc->stats.total_frees,
            total_est);
    fprintf(stderr, "\n");

    /* ---- immortal singletons ---- */
    fprintf(stderr, "Immortal singletons:\n");
    fprintf(stderr, "  Count     : %zu  (null, true, false, unknown, ints %d–%d)\n",
            gc->stats.immortal_count,
            GC_SMALL_INT_MIN, GC_SMALL_INT_MAX);
    fprintf(stderr, "  Note      : immortals bypass PrismGC and ref-counting entirely\n");
    fprintf(stderr, "\n");

    /* ---- string interning ---- */
    fprintf(stderr, "String interning:\n");
    fprintf(stderr, "  Interned  : %zu unique strings\n", gc->stats.intern_count);
    fprintf(stderr, "  Saved     : %zu bytes (~%.1f KB) by sharing identical strings\n",
            gc->stats.intern_bytes_saved,
            (double)gc->stats.intern_bytes_saved / 1024.0);
    fprintf(stderr, "  Threshold : strings <= %d bytes are interned\n", INTERN_MAX_STR);
    fprintf(stderr, "\n");

    /* ---- health indicators ---- */
    fprintf(stderr, "Health indicators:\n");
    double leak_ratio = gc->stats.total_allocations > 0
        ? (double)gc->stats.live_objects / (double)gc->stats.total_allocations : 0.0;
    if (leak_ratio > 0.9 && gc->stats.total_allocations > 100)
        fprintf(stderr, "  [!] high live/alloc ratio (%.0f%%) — possible retention or leak\n",
                leak_ratio * 100.0);
    else
        fprintf(stderr, "  [ok] live/alloc ratio = %.0f%%\n", leak_ratio * 100.0);

    if (gc->stats.type_live[VAL_ARRAY] > 0 &&
        gc->stats.type_allocated[VAL_ARRAY] > 50 &&
        gc->stats.type_freed[VAL_ARRAY] == 0)
        fprintf(stderr, "  [!] %zu arrays allocated, none freed — check for unbounded growth\n",
                gc->stats.type_live[VAL_ARRAY]);
    else
        fprintf(stderr, "  [ok] array lifecycle looks healthy\n");

    if (gc->stats.major_collections == 0 && gc->stats.minor_collections > 10)
        fprintf(stderr, "  [i] no major PrismGC ran — try --gc-sweep to enable collection\n");

    fprintf(stderr, "\n");

    /* ---- top allocation sites ---- */
    if (gc->alloc_sites_used > 0) {
        /* Collect pointers to occupied slots, then sort descending by count.
         * Use a simple insertion sort — at most GC_ALLOC_SITE_CAP entries. */
        AllocSite *sorted[GC_ALLOC_SITE_CAP];
        size_t     n = 0;
        for (size_t i = 0; i < GC_ALLOC_SITE_CAP; i++) {
            if (gc->alloc_sites[i].line > 0)
                sorted[n++] = &gc->alloc_sites[i];
        }
        /* insertion sort descending by count */
        for (size_t i = 1; i < n; i++) {
            AllocSite *key = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j-1]->count < key->count) {
                sorted[j] = sorted[j-1];
                j--;
            }
            sorted[j] = key;
        }

        size_t show = n < 10 ? n : 10;
        fprintf(stderr, "Top allocation sites:\n");
        for (size_t i = 0; i < show; i++) {
            AllocSite *s = sorted[i];
            /* find the dominant type at this site */
            ValueType dom = VAL_NULL;
            size_t    dom_count = 0;
            for (int t = 0; t <= VAL_BUILTIN; t++) {
                if (s->type_counts[t] > dom_count) {
                    dom_count = s->type_counts[t];
                    dom = (ValueType)t;
                }
            }
            /* "?" means file was unset (compile-time or init) */
            const char *label = (s->file && s->file[0] != '?') ? s->file : "<compile-time>";
            fprintf(stderr, "  %2zu. %-32s line %-5d  %6zu allocs",
                    i + 1,
                    label,
                    s->line,
                    s->count);
            if (dom_count > 0)
                fprintf(stderr, "  (mostly %s)", gc_type_name(dom));
            fprintf(stderr, "\n");
        }
        if (n > 10)
            fprintf(stderr, "  ... (%zu more sites)\n", n - 10);
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "=== End Memory Report ===\n\n");
}
