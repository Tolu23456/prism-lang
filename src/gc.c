#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gc.h"
#include "interpreter.h"
#include "vm.h"
#include "chunk.h"

static PrismGC g_gc;
void gc_set_alloc_site(const char *file, int line) { (void)file; (void)line; }
static uint32_t fnv1a(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; } return h; }
static void intern_init(PrismGC *gc) { gc->intern_cap = 1024; gc->intern_buckets = calloc(gc->intern_cap, sizeof(InternBucket *)); }

Value gc_intern_string(PrismGC *gc, const char *s) {
    if (!gc || !s) return VAL_SPEC_NULL;
    uint32_t hash = fnv1a(s); size_t slot = hash & (gc->intern_cap - 1);
    for (InternBucket *b = gc->intern_buckets[slot]; b; b = b->next) if (strcmp(b->key, s) == 0) return b->val;
    ValueStruct *vs = calloc(1, sizeof(ValueStruct)); vs->type = VAL_STRING; vs->ref_count = 1; vs->gc_immortal = 1; vs->str_val = strdup(s);
    Value v = (Value)vs; InternBucket *nb = malloc(sizeof(InternBucket)); nb->key = strdup(s); nb->val = v; nb->next = gc->intern_buckets[slot]; gc->intern_buckets[slot] = nb;
    return v;
}
const char *gc_intern_cstr(PrismGC *gc, const char *s) { if (!s) return s; return AS_STR(gc_intern_string(gc, s)); }

static void vs_free_internal(ValueStruct *vs) {
    if (!vs || vs->gc_immortal) return;
    switch (vs->type) {
        case VAL_STRING: if(vs->str_val) free(vs->str_val); break;
        case VAL_ARRAY: case VAL_SET: case VAL_TUPLE:
            if(vs->array.items) { for (int i = 0; i < vs->array.len; i++) value_release(vs->array.items[i]); free(vs->array.items); }
            break;
        case VAL_DICT:
            if(vs->dict.entries) {
                for (int i = 0; i < vs->dict.cap; i++) if (vs->dict.entries[i].key) { value_release(vs->dict.entries[i].key); value_release(vs->dict.entries[i].val); }
                free(vs->dict.entries);
            }
            break;
        case VAL_FUNCTION:
            if (vs->func.owns_chunk && vs->func.chunk) { chunk_free(vs->func.chunk); free(vs->func.chunk); }
            if (vs->func.name) free(vs->func.name); if (vs->func.closure) env_free(vs->func.closure);
            break;
        case VAL_BUILTIN: if (vs->builtin.name) free(vs->builtin.name); break;
        default: break;
    }
    free(vs);
}

PrismGC *gc_global(void) { if (!g_gc.initialized) { memset(&g_gc, 0, sizeof(g_gc)); g_gc.initialized = true; intern_init(&g_gc); } return &g_gc; }
void gc_track_value(Value value) {
    if (!IS_PTR(value)) return;
    ValueStruct *vs = AS_PTR(value); if (vs->gc_immortal) return;
    PrismGC *gc = gc_global(); vs->gc_next = gc->objects; vs->gc_prev = NULL; if (gc->objects) gc->objects->gc_prev = vs; gc->objects = vs;
}
void gc_untrack_value(Value value) {
    if (!IS_PTR(value)) return;
    ValueStruct *vs = AS_PTR(value); if (vs->gc_immortal) return;
    PrismGC *gc = gc_global(); if (vs->gc_prev) vs->gc_prev->gc_next = vs->gc_next; else gc->objects = vs->gc_next; if (vs->gc_next) vs->gc_next->gc_prev = vs->gc_prev;
}
void gc_mark_value(PrismGC *gc, Value value) {
    if (!IS_PTR(value)) return;
    ValueStruct *vs = AS_PTR(value); if (vs->gc_marked || vs->gc_immortal) return;
    vs->gc_marked = 1;
    switch (vs->type) {
        case VAL_ARRAY: case VAL_SET: case VAL_TUPLE: for (int i = 0; i < vs->array.len; i++) gc_mark_value(gc, vs->array.items[i]); break;
        case VAL_DICT: for (int i = 0; i < vs->dict.cap; i++) if (vs->dict.entries[i].key) { gc_mark_value(gc, vs->dict.entries[i].key); gc_mark_value(gc, vs->dict.entries[i].val); } break;
        case VAL_FUNCTION: if (vs->func.closure) gc_mark_env(gc, vs->func.closure); break;
        default: break;
    }
}
void gc_mark_env(PrismGC *gc, Env *env) { if (!env) return; for (int i = 0; i < env->cap; i++) if (env->slots[i].key) gc_mark_value(gc, env->slots[i].val); if (env->parent) gc_mark_env(gc, env->parent); }
void gc_mark_vm(PrismGC *gc, VM *vm) {
    if (!vm) return;
    for (int i = 0; i < vm->stack_top; i++) gc_mark_value(gc, vm->stack[i]);
    for (int i = 0; i < vm->frame_count; i++) {
        gc_mark_env(gc, vm->frames[i].env);
        for (int j = 0; j < vm->frames[i].local_count; j++) gc_mark_value(gc, vm->frames[i].locals[j]);
        if (vm->frames[i].func) gc_mark_value(gc, vm->frames[i].func);
    }
    if (vm->globals) gc_mark_env(gc, vm->globals);
}
void gc_collect_audit(PrismGC *gc, Env *env, VM *vm, Chunk *chunk) {
    (void)env; (void)chunk; if (!gc) return;
    for (ValueStruct *vs = gc->objects; vs; vs = vs->gc_next) vs->gc_marked = 0;
    gc_mark_vm(gc, vm);
    ValueStruct **ptr = &gc->objects;
    while (*ptr) {
        ValueStruct *vs = *ptr;
        if (!vs->gc_marked && !vs->gc_immortal) { *ptr = vs->gc_next; if (vs->gc_next) vs->gc_next->gc_prev = vs->gc_prev; vs_free_internal(vs); }
        else { vs->gc_marked = 0; ptr = &vs->gc_next; }
    }
}
void gc_init(PrismGC *gc) { (void)gc; }
void gc_shutdown(PrismGC *gc) { (void)gc; }
void gc_mark_chunk(PrismGC *gc, Chunk *c) { if(!c)return; for(int i=0;i<c->const_count;i++) gc_mark_value(gc, c->constants[i]); }
void gc_reset_marks(PrismGC *gc) { (void)gc; }
size_t gc_collect_minor(PrismGC *gc, Env *e, VM *v, Chunk *c) { (void)gc;(void)e;(void)v;(void)c; return 0; }
size_t gc_collect_major(PrismGC *gc, Env *e, VM *v, Chunk *c) { (void)gc;(void)e;(void)v;(void)c; return 0; }
size_t gc_collect_sweep(PrismGC *gc, Env *e, VM *v, Chunk *c) { (void)gc;(void)e;(void)v;(void)c; return 0; }
void gc_push_root(PrismGC *gc, Value v) { (void)gc;(void)v; }
void gc_pop_root(PrismGC *gc) { (void)gc; }
void gc_set_policy(PrismGC *gc, GCPolicy p) { (void)gc;(void)p; }
void gc_set_workload(PrismGC *gc, GCWorkload w) { (void)gc;(void)w; }
const char *gc_policy_name(GCPolicy p) { (void)p; return "balanced"; }
const char *gc_workload_name(GCWorkload w) { (void)w; return "script"; }
void gc_print_stats(PrismGC *gc) { (void)gc; }
void gc_print_mem_report(PrismGC *gc) { (void)gc; }
Value gc_stats_dict(PrismGC *gc) { (void)gc; return value_dict_new(); }
Value gc_set_soft_limit(PrismGC *gc, const char *s) { (void)gc;(void)s; return TO_INT(0); }
void gc_configure_from_env(PrismGC *gc) { (void)gc; }
