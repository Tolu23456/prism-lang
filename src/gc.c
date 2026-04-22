#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc.h"

static PrismGC g_gc;
void gc_set_alloc_site(const char *f, int l) { (void)f;(void)l; }
void gc_init(PrismGC *gc) {
    if (gc->initialized) return;
    memset(gc, 0, sizeof(*gc));
    gc->intern_cap = 512;
    gc->intern_buckets = calloc(512, sizeof(void*));
    gc->initialized = 1;
}
PrismGC *gc_global(void) { if (!g_gc.initialized) gc_init(&g_gc); return &g_gc; }
Value gc_intern_string(PrismGC *gc, const char *s) {
    if (!gc->initialized) gc_init(gc);
    uint32_t h = 2166136261u;
    const char *p = s; while (*p) { h ^= (uint8_t)(*p++); h *= 16777619u; }
    size_t slot = h & (gc->intern_cap - 1);
    InternBucket *b = gc->intern_buckets[slot];
    while (b) { if (strcmp(b->key, s) == 0) return b->val; b = b->next; }
    ValueStruct *vs = calloc(1, sizeof(ValueStruct));
    vs->_type_ = VAL_STRING; vs->ref_count = 1; vs->gc_immortal = 1; vs->_str_val_ = strdup(s);
    InternBucket *nb = malloc(sizeof(InternBucket));
    nb->key = strdup(s); nb->val = (Value)vs; nb->next = gc->intern_buckets[slot];
    gc->intern_buckets[slot] = nb;
    return (Value)vs;
}
const char *gc_intern_cstr(PrismGC *gc, const char *s) { Value v = gc_intern_string(gc, s); return AS_PTR(v)->_str_val_; }
void gc_track_value(Value v) { (void)v; }
void gc_untrack_value(Value v) { (void)v; }
void gc_mark_env(PrismGC *gc, Env *e) { (void)gc;(void)e; }
void gc_mark_value(PrismGC *gc, Value v) { (void)gc;(void)v; }
void gc_collect_audit(PrismGC *gc, Env *e, VM *vm, Chunk *c) { (void)gc;(void)e;(void)vm;(void)c; }
void gc_shutdown(PrismGC *gc) { (void)gc; }
void gc_reset_marks(PrismGC *gc) { (void)gc; }
void gc_set_policy(PrismGC *gc, GCPolicy p) { (void)gc;(void)p; }
void gc_set_workload(PrismGC *gc, GCWorkload w) { (void)gc;(void)w; }
Value gc_stats_dict(PrismGC *gc) { (void)gc; return value_dict_new(); }
Value gc_set_soft_limit(PrismGC *gc, const char *t) { (void)gc;(void)t; return TO_INT(0); }
void gc_configure_from_env(PrismGC *gc) { (void)gc; }
size_t gc_collect_major(PrismGC *gc, Env *e, VM *vm, Chunk *c) { (void)gc;(void)e;(void)vm;(void)c; return 0; }
void gc_push_root(PrismGC *gc, Value v) { (void)gc;(void)v; }
void gc_pop_root(PrismGC *gc) { (void)gc; }
void gc_mark_chunk(PrismGC *gc, Chunk *c) { (void)gc;(void)c; }
void gc_mark_vm(PrismGC *gc, VM *v) { (void)gc;(void)v; }
