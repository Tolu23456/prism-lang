import re

with open('src/interpreter.c', 'r') as f:
    c = f.read()

# Replace Environment section (roughly between markers)
env_start_marker = '/* ================================================================== Environment'
env_end_marker = '/* ================================================================== Interpreter */'

env_section = r'''
/* ================================================================== Environment
 *
 * Open-address hash map keyed on string keys.
 * ================================================================== */

#define ENV_INITIAL_CAP 16

static inline unsigned env_hash(const char *s, int cap) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return (unsigned)h & (unsigned)(cap - 1);
}

static inline const char *env_intern(const char *name) {
    return gc_intern_cstr(gc_global(), name);
}

static inline unsigned env_ptr_slot(const char *key, int cap) {
    uintptr_t v = (uintptr_t)key;
    v = (v ^ (v >> 16)) * 0x45d9f3b5u;
    v = v ^ (v >> 16);
    return (unsigned)v & (unsigned)(cap - 1);
}

static void env_rehash(Env *env, int new_cap) {
    EnvSlot *old   = env->slots;
    int      old_c = env->cap;
    env->slots = calloc((size_t)new_cap, sizeof(EnvSlot));
    env->cap   = new_cap;
    env->size  = 0;
    for (int j = 0; j < old_c; j++) {
        if (!old[j].key) continue;
        const char *key = old[j].key;
        unsigned h = env_ptr_slot(key, new_cap);
        while (env->slots[h].key) h = (h + 1) & (unsigned)(new_cap - 1);
        env->slots[h] = old[j];
        env->size++;
    }
    free(old);
}

Env *env_new(Env *parent) {
    Env *e    = calloc(1, sizeof(Env));
    e->refcount = 1;
    e->cap    = ENV_INITIAL_CAP;
    e->slots  = calloc((size_t)e->cap, sizeof(EnvSlot));
    e->size   = 0;
    e->parent = parent;
    env_retain(parent);
    return e;
}

Env *env_retain(Env *env) {
    if (env && env->parent) env->refcount++;
    return env;
}

void env_free(Env *env) {
    if (!env) return;
    if (!env->parent) return;
    if (--env->refcount > 0) return;
    for (int j = 0; j < env->cap; j++) {
        if (env->slots[j].key) value_release(env->slots[j].val);
    }
    free(env->slots);
    Env *parent = env->parent;
    free(env);
    env_free(parent);
}

static void env_free_root(Env *env) {
    if (!env) return;
    for (int j = 0; j < env->cap; j++) {
        if (env->slots[j].key) value_release(env->slots[j].val);
    }
    free(env->slots);
    free(env);
}

Value *env_get(Env *env, const char *name) {
    if (!env || !name) return NULL;
    const char *key = env_intern(name);
    for (Env *e = env; e; e = e->parent) {
        unsigned h = env_ptr_slot(key, e->cap);
        for (int i = 0; i < e->cap; i++) {
            unsigned idx = (h + (unsigned)i) & (unsigned)(e->cap - 1);
            if (!e->slots[idx].key) break;
            if (e->slots[idx].key == key) return &e->slots[idx].val;
        }
    }
    return NULL;
}

bool env_set(Env *env, const char *name, Value val, bool is_const) {
    if (!env || !name) return false;
    const char *key = env_intern(name);
    unsigned h = env_ptr_slot(key, env->cap);
    for (int i = 0; i < env->cap; i++) {
        unsigned idx = (h + (unsigned)i) & (unsigned)(env->cap - 1);
        if (!env->slots[idx].key) break;
        if (env->slots[idx].key == key) {
            if (env->slots[idx].is_const) return false;
            value_release(env->slots[idx].val);
            env->slots[idx].val = value_retain(val);
            env->slots[idx].is_const = is_const;
            return true;
        }
    }
    if (env->size * 4 >= env->cap * 3) {
        env_rehash(env, env->cap * 2);
        h = env_ptr_slot(key, env->cap);
    }
    unsigned idx = h;
    while (env->slots[idx].key) idx = (idx + 1) & (unsigned)(env->cap - 1);
    env->slots[idx].key = key;
    env->slots[idx].val = value_retain(val);
    env->slots[idx].is_const = is_const;
    env->size++;
    return true;
}

bool env_assign(Env *env, const char *name, Value val) {
    if (!env || !name) return false;
    const char *key = env_intern(name);
    for (Env *e = env; e; e = e->parent) {
        unsigned h = env_ptr_slot(key, e->cap);
        for (int i = 0; i < e->cap; i++) {
            unsigned idx = (h + (unsigned)i) & (unsigned)(e->cap - 1);
            if (!e->slots[idx].key) break;
            if (e->slots[idx].key == key) {
                if (e->slots[idx].is_const) return false;
                value_release(e->slots[idx].val);
                e->slots[idx].val = value_retain(val);
                return true;
            }
        }
    }
    return false;
}

bool env_is_const(Env *env, const char *name) {
    if (!env || !name) return false;
    const char *key = env_intern(name);
    for (Env *e = env; e; e = e->parent) {
        unsigned h = env_ptr_slot(key, e->cap);
        for (int i = 0; i < e->cap; i++) {
            unsigned idx = (h + (unsigned)i) & (unsigned)(e->cap - 1);
            if (!e->slots[idx].key) break;
            if (e->slots[idx].key == key) return e->slots[idx].is_const;
        }
    }
    return false;
}

'''

start_idx = c.find(env_start_marker)
end_idx = c.find(env_end_marker)

if start_idx != -1 and end_idx != -1:
    new_c = c[:start_idx] + env_section + c[end_idx:]
    with open('src/interpreter.c', 'w') as f:
        f.write(new_c)
else:
    print("Markers not found!")
