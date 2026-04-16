#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc.h"
#include "interpreter.h"
#include "vm.h"
#include "chunk.h"

static GC g_gc;

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
    if (strcmp(name, "throughput") == 0) return GC_POLICY_THROUGHPUT;
    if (strcmp(name, "low-latency") == 0) return GC_POLICY_LOW_LATENCY;
    if (strcmp(name, "debug") == 0) return GC_POLICY_DEBUG;
    if (strcmp(name, "stress") == 0) return GC_POLICY_STRESS;
    return GC_POLICY_BALANCED;
}

static const char *gc_type_name(ValueType type) {
    return value_type_name(type);
}

static void gc_free_tracked_value(Value *value) {
    if (!value) return;

    switch (value->type) {
        case VAL_STRING:
            free(value->str_val);
            break;
        case VAL_ARRAY:
            free(value->array.items);
            break;
        case VAL_DICT:
            free(value->dict.entries);
            break;
        case VAL_SET:
            free(value->set.items);
            break;
        case VAL_TUPLE:
            free(value->tuple.items);
            break;
        case VAL_FUNCTION:
            free(value->func.name);
            break;
        case VAL_BUILTIN:
            free(value->builtin.name);
            break;
        default:
            break;
    }

    free(value);
}

static void gc_reclaim_remaining(GC *gc) {
    if (!gc) return;

    size_t reclaimed = 0;
    Value *value = gc->objects;
    while (value) {
        Value *next = value->gc_next;
        if (value->type <= VAL_BUILTIN && gc->stats.type_live[value->type] > 0) {
            gc->stats.type_live[value->type]--;
            gc->stats.type_freed[value->type]++;
        }
        gc_free_tracked_value(value);
        reclaimed++;
        value = next;
    }

    if (reclaimed > 0) {
        gc->stats.total_frees += reclaimed;
        if (gc->stats.live_objects >= reclaimed) gc->stats.live_objects -= reclaimed;
        else gc->stats.live_objects = 0;
    }

    gc->objects = NULL;

    if (reclaimed > 0 && (gc->log_enabled || gc->stats_on_shutdown)) {
        fprintf(stderr, "[gc] reclaimed %zu remaining tracked object(s) at shutdown\n", reclaimed);
    }
}

GC *gc_global(void) {
    if (!g_gc.initialized) {
        gc_init(&g_gc);
        gc_configure_from_env(&g_gc);
    }
    return &g_gc;
}

void gc_init(GC *gc) {
    if (!gc) return;
    memset(gc, 0, sizeof(*gc));
    gc->policy = GC_POLICY_BALANCED;
    gc->next_collection = 1024 * 1024;
    gc->initialized = true;
}

void gc_configure_from_env(GC *gc) {
    if (!gc) return;

    const char *policy = getenv("PRISM_GC_POLICY");
    gc_set_policy(gc, gc_policy_from_string(policy));

    const char *log = getenv("PRISM_GC_LOG");
    const char *stress = getenv("PRISM_GC_STRESS");
    const char *stats = getenv("PRISM_GC_STATS");

    gc->log_enabled = log && strcmp(log, "0") != 0;
    gc->stress_enabled = stress && strcmp(stress, "0") != 0;
    gc->stats_on_shutdown = stats && strcmp(stats, "0") != 0;

    if (gc->policy == GC_POLICY_DEBUG) {
        gc->log_enabled = true;
        gc->stats_on_shutdown = true;
    }

    if (gc->policy == GC_POLICY_STRESS) {
        gc->stress_enabled = true;
        gc->log_enabled = true;
        gc->stats_on_shutdown = true;
    }
}

void gc_shutdown(GC *gc) {
    if (!gc || !gc->initialized) return;
    if (gc->stats_on_shutdown || gc->log_enabled) gc_print_stats(gc);

    if (gc->objects && gc->log_enabled) {
        fprintf(stderr, "[gc] shutdown with %zu tracked live object(s); existing ref-count cleanup owns final release\n", gc->stats.live_objects);
    }
    gc_reclaim_remaining(gc);
}

void gc_track_value(Value *value) {
    if (!value) return;

    GC *gc = gc_global();
    value->gc_marked = 0;
    value->gc_next = gc->objects;
    gc->objects = value;

    size_t size = gc_estimate_value_size(value);
    gc->stats.bytes_allocated += size;
    gc->stats.live_objects++;
    gc->stats.total_allocations++;
    if (value->type <= VAL_BUILTIN) {
        gc->stats.type_live[value->type]++;
        gc->stats.type_allocated[value->type]++;
    }

    if (gc->log_enabled) {
        fprintf(stderr, "[gc] track %s object=%p live=%zu policy=%s\n",
                gc_type_name(value->type), (void *)value, gc->stats.live_objects, gc_policy_name(gc->policy));
    }
}

void gc_untrack_value(Value *value) {
    if (!value) return;

    GC *gc = gc_global();
    Value *prev = NULL;
    Value *cur = gc->objects;

    while (cur) {
        if (cur == value) {
            if (prev) prev->gc_next = cur->gc_next;
            else gc->objects = cur->gc_next;
            break;
        }
        prev = cur;
        cur = cur->gc_next;
    }

    if (cur) {
        size_t size = gc_estimate_value_size(value);
        gc->stats.bytes_freed += size;
        if (gc->stats.live_objects > 0) gc->stats.live_objects--;
        gc->stats.total_frees++;
        if (value->type <= VAL_BUILTIN) {
            if (gc->stats.type_live[value->type] > 0) gc->stats.type_live[value->type]--;
            gc->stats.type_freed[value->type]++;
        }
    }

    value->gc_next = NULL;
    value->gc_marked = 0;
}

void gc_mark_value(GC *gc, Value *value) {
    if (!gc || !value || value->gc_marked) return;

    value->gc_marked = 1;
    gc->stats.objects_marked++;

    switch (value->type) {
        case VAL_ARRAY:
            for (int i = 0; i < value->array.len; i++) gc_mark_value(gc, value->array.items[i]);
            break;
        case VAL_DICT:
            for (int i = 0; i < value->dict.len; i++) {
                gc_mark_value(gc, value->dict.entries[i].key);
                gc_mark_value(gc, value->dict.entries[i].val);
            }
            break;
        case VAL_SET:
            for (int i = 0; i < value->set.len; i++) gc_mark_value(gc, value->set.items[i]);
            break;
        case VAL_TUPLE:
            for (int i = 0; i < value->tuple.len; i++) gc_mark_value(gc, value->tuple.items[i]);
            break;
        case VAL_FUNCTION:
            gc_mark_env(gc, value->func.closure);
            break;
        default:
            break;
    }
}

void gc_mark_env(GC *gc, Env *env) {
    if (!gc || !env) return;

    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->size; i++) {
            gc->stats.roots_marked++;
            gc_mark_value(gc, e->values[i]);
        }
    }
}

void gc_mark_vm(GC *gc, VM *vm) {
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

void gc_mark_chunk(GC *gc, Chunk *chunk) {
    if (!gc || !chunk) return;

    for (int i = 0; i < chunk->const_count; i++) {
        gc->stats.roots_marked++;
        gc_mark_value(gc, chunk->constants[i]);
    }
}

void gc_reset_marks(GC *gc) {
    if (!gc) return;
    for (Value *value = gc->objects; value; value = value->gc_next) value->gc_marked = 0;
    gc->stats.roots_marked = 0;
    gc->stats.objects_marked = 0;
}

void gc_collect_audit(GC *gc, Env *env, VM *vm, Chunk *chunk) {
    if (!gc) return;

    gc_reset_marks(gc);
    gc_mark_env(gc, env);
    gc_mark_vm(gc, vm);
    gc_mark_chunk(gc, chunk);
    gc->stats.collections_run++;

    size_t unreachable = 0;
    for (Value *value = gc->objects; value; value = value->gc_next) {
        if (!value->gc_marked) unreachable++;
    }

    if (gc->log_enabled || gc->policy == GC_POLICY_DEBUG || gc->policy == GC_POLICY_STRESS) {
        fprintf(stderr, "[gc] audit #%zu roots=%zu marked=%zu unreachable-candidates=%zu live=%zu\n",
                gc->stats.collections_run,
                gc->stats.roots_marked,
                gc->stats.objects_marked,
                unreachable,
                gc->stats.live_objects);
    }

    gc_reset_marks(gc);
}

void gc_set_policy(GC *gc, GCPolicy policy) {
    if (!gc) return;
    gc->policy = policy;

    switch (policy) {
        case GC_POLICY_THROUGHPUT:
            gc->next_collection = 8 * 1024 * 1024;
            break;
        case GC_POLICY_LOW_LATENCY:
            gc->next_collection = 256 * 1024;
            break;
        case GC_POLICY_DEBUG:
        case GC_POLICY_STRESS:
            gc->next_collection = 1;
            break;
        case GC_POLICY_BALANCED:
        default:
            gc->next_collection = 1024 * 1024;
            break;
    }
}

const char *gc_policy_name(GCPolicy policy) {
    switch (policy) {
        case GC_POLICY_THROUGHPUT: return "throughput";
        case GC_POLICY_LOW_LATENCY: return "low-latency";
        case GC_POLICY_DEBUG: return "debug";
        case GC_POLICY_STRESS: return "stress";
        case GC_POLICY_BALANCED:
        default: return "balanced";
    }
}

void gc_print_stats(GC *gc) {
    if (!gc) return;

    fprintf(stderr, "[gc] policy=%s collections=%zu live=%zu allocations=%zu frees=%zu bytes_allocated=%zu bytes_freed=%zu\n",
            gc_policy_name(gc->policy),
            gc->stats.collections_run,
            gc->stats.live_objects,
            gc->stats.total_allocations,
            gc->stats.total_frees,
            gc->stats.bytes_allocated,
            gc->stats.bytes_freed);

    for (int i = 0; i <= VAL_BUILTIN; i++) {
        if (gc->stats.type_allocated[i] || gc->stats.type_live[i] || gc->stats.type_freed[i]) {
            fprintf(stderr, "[gc]   %-8s live=%zu allocated=%zu freed=%zu\n",
                    gc_type_name((ValueType)i),
                    gc->stats.type_live[i],
                    gc->stats.type_allocated[i],
                    gc->stats.type_freed[i]);
        }
    }
}
