#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include "interpreter.h"
#include "parser.h"
#include "value.h"
#ifdef HAVE_X11
#include "xgui.h"
#endif

/* ================================================================== GUI State */

typedef struct {
    char  title[256];
    int   width, height;
    char *body;   /* heap-allocated HTML fragments */
    int   body_len, body_cap;
    int   active; /* 1 after gui_window() called */
} GuiState;

static GuiState g_gui = {"Prism Window", 800, 600, NULL, 0, 0, 0};

static void gui_body_append(const char *html) {
    int add = (int)strlen(html);
    if (!g_gui.body) {
        g_gui.body_cap = 4096;
        g_gui.body = malloc(g_gui.body_cap);
        g_gui.body[0] = '\0';
        g_gui.body_len = 0;
    }
    while (g_gui.body_len + add + 1 >= g_gui.body_cap) {
        g_gui.body_cap *= 2;
        g_gui.body = realloc(g_gui.body, g_gui.body_cap);
    }
    memcpy(g_gui.body + g_gui.body_len, html, add + 1);
    g_gui.body_len += add;
}

/* ================================================================== Environment
 *
 * Open-address hash map keyed on interned const char* pointers.
 * Because all identifier strings are interned through gc_intern_cstr(),
 * the same name always has exactly one address — comparison is a pointer
 * equality check, and hashing is done on the pointer value itself.
 *
 * Hash:   mix the pointer bits with a Knuth multiplicative step
 * Probe:  linear probing
 * Resize: when load > 75%
 * ================================================================== */

#define ENV_INITIAL_CAP 16

/* Mix pointer bits for a well-distributed hash index. */
static inline unsigned env_ptr_slot(const char *key, int cap) {
    uintptr_t v = (uintptr_t)key;
    v = (v ^ (v >> 16)) * 0x45d9f3b5u;
    v = v ^ (v >> 16);
    return (unsigned)v & (unsigned)(cap - 1);
}

/* Intern the name through the global GC so we always have a canonical ptr. */
static inline const char *env_intern(const char *name) {
    return gc_intern_cstr(gc_global(), name);
}

static void env_rehash(Env *env, int new_cap) {
    EnvSlot *old   = env->slots;
    int      old_c = env->cap;
    env->slots = calloc((size_t)new_cap, sizeof(EnvSlot));
    env->cap   = new_cap;
    env->size  = 0;
    for (int i = 0; i < old_c; i++) {
        if (!old[i].key) continue;
        /* re-insert into new table */
        unsigned h = env_ptr_slot(old[i].key, new_cap);
        while (env->slots[h].key) h = (h + 1) & (unsigned)(new_cap - 1);
        env->slots[h] = old[i];
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
    env_retain(parent); /* hold parent alive while this child exists */
    return e;
}

Env *env_retain(Env *env) {
    /* Only ref-count non-root envs; the global (root) env lives as long as the
     * interpreter and freeing it is handled by interpreter_free directly. */
    if (env && env->parent) env->refcount++;
    return env;
}

void env_free(Env *env) {
    if (!env) return;
    /* Root/global env (parent == NULL) is NOT ref-counted here;
     * its lifetime is managed exclusively by interpreter_free. */
    if (!env->parent) return;
    if (--env->refcount > 0) return; /* still referenced by a closure or child */
    /* Release all values stored in this env. */
    for (int i = 0; i < env->cap; i++) {
        if (env->slots[i].key)
            value_release(env->slots[i].val);
    }
    free(env->slots);
    /* Release the reference to the parent that env_new took. */
    Env *parent = env->parent;
    free(env);
    env_free(parent);
}

/* interpreter_free uses this to destroy the root env. */
static void env_free_root(Env *env) {
    if (!env) return;
    for (int i = 0; i < env->cap; i++) {
        if (env->slots[i].key)
            value_release(env->slots[i].val);
    }
    free(env->slots);
    free(env);
}

Value *env_get(Env *env, const char *name) {
    const char *key = env_intern(name);
    for (Env *e = env; e; e = e->parent) {
        unsigned h = env_ptr_slot(key, e->cap);
        for (int i = 0; i < e->cap; i++) {
            unsigned idx = (h + (unsigned)i) & (unsigned)(e->cap - 1);
            if (!e->slots[idx].key) break;           /* empty slot → not here */
            if (e->slots[idx].key == key) return e->slots[idx].val;
        }
    }
    return NULL;
}

bool env_set(Env *env, const char *name, Value *val, bool is_const) {
    const char *key = env_intern(name);
    /* check if key already exists in current scope */
    unsigned h = env_ptr_slot(key, env->cap);
    for (int i = 0; i < env->cap; i++) {
        unsigned idx = (h + (unsigned)i) & (unsigned)(env->cap - 1);
        if (!env->slots[idx].key) break;
        if (env->slots[idx].key == key) {
            if (env->slots[idx].is_const) return false; /* const violation */
            value_release(env->slots[idx].val);
            env->slots[idx].val      = value_retain(val);
            env->slots[idx].is_const = is_const;
            return true;
        }
    }
    /* not found — insert new entry; resize if load > 75% */
    if (env->size * 4 >= env->cap * 3) {
        env_rehash(env, env->cap * 2);
        h = env_ptr_slot(key, env->cap);
    }
    unsigned idx = env_ptr_slot(key, env->cap);
    for (;;) {
        if (!env->slots[idx].key) {
            env->slots[idx].key      = key;
            env->slots[idx].val      = value_retain(val);
            env->slots[idx].is_const = is_const;
            env->size++;
            return true;
        }
        idx = (idx + 1) & (unsigned)(env->cap - 1);
    }
}

bool env_assign(Env *env, const char *name, Value *val) {
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
    return false; /* not found */
}

bool env_is_const(Env *env, const char *name) {
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

/* ================================================================== Interpreter */

static Value *eval_node(Interpreter *interp, ASTNode *node, Env *env);

static void runtime_error(Interpreter *interp, const char *msg, int line) {
    if (interp->had_error) return;
    interp->had_error = 1;
    snprintf(interp->error_msg, sizeof(interp->error_msg), "line %d: %s", line, msg);
}


/* Global interpreter pointer set during builtin dispatch so builtins can
   signal catchable errors without needing the interp in their signature. */
static Interpreter *g_current_interp = NULL;

static void builtin_throw(const char *msg) {
    if (!g_current_interp) { fprintf(stderr, "RuntimeError: %s\n", msg); exit(1); }
    runtime_error(g_current_interp, msg, 0);
}

/* ------------------------------------------------------------------ builtins */

static Value *builtin_output(Value **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (args[i]->type == VAL_STRING)
            printf("%s", args[i]->str_val);
        else
            value_print(args[i]);
    }
    printf("\n");
    return value_null();
}

static Value *builtin_input(Value **args, int argc) {
    if (argc > 0 && args[0]->type == VAL_STRING)
        printf("%s", args[0]->str_val);
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return value_string("");
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return value_string(buf);
}

static Value *builtin_len(Value **args, int argc) {
    if (argc < 1) return value_int(0);
    Value *v = args[0];
    switch (v->type) {
        case VAL_STRING: return value_int((long long)strlen(v->str_val));
        case VAL_ARRAY:  return value_int(v->array.len);
        case VAL_TUPLE:  return value_int(v->tuple.len);
        case VAL_DICT:   return value_int(v->dict.len);
        case VAL_SET:    return value_int(v->set.len);
        default:         return value_int(0);
    }
}

static Value *builtin_bool_fn(Value **args, int argc) {
    if (argc < 1) return value_bool(0);
    Value *v = args[0];
    if (v->type == VAL_BOOL) return value_retain(v);
    if (v->type == VAL_INT)  return value_bool(v->int_val == 0 ? 0 : (v->int_val < 0 ? -1 : 1));
    if (v->type == VAL_STRING) {
        if (strcmp(v->str_val, "true")    == 0) return value_bool(1);
        if (strcmp(v->str_val, "false")   == 0) return value_bool(0);
        if (strcmp(v->str_val, "unknown") == 0) return value_bool(-1);
    }
    return value_bool(value_truthy(v) ? 1 : 0);
}

static Value *builtin_int_fn(Value **args, int argc) {
    if (argc < 1) return value_int(0);
    Value *v = args[0];
    if (v->type == VAL_INT)     return value_retain(v);
    if (v->type == VAL_FLOAT)   return value_int((long long)v->float_val);
    if (v->type == VAL_BOOL)    return value_int(v->bool_val == 1 ? 1 : 0);
    if (v->type == VAL_NULL)    return value_int(0);
    if (v->type == VAL_COMPLEX) return value_int((long long)v->complex_val.real);
    if (v->type == VAL_STRING) {
        const char *s = v->str_val;
        if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
            return value_int(strtoll(s + 2, NULL, 2));
        if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
            return value_int(strtoll(s + 2, NULL, 8));
        return value_int(strtoll(s, NULL, 0));
    }
    return value_int(0);
}

static Value *builtin_float_fn(Value **args, int argc) {
    if (argc < 1) return value_float(0.0);
    Value *v = args[0];
    if (v->type == VAL_FLOAT)   return value_retain(v);
    if (v->type == VAL_INT)     return value_float((double)v->int_val);
    if (v->type == VAL_BOOL)    return value_float(v->bool_val == 1 ? 1.0 : 0.0);
    if (v->type == VAL_NULL)    return value_float(0.0);
    if (v->type == VAL_COMPLEX) return value_float(v->complex_val.real);
    if (v->type == VAL_STRING)  return value_float(strtod(v->str_val, NULL));
    return value_float(0.0);
}

static Value *builtin_str_fn(Value **args, int argc) {
    if (argc < 1) return value_string("");
    char *s = value_to_string(args[0]);
    Value *v = value_string_take(s);
    return v;
}

static Value *builtin_dict_fn(Value **args, int argc) {
    Value *d = value_dict_new();
    /* dict() -> empty dict
     * dict(key, val, key, val, ...) -> pairs
     * dict(other_dict) -> shallow copy */
    if (argc == 1 && args[0]->type == VAL_DICT) {
        for (int i = 0; i < args[0]->dict.len; i++)
            value_dict_set(d, args[0]->dict.entries[i].key, args[0]->dict.entries[i].val);
    } else if (argc % 2 == 0) {
        for (int i = 0; i < argc; i += 2)
            value_dict_set(d, args[i], args[i + 1]);
    }
    return d;
}

static Value *builtin_set_fn(Value **args, int argc) {
    Value *s = value_set_new();
    if (argc >= 1) {
        Value *src = args[0];
        if (src->type == VAL_ARRAY || src->type == VAL_TUPLE) {
            ValueArray *arr = (src->type == VAL_ARRAY) ? &src->array : &src->tuple;
            for (int i = 0; i < arr->len; i++) value_set_add(s, arr->items[i]);
        } else if (src->type == VAL_SET) {
            for (int i = 0; i < src->set.len; i++) value_set_add(s, src->set.items[i]);
        } else if (src->type == VAL_DICT) {
            for (int i = 0; i < src->dict.len; i++) value_set_add(s, src->dict.entries[i].key);
        } else if (src->type == VAL_STRING) {
            const char *p = src->str_val;
            while (*p) {
                char buf[5] = {0};
                buf[0] = *p++;
                value_set_add(s, value_string(buf));
            }
        }
    }
    return s;
}

static Value *builtin_array_fn(Value **args, int argc) {
    Value *a = value_array_new();
    if (argc >= 1) {
        Value *src = args[0];
        if (src->type == VAL_ARRAY) {
            for (int i = 0; i < src->array.len; i++) value_array_push(a, src->array.items[i]);
        } else if (src->type == VAL_TUPLE) {
            for (int i = 0; i < src->tuple.len; i++) value_array_push(a, src->tuple.items[i]);
        } else if (src->type == VAL_SET) {
            for (int i = 0; i < src->set.len; i++) value_array_push(a, src->set.items[i]);
        } else if (src->type == VAL_DICT) {
            for (int i = 0; i < src->dict.len; i++) value_array_push(a, src->dict.entries[i].key);
        } else if (src->type == VAL_STRING) {
            const char *p = src->str_val;
            while (*p) {
                char buf[5] = {0};
                buf[0] = *p++;
                value_array_push(a, value_string(buf));
            }
        } else {
            value_array_push(a, src);
        }
    }
    return a;
}

static Value *builtin_tuple_fn(Value **args, int argc) {
    if (argc < 1) return value_tuple_new(NULL, 0);
    Value *src = args[0];
    if (src->type == VAL_TUPLE) return value_retain(src);
    if (src->type == VAL_ARRAY)
        return value_tuple_new(src->array.items, src->array.len);
    if (src->type == VAL_SET)
        return value_tuple_new(src->set.items, src->set.len);
    if (src->type == VAL_DICT) {
        Value **keys = malloc(src->dict.len * sizeof(Value *));
        for (int i = 0; i < src->dict.len; i++) keys[i] = src->dict.entries[i].key;
        Value *t = value_tuple_new(keys, src->dict.len);
        free(keys);
        return t;
    }
    return value_tuple_new(&src, 1);
}

static Value *builtin_complex_fn(Value **args, int argc) {
    if (argc < 1) return value_complex(0.0, 0.0);
    double real = 0.0, imag = 0.0;
    if (argc >= 1) {
        Value *v = args[0];
        if (v->type == VAL_COMPLEX)  return value_retain(v);
        if (v->type == VAL_INT)      real = (double)v->int_val;
        else if (v->type == VAL_FLOAT) real = v->float_val;
        else if (v->type == VAL_STRING) {
            char *end;
            real = strtod(v->str_val, &end);
            if (*end == '+' || *end == '-') imag = strtod(end, NULL);
        }
    }
    if (argc >= 2) {
        Value *v = args[1];
        if (v->type == VAL_INT)   imag = (double)v->int_val;
        else if (v->type == VAL_FLOAT) imag = v->float_val;
    }
    return value_complex(real, imag);
}

static Value *builtin_type_fn(Value **args, int argc) {
    if (argc < 1) return value_string("null");
    return value_string(value_type_name(args[0]->type));
}

static Value *builtin_assert(Value **args, int argc) {
    if (argc < 1 || !value_truthy(args[0])) {
        const char *msg = (argc > 1 && args[1]->type == VAL_STRING)
            ? args[1]->str_val : "assertion failed";
        fprintf(stderr, "[FAIL] %s\n", msg);
        exit(1);
    }
    return value_null();
}

static Value *builtin_assert_eq(Value **args, int argc) {
    if (argc < 2) {
        fprintf(stderr, "[FAIL] assert_eq requires 2 arguments\n");
        exit(1);
    }
    if (!value_equals(args[0], args[1])) {
        if (argc > 2 && args[2]->type == VAL_STRING) {
            fprintf(stderr, "[FAIL] %s\n", args[2]->str_val);
        } else {
            char *s0 = value_to_string(args[0]);
            char *s1 = value_to_string(args[1]);
            fprintf(stderr, "[FAIL] expected %s but got %s\n", s1, s0);
            free(s0); free(s1);
        }
        exit(1);
    }
    return value_null();
}

/* ================================================================== Missing builtins block */

#include <time.h>

/* ---- math ---- */
static Value *builtin_abs(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT)   return value_int(llabs(a[0]->int_val));
    if (a[0]->type == VAL_FLOAT) return value_float(fabs(a[0]->float_val));
    return value_retain(a[0]);
}
static Value *builtin_sqrt(Value **a, int n) {
    if (n < 1) return value_float(0.0);
    double v = (a[0]->type == VAL_INT) ? (double)a[0]->int_val : a[0]->float_val;
    return value_float(sqrt(v));
}
static Value *builtin_floor(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_int(a[0]->int_val);
    return value_int((long long)floor(a[0]->float_val));
}
static Value *builtin_ceil(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_int(a[0]->int_val);
    return value_int((long long)ceil(a[0]->float_val));
}
static Value *builtin_round(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_int(a[0]->int_val);
    return value_int((long long)round(a[0]->float_val));
}
static Value *builtin_pow(Value **a, int n) {
    if (n < 2) return value_float(0.0);
    double base = (a[0]->type == VAL_INT) ? (double)a[0]->int_val : a[0]->float_val;
    double exp  = (a[1]->type == VAL_INT) ? (double)a[1]->int_val : a[1]->float_val;
    double r = pow(base, exp);
    if (a[0]->type == VAL_INT && a[1]->type == VAL_INT && exp >= 0)
        return value_int((long long)r);
    return value_float(r);
}
static Value *builtin_sin(Value **a, int n) { if (n<1) return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(sin(v)); }
static Value *builtin_cos(Value **a, int n) { if (n<1) return value_float(1.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(cos(v)); }
static Value *builtin_tan(Value **a, int n) { if (n<1) return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(tan(v)); }
static Value *builtin_asin(Value **a, int n) { if (n<1) return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(asin(v)); }
static Value *builtin_acos(Value **a, int n) { if (n<1) return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(acos(v)); }
static Value *builtin_atan(Value **a, int n) { if (n<1) return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(atan(v)); }
static Value *builtin_atan2(Value **a, int n) {
    if (n<2) return value_float(0.0);
    double y=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val;
    double x=(a[1]->type==VAL_INT)?(double)a[1]->int_val:a[1]->float_val;
    return value_float(atan2(y,x));
}
static Value *builtin_log(Value **a, int n) {
    if (n<1) return value_float(0.0);
    double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val;
    if (n>=2) { /* log(x, base) */
        double base=(a[1]->type==VAL_INT)?(double)a[1]->int_val:a[1]->float_val;
        return value_float(log(v)/log(base));
    }
    return value_float(log(v));
}
static Value *builtin_log2(Value **a, int n)  { if(n<1)return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(log2(v)); }
static Value *builtin_log10(Value **a, int n) { if(n<1)return value_float(0.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(log10(v)); }
static Value *builtin_exp(Value **a, int n)   { if(n<1)return value_float(1.0); double v=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val; return value_float(exp(v)); }
static Value *builtin_hypot(Value **a, int n) {
    if(n<2) return value_float(0.0);
    double x=(a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val;
    double y=(a[1]->type==VAL_INT)?(double)a[1]->int_val:a[1]->float_val;
    return value_float(hypot(x,y));
}

static double _to_double(Value *v) {
    if (v->type == VAL_INT)   return (double)v->int_val;
    if (v->type == VAL_FLOAT) return v->float_val;
    if (v->type == VAL_STRING) { char *e; double d=strtod(v->str_val,&e); return (e!=v->str_val)?d:0.0; }
    return 0.0;
}

static Value *builtin_min(Value **a, int n) {
    if (n == 0) return value_null();
    if (n == 1 && a[0]->type == VAL_ARRAY) {
        if (a[0]->array.len == 0) return value_null();
        Value *m = a[0]->array.items[0];
        for (int i = 1; i < a[0]->array.len; i++)
            if (value_compare(a[0]->array.items[i], m) < 0) m = a[0]->array.items[i];
        return value_retain(m);
    }
    Value *m = a[0];
    for (int i = 1; i < n; i++) if (value_compare(a[i], m) < 0) m = a[i];
    return value_retain(m);
}
static Value *builtin_max(Value **a, int n) {
    if (n == 0) return value_null();
    if (n == 1 && a[0]->type == VAL_ARRAY) {
        if (a[0]->array.len == 0) return value_null();
        Value *m = a[0]->array.items[0];
        for (int i = 1; i < a[0]->array.len; i++)
            if (value_compare(a[0]->array.items[i], m) > 0) m = a[0]->array.items[i];
        return value_retain(m);
    }
    Value *m = a[0];
    for (int i = 1; i < n; i++) if (value_compare(a[i], m) > 0) m = a[i];
    return value_retain(m);
}
static Value *builtin_clamp(Value **a, int n) {
    if (n < 3) return n > 0 ? value_retain(a[0]) : value_null();
    if (value_compare(a[0], a[1]) < 0) return value_retain(a[1]);
    if (value_compare(a[0], a[2]) > 0) return value_retain(a[2]);
    return value_retain(a[0]);
}

/* ---- time ---- */
static Value *builtin_clock(Value **a, int n) {
    (void)a; (void)n;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return value_float((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
}
static Value *builtin_time_now(Value **a, int n) {
    (void)a; (void)n;
    return value_float((double)time(NULL));
}

/* ---- string builtins ---- */
static Value *builtin_chars(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_array_new();
    const char *s = a[0]->str_val;
    Value *arr = value_array_new();
    for (const char *p = s; *p; p++) {
        char buf[2] = {*p, '\0'};
        Value *cv = value_string(buf);
        value_array_push(arr, cv);
        value_release(cv);
    }
    return arr;
}
static Value *builtin_upper(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return n>0 ? value_retain(a[0]) : value_string("");
    char *r = strdup(a[0]->str_val);
    for (char *p = r; *p; p++) *p = toupper(*p);
    return value_string_take(r);
}
static Value *builtin_lower(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return n>0 ? value_retain(a[0]) : value_string("");
    char *r = strdup(a[0]->str_val);
    for (char *p = r; *p; p++) *p = tolower(*p);
    return value_string_take(r);
}
static Value *builtin_trim(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return n>0 ? value_retain(a[0]) : value_string("");
    const char *s = a[0]->str_val;
    while (isspace((unsigned char)*s)) s++;
    const char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) e--;
    return value_string(strndup(s, (size_t)(e - s)));
}
static Value *builtin_starts(Value **a, int n) {
    if (n < 2 || a[0]->type != VAL_STRING || a[1]->type != VAL_STRING) return value_bool(0);
    return value_bool(strncmp(a[0]->str_val, a[1]->str_val, strlen(a[1]->str_val)) == 0 ? 1 : 0);
}
static Value *builtin_ends(Value **a, int n) {
    if (n < 2 || a[0]->type != VAL_STRING || a[1]->type != VAL_STRING) return value_bool(0);
    size_t sl = strlen(a[0]->str_val), pl = strlen(a[1]->str_val);
    return value_bool((sl >= pl && strcmp(a[0]->str_val + sl - pl, a[1]->str_val) == 0) ? 1 : 0);
}
static Value *builtin_contains(Value **a, int n) {
    if (n < 2) return value_bool(0);
    if (a[0]->type == VAL_STRING && a[1]->type == VAL_STRING)
        return value_bool(strstr(a[0]->str_val, a[1]->str_val) != NULL ? 1 : 0);
    if (a[0]->type == VAL_ARRAY) {
        for (int i = 0; i < a[0]->array.len; i++)
            if (value_equals(a[0]->array.items[i], a[1])) return value_bool(1);
        return value_bool(0);
    }
    if (a[0]->type == VAL_DICT) {
        Value *found = value_dict_get(a[0], a[1]);
        return value_bool(found ? 1 : 0);
    }
    return value_bool(0);
}
static Value *builtin_split(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_array_new();
    const char *s = a[0]->str_val;
    const char *delim = (n >= 2 && a[1]->type == VAL_STRING) ? a[1]->str_val : " ";
    Value *arr = value_array_new();
    size_t dlen = strlen(delim);
    if (dlen == 0) { /* split each char */
        for (const char *p = s; *p; p++) {
            char buf[2] = {*p, '\0'};
            Value *sv = value_string(buf);
            value_array_push(arr, sv); value_release(sv);
        }
        return arr;
    }
    char *copy = strdup(s), *rest = copy;
    char *pos;
    while ((pos = strstr(rest, delim)) != NULL) {
        *pos = '\0';
        Value *sv = value_string(rest); value_array_push(arr, sv); value_release(sv);
        rest = pos + dlen;
    }
    Value *sv = value_string(rest); value_array_push(arr, sv); value_release(sv);
    free(copy);
    return arr;
}
static Value *builtin_join(Value **a, int n) {
    /* join(sep, arr)  or  join(arr, sep) — detect by type */
    const char *sep = "";
    Value *arr = NULL;
    if (n >= 2) {
        if (a[0]->type == VAL_STRING && a[1]->type == VAL_ARRAY) { sep = a[0]->str_val; arr = a[1]; }
        else if (a[0]->type == VAL_ARRAY && a[1]->type == VAL_STRING) { arr = a[0]; sep = a[1]->str_val; }
        else if (a[0]->type == VAL_ARRAY) { arr = a[0]; }
    } else if (n == 1 && a[0]->type == VAL_ARRAY) {
        arr = a[0];
    }
    if (!arr) return value_string("");
    size_t dlen = strlen(sep);
    int cap = 256, sz = 0;
    char *res = malloc(cap);
    res[0] = '\0';
    for (int i = 0; i < arr->array.len; i++) {
        char *part = value_to_string(arr->array.items[i]);
        size_t plen = strlen(part);
        while (sz + (int)plen + (int)dlen + 2 >= cap) { cap *= 2; res = realloc(res, cap); }
        memcpy(res + sz, part, plen); sz += plen; res[sz] = '\0';
        if (i < arr->array.len - 1 && dlen > 0) { memcpy(res + sz, sep, dlen); sz += dlen; res[sz] = '\0'; }
        free(part);
    }
    return value_string_take(res);
}
static Value *builtin_replace(Value **a, int n) {
    if (n < 3 || a[0]->type != VAL_STRING || a[1]->type != VAL_STRING || a[2]->type != VAL_STRING)
        return n > 0 ? value_retain(a[0]) : value_string("");
    const char *s = a[0]->str_val, *old = a[1]->str_val, *neww = a[2]->str_val;
    size_t oldlen = strlen(old), newlen = strlen(neww);
    int cap = 256, sz = 0;
    char *res = malloc(cap);
    res[0] = '\0';
    const char *p = s;
    while (*p) {
        if (oldlen > 0 && strncmp(p, old, oldlen) == 0) {
            while (sz + (int)newlen + 1 >= cap) { cap *= 2; res = realloc(res, cap); }
            memcpy(res + sz, neww, newlen); sz += newlen; res[sz] = '\0';
            p += oldlen;
        } else {
            if (sz + 1 >= cap) { cap *= 2; res = realloc(res, cap); }
            res[sz++] = *p++; res[sz] = '\0';
        }
    }
    return value_string_take(res);
}
static Value *builtin_fromCharCode(Value **a, int n) {
    if (n < 1) return value_string("");
    char buf[2] = {0, 0};
    long long code = (a[0]->type == VAL_INT) ? a[0]->int_val : (long long)a[0]->float_val;
    buf[0] = (char)(code & 0xFF);
    return value_string(buf);
}
static Value *builtin_ord(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING || a[0]->str_val[0] == '\0') return value_int(0);
    return value_int((long long)(unsigned char)a[0]->str_val[0]);
}
static Value *builtin_parseInt(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_retain(a[0]);
    if (a[0]->type == VAL_FLOAT) return value_int((long long)a[0]->float_val);
    if (a[0]->type == VAL_STRING) {
        int base = (n >= 2 && a[1]->type == VAL_INT) ? (int)a[1]->int_val : 10;
        return value_int(strtoll(a[0]->str_val, NULL, base));
    }
    return value_int(0);
}
static Value *builtin_parseFloat(Value **a, int n) {
    if (n < 1) return value_float(0.0);
    if (a[0]->type == VAL_FLOAT) return value_retain(a[0]);
    if (a[0]->type == VAL_INT)   return value_float((double)a[0]->int_val);
    if (a[0]->type == VAL_STRING) return value_float(strtod(a[0]->str_val, NULL));
    return value_float(0.0);
}
static Value *builtin_repr(Value **a, int n) {
    if (n < 1) return value_string("null");
    char *s = value_to_string(a[0]);
    Value *v = value_string(s);
    free(s);
    return v;
}

/* ---- array builtins ---- */
static Value *builtin_push(Value **a, int n) {
    if (n < 2 || a[0]->type != VAL_ARRAY) return value_null();
    for (int i = 1; i < n; i++) value_array_push(a[0], a[i]);
    return value_retain(a[0]);
}
static Value *builtin_pop(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_ARRAY || a[0]->array.len == 0) return value_null();
    long long idx = (n >= 2 && a[1]->type == VAL_INT) ? a[1]->int_val : (long long)(a[0]->array.len - 1);
    return value_array_pop(a[0], idx);
}
static Value *builtin_sort(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_ARRAY) return n>0?value_retain(a[0]):value_array_new();
    value_array_sort(a[0]);
    return value_retain(a[0]);
}
static Value *builtin_reverse(Value **a, int n) {
    if (n < 1) return value_array_new();
    if (a[0]->type == VAL_ARRAY) {
        /* in-place reverse, same as sort() */
        ValueArray *arr = &a[0]->array;
        for (int i = 0, j = arr->len-1; i < j; i++, j--) {
            Value *tmp = arr->items[i]; arr->items[i] = arr->items[j]; arr->items[j] = tmp;
        }
        return value_retain(a[0]);
    }
    if (a[0]->type == VAL_STRING) {
        const char *s = a[0]->str_val;
        size_t len = strlen(s);
        char *r = malloc(len + 1);
        for (size_t i = 0; i < len; i++) r[i] = s[len-1-i];
        r[len] = '\0';
        return value_string_take(r);
    }
    return value_retain(a[0]);
}
static Value *builtin_slice(Value **a, int n) {
    if (n < 1) return value_array_new();
    long long start = (n >= 2 && a[1]->type == VAL_INT) ? a[1]->int_val : 0;
    if (a[0]->type == VAL_STRING) {
        const char *s = a[0]->str_val;
        long long slen = (long long)strlen(s);
        long long end  = (n >= 3 && a[2]->type == VAL_INT) ? a[2]->int_val : slen;
        if (start < 0) start = slen + start;
        if (end   < 0) end   = slen + end;
        if (start < 0) start = 0;
        if (end > slen) end = slen;
        if (start >= end) return value_string("");
        return value_string(strndup(s + start, (size_t)(end - start)));
    }
    if (a[0]->type == VAL_ARRAY) {
        long long alen = a[0]->array.len;
        long long end  = (n >= 3 && a[2]->type == VAL_INT) ? a[2]->int_val : alen;
        if (start < 0) start = alen + start;
        if (end   < 0) end   = alen + end;
        if (start < 0) start = 0;
        if (end > alen) end = alen;
        Value *arr = value_array_new();
        for (long long i = start; i < end; i++) value_array_push(arr, a[0]->array.items[i]);
        return arr;
    }
    return value_retain(a[0]);
}
static Value *builtin_flatten(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_ARRAY) return n>0?value_retain(a[0]):value_array_new();
    Value *result = value_array_new();
    for (int i = 0; i < a[0]->array.len; i++) {
        Value *item = a[0]->array.items[i];
        if (item->type == VAL_ARRAY) {
            for (int j = 0; j < item->array.len; j++) value_array_push(result, item->array.items[j]);
        } else {
            value_array_push(result, item);
        }
    }
    return result;
}

/* ---- range ---- */
static Value *builtin_range(Value **a, int n) {
    long long start = 0, stop = 0, step = 1;
    if (n == 1) {
        stop = (a[0]->type == VAL_INT) ? a[0]->int_val : (long long)a[0]->float_val;
    } else if (n >= 2) {
        start = (a[0]->type == VAL_INT) ? a[0]->int_val : (long long)a[0]->float_val;
        stop  = (a[1]->type == VAL_INT) ? a[1]->int_val : (long long)a[1]->float_val;
        if (n >= 3)
            step = (a[2]->type == VAL_INT) ? a[2]->int_val : (long long)a[2]->float_val;
    }
    if (step == 0) step = 1;
    Value *arr = value_array_new();
    if (step > 0) {
        for (long long i = start; i < stop; i += step) {
            Value *v = value_int(i); value_array_push(arr, v); value_release(v);
        }
    } else {
        for (long long i = start; i > stop; i += step) {
            Value *v = value_int(i); value_array_push(arr, v); value_release(v);
        }
    }
    return arr;
}

/* ---- dict builtins ---- */
static Value *builtin_keys(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_DICT) return value_array_new();
    Value *arr = value_array_new();
    for (int i = 0; i < a[0]->dict.len; i++) {
        Value *k = a[0]->dict.entries[i].key;
        /* skip internal keys */
        if (k->type == VAL_STRING && k->str_val[0] == '_' && k->str_val[1] == '_') continue;
        value_array_push(arr, k);
    }
    return arr;
}
static Value *builtin_values(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_DICT) return value_array_new();
    Value *arr = value_array_new();
    for (int i = 0; i < a[0]->dict.len; i++) {
        Value *k = a[0]->dict.entries[i].key;
        if (k->type == VAL_STRING && k->str_val[0] == '_' && k->str_val[1] == '_') continue;
        value_array_push(arr, a[0]->dict.entries[i].val);
    }
    return arr;
}
static Value *builtin_items(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_DICT) return value_array_new();
    Value *arr = value_array_new();
    for (int i = 0; i < a[0]->dict.len; i++) {
        Value *k = a[0]->dict.entries[i].key;
        if (k->type == VAL_STRING && k->str_val[0] == '_' && k->str_val[1] == '_') continue;
        Value *pair = value_array_new();
        value_array_push(pair, k);
        value_array_push(pair, a[0]->dict.entries[i].val);
        value_array_push(arr, pair);
        value_release(pair);
    }
    return arr;
}
static Value *builtin_has(Value **a, int n) {
    if (n < 2) return value_bool(0);
    if (a[0]->type == VAL_DICT) {
        Value *found = value_dict_get(a[0], a[1]);
        return value_bool(found ? 1 : 0);
    }
    if (a[0]->type == VAL_ARRAY) {
        for (int i = 0; i < a[0]->array.len; i++)
            if (value_equals(a[0]->array.items[i], a[1])) return value_bool(1);
        return value_bool(0);
    }
    if (a[0]->type == VAL_SET) return value_bool(value_set_has(a[0], a[1]) ? 1 : 0);
    return value_bool(0);
}

/* ---- utility ---- */
static Value *builtin_print(Value **a, int n) {
    for (int i = 0; i < n; i++) {
        if (i > 0) printf(" ");
        char *s = value_to_string(a[i]);
        printf("%s", s);
        free(s);
    }
    printf("\n");
    return value_null();
}
static Value *builtin_error(Value **a, int n) {
    const char *msg = (n > 0 && a[0]->type == VAL_STRING) ? a[0]->str_val : "error";
    builtin_throw(msg);
    return value_null();
}
static Value *builtin_exit(Value **a, int n) {
    int code = (n > 0 && a[0]->type == VAL_INT) ? (int)a[0]->int_val : 0;
    exit(code);
    return value_null();
}
static Value *builtin_isnan(Value **a, int n) {
    if (n < 1) return value_bool(0);
    if (a[0]->type == VAL_FLOAT) return value_bool(isnan(a[0]->float_val) ? 1 : 0);
    return value_bool(0);
}
static Value *builtin_isinf(Value **a, int n) {
    if (n < 1) return value_bool(0);
    if (a[0]->type == VAL_FLOAT) return value_bool(isinf(a[0]->float_val) ? 1 : 0);
    return value_bool(0);
}
static Value *builtin_sum(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_ARRAY) {
        long long isum = 0; double fsum = 0.0; bool use_float = false;
        for (int i = 0; i < a[0]->array.len; i++) {
            Value *v = a[0]->array.items[i];
            if (v->type == VAL_FLOAT) { use_float = true; fsum += v->float_val; }
            else if (v->type == VAL_INT) { isum += v->int_val; fsum += (double)v->int_val; }
        }
        return use_float ? value_float(fsum) : value_int(isum);
    }
    return value_int(0);
}
static Value *builtin_zip(Value **a, int n) {
    if (n < 2) return value_array_new();
    /* find shortest length */
    int minlen = -1;
    for (int i = 0; i < n; i++) {
        if (a[i]->type != VAL_ARRAY) return value_array_new();
        if (minlen < 0 || a[i]->array.len < minlen) minlen = a[i]->array.len;
    }
    if (minlen < 0) minlen = 0;
    Value *result = value_array_new();
    for (int i = 0; i < minlen; i++) {
        Value *tuple = value_array_new();
        for (int j = 0; j < n; j++) value_array_push(tuple, a[j]->array.items[i]);
        value_array_push(result, tuple);
        value_release(tuple);
    }
    return result;
}
static Value *builtin_enumerate(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_ARRAY) return value_array_new();
    long long start = (n >= 2 && a[1]->type == VAL_INT) ? a[1]->int_val : 0;
    Value *result = value_array_new();
    for (int i = 0; i < a[0]->array.len; i++) {
        Value *pair = value_array_new();
        Value *idx = value_int(start + i);
        value_array_push(pair, idx); value_release(idx);
        value_array_push(pair, a[0]->array.items[i]);
        value_array_push(result, pair);
        value_release(pair);
    }
    return result;
}
static Value *builtin_map_fn(Value **a, int n) {
    /* map(fn, arr) */
    if (n < 2 || a[1]->type != VAL_ARRAY) return value_array_new();
    /* Note: to call fn we'd need interpreter context; return stub array for now */
    return value_retain(a[1]);
}
static Value *builtin_filter_fn(Value **a, int n) {
    if (n < 2 || a[1]->type != VAL_ARRAY) return value_array_new();
    return value_retain(a[1]);
}
static Value *builtin_copy(Value **a, int n) {
    if (n < 1) return value_null();
    return value_copy(a[0]);
}
static Value *builtin_id(Value **a, int n) {
    if (n < 1) return value_int(0);
    return value_int((long long)(uintptr_t)a[0]);
}
static Value *builtin_hex(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_INT) return value_string("0x0");
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)a[0]->int_val);
    return value_string(buf);
}
static Value *builtin_bin(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_INT) return value_string("0b0");
    long long v = a[0]->int_val;
    char buf[70]; int pos = 67;
    buf[68] = '\0'; buf[67] = '0';
    if (v == 0) { buf[67] = '0'; buf[66] = 'b'; buf[65] = '0'; return value_string(buf + 65); }
    unsigned long long uv = (unsigned long long)v;
    buf[pos--] = '\0';
    while (uv > 0) { buf[pos--] = (uv & 1) ? '1' : '0'; uv >>= 1; }
    buf[pos--] = 'b'; buf[pos--] = '0';
    return value_string(buf + pos + 1);
}
static Value *builtin_oct(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_INT) return value_string("0o0");
    char buf[32];
    snprintf(buf, sizeof(buf), "0o%llo", (unsigned long long)a[0]->int_val);
    return value_string(buf);
}

/* math constants exposed as builtins */
static Value *builtin_math_pi(Value **a, int n) { (void)a;(void)n; return value_float(3.14159265358979323846); }
static Value *builtin_math_e(Value **a, int n)  { (void)a;(void)n; return value_float(2.71828182845904523536); }
static Value *builtin_math_tau(Value **a, int n){ (void)a;(void)n; return value_float(6.28318530717958647692); }
static Value *builtin_math_inf(Value **a, int n){ (void)a;(void)n; return value_float(1.0/0.0); }
static Value *builtin_math_nan(Value **a, int n){ (void)a;(void)n; return value_float(0.0/0.0); }

static bool is_memory_module(Value *obj) {
    if (!obj || obj->type != VAL_DICT) return false;
    Value *key = value_string("__module");
    Value *found = value_dict_get(obj, key);
    value_release(key);
    return found && found->type == VAL_STRING && strcmp(found->str_val, "memory") == 0;
}

static Value *memory_method(Interpreter *interp, Env *env, Value *obj, const char *method,
                            Value **args, int argc, int line) {
    (void)obj;
    PrismGC *gc = interp->gc ? interp->gc : gc_global();
    if (strcmp(method, "stats") == 0) return gc_stats_dict(gc);
    if (strcmp(method, "collect") == 0) {
        size_t freed = gc_collect_major(gc, env, NULL, NULL);
        return value_int((long long)freed);
    }
    if (strcmp(method, "limit") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) {
            runtime_error(interp, "memory.limit() requires a string like \"512mb\"", line);
            return value_null();
        }
        return gc_set_soft_limit(gc, args[0]->str_val);
    }
    if (strcmp(method, "profile") == 0) return gc_stats_dict(gc);
    char emsg[128];
    snprintf(emsg, sizeof(emsg), "memory has no method '%s'", method);
    runtime_error(interp, emsg, line);
    return value_null();
}

/* ------------------------------------------------------------------ GUI builtins */

static Value *builtin_gui_window(Value **args, int argc) {
    if (argc >= 1 && args[0]->type == VAL_STRING)
        snprintf(g_gui.title, sizeof(g_gui.title), "%s", args[0]->str_val);
    if (argc >= 2 && args[1]->type == VAL_INT) g_gui.width  = (int)args[1]->int_val;
    if (argc >= 3 && args[2]->type == VAL_INT) g_gui.height = (int)args[2]->int_val;
    g_gui.active = 1;
    return value_null();
}

static Value *builtin_gui_label(Value **args, int argc) {
    char buf[2048];
    const char *text = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "";
    snprintf(buf, sizeof(buf), "  <p class=\"gui-label\">%s</p>\n", text);
    gui_body_append(buf);
    return value_null();
}

static Value *builtin_gui_button(Value **args, int argc) {
    char buf[2048];
    const char *text = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "Button";
    snprintf(buf, sizeof(buf), "  <button class=\"gui-btn\">%s</button>\n", text);
    gui_body_append(buf);
    return value_null();
}

static Value *builtin_gui_input(Value **args, int argc) {
    char buf[2048];
    const char *ph = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "";
    snprintf(buf, sizeof(buf), "  <input class=\"gui-input\" type=\"text\" placeholder=\"%s\" />\n", ph);
    gui_body_append(buf);
    return value_null();
}

static Value *builtin_gui_run(Value **args, int argc) {
    (void)args; (void)argc;
    if (!g_gui.active) {
        fprintf(stderr, "[prism] gui_run() called without gui_window()\n");
        return value_null();
    }
    FILE *f = fopen("prism_gui.html", "w");
    if (!f) { fprintf(stderr, "[prism] could not write prism_gui.html\n"); return value_null(); }
    fprintf(f,
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>%s</title>\n"
        "  <style>\n"
        "    body { font-family: sans-serif; margin: 0; padding: 20px;"
        "           width: %dpx; min-height: %dpx; box-sizing: border-box; }\n"
        "    .gui-label  { font-size: 16px; margin: 8px 0; }\n"
        "    .gui-btn    { padding: 8px 18px; font-size: 15px; cursor: pointer;"
        "                  background: #4f8ef7; color: #fff; border: none;"
        "                  border-radius: 4px; margin: 6px 4px; }\n"
        "    .gui-btn:hover { background: #2563c7; }\n"
        "    .gui-input  { padding: 7px 12px; font-size: 15px; border: 1px solid #ccc;"
        "                  border-radius: 4px; margin: 6px 0; width: 100%%; box-sizing: border-box; }\n"
        "  </style>\n"
        "</head>\n<body>\n"
        "  <h2>%s</h2>\n"
        "%s"
        "</body>\n</html>\n",
        g_gui.title, g_gui.width, g_gui.height,
        g_gui.title,
        g_gui.body ? g_gui.body : "");
    fclose(f);
    printf("[prism] GUI written to prism_gui.html\n");
    return value_null();
}

/* ================================================================== XGUI builtins */

#ifdef HAVE_X11

static XGui *g_xgui = NULL;

static Value *bi_xgui_init(Value **args, int argc) {
    int w = (argc > 0 && args[0]->type == VAL_INT) ? (int)args[0]->int_val : 800;
    int h = (argc > 1 && args[1]->type == VAL_INT) ? (int)args[1]->int_val : 600;
    const char *title = (argc > 2 && args[2]->type == VAL_STRING) ? args[2]->str_val : "Prism";
    if (g_xgui) xgui_destroy(g_xgui);
    g_xgui = xgui_init(w, h, title);
    return value_null();
}

static Value *bi_xgui_style(Value **args, int argc) {
    if (g_xgui && argc > 0 && args[0]->type == VAL_STRING)
        xgui_load_style(g_xgui, args[0]->str_val);
    return value_null();
}

static Value *bi_xgui_running(Value **args, int argc) {
    (void)args; (void)argc;
    return value_bool(xgui_running(g_xgui) ? 1 : 0);
}

static Value *bi_xgui_begin(Value **args, int argc) {
    (void)args; (void)argc;
    xgui_begin(g_xgui);
    return value_null();
}

static Value *bi_xgui_end(Value **args, int argc) {
    (void)args; (void)argc;
    xgui_end(g_xgui);
    return value_null();
}

static Value *bi_xgui_label(Value **args, int argc) {
    if (g_xgui && argc > 0 && args[0]->type == VAL_STRING)
        xgui_label(g_xgui, args[0]->str_val);
    return value_null();
}

static Value *bi_xgui_button(Value **args, int argc) {
    if (!g_xgui || argc < 1 || args[0]->type != VAL_STRING)
        return value_bool(0);
    return value_bool(xgui_button(g_xgui, args[0]->str_val) ? 1 : 0);
}

static Value *bi_xgui_input(Value **args, int argc) {
    if (!g_xgui) return value_string("");
    const char *id          = (argc > 0 && args[0]->type == VAL_STRING) ? args[0]->str_val : "input";
    const char *placeholder = (argc > 1 && args[1]->type == VAL_STRING) ? args[1]->str_val : "";
    const char *val = xgui_input(g_xgui, id, placeholder);
    return value_string(val ? val : "");
}

static Value *bi_xgui_spacer(Value **args, int argc) {
    int h = (argc > 0 && args[0]->type == VAL_INT) ? (int)args[0]->int_val : 16;
    xgui_spacer(g_xgui, h);
    return value_null();
}

static Value *bi_xgui_row_begin(Value **args, int argc) {
    (void)args; (void)argc;
    xgui_row_begin(g_xgui);
    return value_null();
}

static Value *bi_xgui_row_end(Value **args, int argc) {
    (void)args; (void)argc;
    xgui_row_end(g_xgui);
    return value_null();
}

static Value *bi_xgui_close(Value **args, int argc) {
    (void)args; (void)argc;
    if (g_xgui) { xgui_destroy(g_xgui); g_xgui = NULL; }
    return value_null();
}

#else /* !HAVE_X11 — graceful stubs */

static Value *bi_xgui_no_x11(Value **args, int argc) {
    (void)args; (void)argc;
    fprintf(stderr, "xgui: X11 support was not compiled in. "
                    "Install libX11-dev / xorg-dev and recompile.\n");
    return value_null();
}

#endif /* HAVE_X11 */

static void register_builtins(Interpreter *interp) {
    struct { const char *name; BuiltinFn fn; } builtins[] = {
        /* core I/O */
        {"output",      builtin_output},
        {"print",       builtin_print},
        {"input",       builtin_input},
        /* type functions */
        {"len",         builtin_len},
        {"bool",        builtin_bool_fn},
        {"int",         builtin_int_fn},
        {"float",       builtin_float_fn},
        {"str",         builtin_str_fn},
        {"dict",        builtin_dict_fn},
        {"set",         builtin_set_fn},
        {"array",       builtin_array_fn},
        {"tuple",       builtin_tuple_fn},
        {"complex",     builtin_complex_fn},
        {"type",        builtin_type_fn},
        {"repr",        builtin_repr},
        {"copy",        builtin_copy},
        {"id",          builtin_id},
        /* assertion */
        {"assert",      builtin_assert},
        {"assert_eq",   builtin_assert_eq},
        /* math */
        {"abs",         builtin_abs},
        {"sqrt",        builtin_sqrt},
        {"floor",       builtin_floor},
        {"ceil",        builtin_ceil},
        {"round",       builtin_round},
        {"pow",         builtin_pow},
        {"sin",         builtin_sin},
        {"cos",         builtin_cos},
        {"tan",         builtin_tan},
        {"asin",        builtin_asin},
        {"acos",        builtin_acos},
        {"atan",        builtin_atan},
        {"atan2",       builtin_atan2},
        {"log",         builtin_log},
        {"log2",        builtin_log2},
        {"log10",       builtin_log10},
        {"exp",         builtin_exp},
        {"hypot",       builtin_hypot},
        {"isnan",       builtin_isnan},
        {"isinf",       builtin_isinf},
        {"min",         builtin_min},
        {"max",         builtin_max},
        {"clamp",       builtin_clamp},
        {"sum",         builtin_sum},
        /* string functions */
        {"chars",       builtin_chars},
        {"upper",       builtin_upper},
        {"lower",       builtin_lower},
        {"trim",        builtin_trim},
        {"starts",      builtin_starts},
        {"ends",        builtin_ends},
        {"contains",    builtin_contains},
        {"split",       builtin_split},
        {"join",        builtin_join},
        {"replace",     builtin_replace},
        {"fromCharCode",builtin_fromCharCode},
        {"chr",         builtin_fromCharCode},
        {"ord",         builtin_ord},
        {"parseInt",    builtin_parseInt},
        {"parseFloat",  builtin_parseFloat},
        {"hex",         builtin_hex},
        {"bin",         builtin_bin},
        {"oct",         builtin_oct},
        /* array functions */
        {"push",        builtin_push},
        {"pop",         builtin_pop},
        {"sort",        builtin_sort},
        {"reverse",     builtin_reverse},
        {"slice",       builtin_slice},
        {"flatten",     builtin_flatten},
        {"range",       builtin_range},
        {"zip",         builtin_zip},
        {"enumerate",   builtin_enumerate},
        /* dict functions */
        {"keys",        builtin_keys},
        {"values",      builtin_values},
        {"items",       builtin_items},
        {"has",         builtin_has},
        /* time */
        {"clock",       builtin_clock},
        {"time",        builtin_time_now},
        /* utility */
        {"error",       builtin_error},
        {"exit",        builtin_exit},
        /* math constants as functions */
        {"PI",          builtin_math_pi},
        {"E",           builtin_math_e},
        {"TAU",         builtin_math_tau},
        {"INF",         builtin_math_inf},
        {"NAN",         builtin_math_nan},
        {"gui_window",  builtin_gui_window},
        {"gui_label",   builtin_gui_label},
        {"gui_button",  builtin_gui_button},
        {"gui_input",   builtin_gui_input},
        {"gui_run",     builtin_gui_run},
        /* X11 native GUI */
#ifdef HAVE_X11
        {"xgui_init",      bi_xgui_init},
        {"xgui_style",     bi_xgui_style},
        {"xgui_running",   bi_xgui_running},
        {"xgui_begin",     bi_xgui_begin},
        {"xgui_end",       bi_xgui_end},
        {"xgui_label",     bi_xgui_label},
        {"xgui_button",    bi_xgui_button},
        {"xgui_input",     bi_xgui_input},
        {"xgui_spacer",    bi_xgui_spacer},
        {"xgui_row_begin", bi_xgui_row_begin},
        {"xgui_row_end",   bi_xgui_row_end},
        {"xgui_close",     bi_xgui_close},
#else
        {"xgui_init",      bi_xgui_no_x11},
        {"xgui_style",     bi_xgui_no_x11},
        {"xgui_running",   bi_xgui_no_x11},
        {"xgui_begin",     bi_xgui_no_x11},
        {"xgui_end",       bi_xgui_no_x11},
        {"xgui_label",     bi_xgui_no_x11},
        {"xgui_button",    bi_xgui_no_x11},
        {"xgui_input",     bi_xgui_no_x11},
        {"xgui_spacer",    bi_xgui_no_x11},
        {"xgui_row_begin", bi_xgui_no_x11},
        {"xgui_row_end",   bi_xgui_no_x11},
        {"xgui_close",     bi_xgui_no_x11},
#endif
        {NULL, NULL}
    };
    for (int i = 0; builtins[i].name; i++) {
        Value *v = value_builtin(builtins[i].name, builtins[i].fn);
        env_set(interp->globals, builtins[i].name, v, false);
        value_release(v);
    }
    /* math constants as values */
    {
        struct { const char *name; double val; } consts[] = {
            {"PI",  3.14159265358979323846},
            {"E",   2.71828182845904523536},
            {"TAU", 6.28318530717958647692},
            {NULL, 0.0}
        };
        for (int i = 0; consts[i].name; i++) {
            Value *v = value_float(consts[i].val);
            env_set(interp->globals, consts[i].name, v, true);
            value_release(v);
        }
        Value *inf_v = value_float(1.0/0.0);
        env_set(interp->globals, "INF", inf_v, true);
        value_release(inf_v);
        Value *nan_v = value_float(0.0/0.0);
        env_set(interp->globals, "NAN", nan_v, true);
        value_release(nan_v);
    }

    Value *memory = value_dict_new();
    Value *key = value_string("__module");
    Value *name = value_string("memory");
    value_dict_set(memory, key, name);
    value_release(key);
    value_release(name);
    env_set(interp->globals, "memory", memory, true);
    value_release(memory);
}

/* ------------------------------------------------------------------ f-string processing */

static char *process_fstring(Interpreter *interp, const char *tmpl, Env *env) {
    int   cap = 256, sz = 0;
    char *buf = malloc(cap);
    buf[0] = '\0';

    while (*tmpl) {
        if (*tmpl == '{' && *(tmpl+1) != '{') {
            tmpl++;
            /* find closing } */
            const char *end = strchr(tmpl, '}');
            if (!end) break;
            int len = (int)(end - tmpl);
            char *expr_src = strndup(tmpl, len);
            tmpl = end + 1;

            Parser  *ep = parser_new(expr_src);
            ASTNode *ea = parser_parse(ep);
            free(expr_src);

            char *val_str = NULL;
            if (!ep->had_error) {
                Value *result = eval_node(interp, ea, env);
                if (result) {
                    val_str = value_to_string(result);
                    value_release(result);
                }
            }
            ast_node_free(ea);
            parser_free(ep);

            if (!val_str) val_str = strdup("null");
            size_t need = sz + strlen(val_str) + 1;
            while ((int)need >= cap) { cap *= 2; buf = realloc(buf, cap); }
            strcat(buf, val_str);
            sz += strlen(val_str);
            free(val_str);
        } else if (*tmpl == '{' && *(tmpl+1) == '{') {
            if (sz + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[sz++] = '{'; buf[sz] = '\0';
            tmpl += 2;
        } else {
            if (sz + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[sz++] = *tmpl++;
            buf[sz] = '\0';
        }
    }
    return buf;
}

/* ------------------------------------------------------------------ string methods */

static Value *string_method(Interpreter *interp, Value *obj, const char *method,
                             Value **args, int argc, int line) {
    const char *s = obj->str_val;
    (void)interp;

    if (strcmp(method, "upper") == 0) {
        char *r = strdup(s);
        for (char *p = r; *p; p++) *p = toupper(*p);
        return value_string_take(r);
    }
    if (strcmp(method, "lower") == 0) {
        char *r = strdup(s);
        for (char *p = r; *p; p++) *p = tolower(*p);
        return value_string_take(r);
    }
    if (strcmp(method, "capitalize") == 0) {
        char *r = strdup(s);
        if (r[0]) r[0] = toupper(r[0]);
        for (char *p = r+1; *p; p++) *p = tolower(*p);
        return value_string_take(r);
    }
    if (strcmp(method, "title") == 0) {
        char *r = strdup(s);
        bool cap_next = true;
        for (char *p = r; *p; p++) {
            if (isspace(*p)) cap_next = true;
            else if (cap_next) { *p = toupper(*p); cap_next = false; }
            else *p = tolower(*p);
        }
        return value_string_take(r);
    }
    if (strcmp(method, "strip") == 0) {
        const char *start = s;
        while (isspace(*start)) start++;
        const char *end = s + strlen(s);
        while (end > start && isspace(*(end-1))) end--;
        return value_string(strndup(start, end - start));
    }
    if (strcmp(method, "lstrip") == 0) {
        while (isspace(*s)) s++;
        return value_string(s);
    }
    if (strcmp(method, "rstrip") == 0) {
        char *r = strdup(s);
        char *end = r + strlen(r);
        while (end > r && isspace(*(end-1))) *--end = '\0';
        return value_string_take(r);
    }
    if (strcmp(method, "find") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_int(-1);
        const char *found = strstr(s, args[0]->str_val);
        return value_int(found ? (long long)(found - s) : -1LL);
    }
    if (strcmp(method, "index") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_int(-1);
        const char *found = strstr(s, args[0]->str_val);
        if (!found) { runtime_error(interp, "substring not found", line); return value_null(); }
        return value_int((long long)(found - s));
    }
    if (strcmp(method, "replace") == 0) {
        if (argc < 2 || args[0]->type != VAL_STRING || args[1]->type != VAL_STRING)
            return value_retain(obj);
        const char *old = args[0]->str_val, *neww = args[1]->str_val;
        size_t oldlen = strlen(old), newlen = strlen(neww);
        int cap = 256, sz = 0;
        char *res = malloc(cap);
        res[0] = '\0';
        const char *p = s;
        while (*p) {
            if (oldlen > 0 && strncmp(p, old, oldlen) == 0) {
                while (sz + (int)newlen + 1 >= cap) { cap *= 2; res = realloc(res, cap); }
                memcpy(res + sz, neww, newlen); sz += newlen; res[sz] = '\0';
                p += oldlen;
            } else {
                if (sz + 1 >= cap) { cap *= 2; res = realloc(res, cap); }
                res[sz++] = *p++; res[sz] = '\0';
            }
        }
        return value_string_take(res);
    }
    if (strcmp(method, "split") == 0) {
        const char *delim = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : " ";
        Value *arr = value_array_new();
        char *copy = strdup(s), *rest = copy;
        size_t dlen = strlen(delim);
        if (dlen == 0) {
            /* split into chars */
            for (char *p = copy; *p; p++) {
                char ch[2] = {*p, '\0'};
                Value *sv = value_string(ch);
                value_array_push(arr, sv); value_release(sv);
            }
        } else {
            char *pos;
            while ((pos = strstr(rest, delim)) != NULL) {
                *pos = '\0';
                Value *sv = value_string(rest); value_array_push(arr, sv); value_release(sv);
                rest = pos + dlen;
            }
            Value *sv = value_string(rest); value_array_push(arr, sv); value_release(sv);
        }
        free(copy);
        return arr;
    }
    if (strcmp(method, "join") == 0) {
        if (argc < 1 || args[0]->type != VAL_ARRAY) return value_retain(obj);
        ValueArray *items = &args[0]->array;
        int cap = 256, sz = 0;
        char *res = malloc(cap);
        res[0] = '\0';
        size_t dlen = strlen(s);
        for (int i = 0; i < items->len; i++) {
            char *part = value_to_string(items->items[i]);
            size_t plen = strlen(part);
            while (sz + (int)plen + (int)dlen + 2 >= cap) { cap *= 2; res = realloc(res, cap); }
            memcpy(res + sz, part, plen); sz += plen; res[sz] = '\0';
            if (i < items->len - 1) { memcpy(res + sz, s, dlen); sz += dlen; res[sz] = '\0'; }
            free(part);
        }
        return value_string_take(res);
    }
    if (strcmp(method, "isdigit") == 0) {
        bool ok = strlen(s) > 0;
        for (const char *p = s; *p; p++) if (!isdigit(*p)) { ok = false; break; }
        return value_bool(ok ? 1 : 0);
    }
    if (strcmp(method, "isalpha") == 0) {
        bool ok = strlen(s) > 0;
        for (const char *p = s; *p; p++) if (!isalpha(*p)) { ok = false; break; }
        return value_bool(ok ? 1 : 0);
    }
    if (strcmp(method, "isspace") == 0) {
        bool ok = strlen(s) > 0;
        for (const char *p = s; *p; p++) if (!isspace(*p)) { ok = false; break; }
        return value_bool(ok ? 1 : 0);
    }
    if (strcmp(method, "startswith") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_bool(0);
        return value_bool(strncmp(s, args[0]->str_val, strlen(args[0]->str_val)) == 0 ? 1 : 0);
    }
    if (strcmp(method, "endswith") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_bool(0);
        size_t sl = strlen(s), ml = strlen(args[0]->str_val);
        return value_bool((sl >= ml && strcmp(s + sl - ml, args[0]->str_val) == 0) ? 1 : 0);
    }
    if (strcmp(method, "contains") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_bool(0);
        return value_bool(strstr(s, args[0]->str_val) != NULL ? 1 : 0);
    }
    if (strcmp(method, "before") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_string(s);
        const char *found = strstr(s, args[0]->str_val);
        if (!found) return value_string(s);
        return value_string(strndup(s, found - s));
    }
    if (strcmp(method, "after") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_string("");
        const char *found = strstr(s, args[0]->str_val);
        if (!found) return value_string("");
        return value_string(found + strlen(args[0]->str_val));
    }
    if (strcmp(method, "reverse") == 0) {
        size_t len = strlen(s);
        char *r = malloc(len + 1);
        for (size_t i = 0; i < len; i++) r[i] = s[len - 1 - i];
        r[len] = '\0';
        return value_string_take(r);
    }
    if (strcmp(method, "words") == 0) {
        Value *arr = value_array_new();
        char *copy = strdup(s);
        char *tok = strtok(copy, " \t\n\r");
        while (tok) {
            Value *sv = value_string(tok);
            value_array_push(arr, sv); value_release(sv);
            tok = strtok(NULL, " \t\n\r");
        }
        free(copy);
        return arr;
    }
    if (strcmp(method, "lines") == 0) {
        Value *arr = value_array_new();
        char *copy = strdup(s), *rest = copy;
        char *pos;
        while ((pos = strchr(rest, '\n')) != NULL) {
            *pos = '\0';
            Value *sv = value_string(rest); value_array_push(arr, sv); value_release(sv);
            rest = pos + 1;
        }
        Value *sv = value_string(rest); value_array_push(arr, sv); value_release(sv);
        free(copy);
        return arr;
    }
    if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
        return value_int((long long)strlen(s));
    }
    if (strcmp(method, "format") == 0) {
        /* simple positional substitution: "hello {}".format("world") */
        int cap = 256, sz = 0;
        char *buf = malloc(cap);
        buf[0] = '\0';
        const char *p = s;
        int ai = 0;
        while (*p) {
            if (*p == '{' && *(p+1) == '}') {
                const char *sub = (ai < argc) ? (args[ai]->type == VAL_STRING ? args[ai]->str_val : "") : "";
                char *vsub = (ai < argc) ? value_to_string(args[ai]) : strdup("");
                size_t vlen = strlen(vsub);
                while (sz + (int)vlen + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + sz, vsub, vlen); sz += vlen; buf[sz] = '\0';
                free(vsub);
                ai++; p += 2;
            } else {
                if (sz + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[sz++] = *p++; buf[sz] = '\0';
            }
        }
        return value_string_take(buf);
    }
    if (strcmp(method, "count") == 0) {
        if (argc < 1 || args[0]->type != VAL_STRING) return value_int(0);
        const char *needle = args[0]->str_val;
        size_t nlen = strlen(needle);
        long long cnt = 0;
        if (nlen == 0) return value_int(0);
        const char *p = s;
        while ((p = strstr(p, needle)) != NULL) { cnt++; p += nlen; }
        return value_int(cnt);
    }
    if (strcmp(method, "pad_left") == 0 || strcmp(method, "ljust") == 0) {
        if (argc < 1 || args[0]->type != VAL_INT) return value_string(s);
        long long width = args[0]->int_val;
        char fill = (argc >= 2 && args[1]->type == VAL_STRING && args[1]->str_val[0]) ? args[1]->str_val[0] : ' ';
        size_t slen = strlen(s);
        if ((long long)slen >= width) return value_string(s);
        char *r = malloc((size_t)width + 1);
        memset(r, fill, (size_t)width - slen);
        memcpy(r + (size_t)width - slen, s, slen);
        r[width] = '\0';
        return value_string_take(r);
    }
    if (strcmp(method, "pad_right") == 0 || strcmp(method, "rjust") == 0) {
        if (argc < 1 || args[0]->type != VAL_INT) return value_string(s);
        long long width = args[0]->int_val;
        char fill = (argc >= 2 && args[1]->type == VAL_STRING && args[1]->str_val[0]) ? args[1]->str_val[0] : ' ';
        size_t slen = strlen(s);
        if ((long long)slen >= width) return value_string(s);
        char *r = malloc((size_t)width + 1);
        memcpy(r, s, slen);
        memset(r + slen, fill, (size_t)width - slen);
        r[width] = '\0';
        return value_string_take(r);
    }

    char emsg[128];
    snprintf(emsg, sizeof(emsg), "string has no method '%s'", method);
    runtime_error(interp, emsg, line);
    return value_null();
}

/* ------------------------------------------------------------------ array methods */

static Value *array_method(Interpreter *interp, Value *obj, const char *method,
                            Value **args, int argc, int line) {
    if (strcmp(method, "add") == 0) {
        if (argc < 1) { runtime_error(interp, "add() requires 1 argument", line); return value_null(); }
        value_array_push(obj, args[0]);
        return value_null();
    }
    if (strcmp(method, "insert") == 0) {
        if (argc < 2) { runtime_error(interp, "insert() requires 2 arguments", line); return value_null(); }
        long long idx = (args[0]->type == VAL_INT) ? args[0]->int_val : 0;
        value_array_insert(obj, idx, args[1]);
        return value_null();
    }
    if (strcmp(method, "remove") == 0) {
        if (argc < 1) { runtime_error(interp, "remove() requires 1 argument", line); return value_null(); }
        value_array_remove(obj, args[0]);
        return value_null();
    }
    if (strcmp(method, "pop") == 0) {
        long long idx = (argc >= 1 && args[0]->type == VAL_INT) ? args[0]->int_val : -1;
        return value_array_pop(obj, idx);
    }
    if (strcmp(method, "sort") == 0) {
        value_array_sort(obj);
        return value_null();
    }
    if (strcmp(method, "extend") == 0) {
        if (argc >= 1) value_array_extend(obj, args[0]);
        return value_null();
    }
    if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
        return value_int(obj->array.len);
    }
    if (strcmp(method, "count") == 0) {
        if (argc < 1) return value_int(0);
        long long cnt = 0;
        for (int i = 0; i < obj->array.len; i++)
            if (value_equals(obj->array.items[i], args[0])) cnt++;
        return value_int(cnt);
    }
    if (strcmp(method, "index") == 0) {
        if (argc < 1) return value_int(-1);
        for (int i = 0; i < obj->array.len; i++)
            if (value_equals(obj->array.items[i], args[0])) return value_int(i);
        return value_int(-1);
    }
    if (strcmp(method, "clear") == 0) {
        for (int i = 0; i < obj->array.len; i++) value_release(obj->array.items[i]);
        obj->array.len = 0;
        return value_null();
    }
    if (strcmp(method, "contains") == 0) {
        if (argc < 1) return value_bool(0);
        for (int i = 0; i < obj->array.len; i++)
            if (value_equals(obj->array.items[i], args[0])) return value_bool(1);
        return value_bool(0);
    }
    if (strcmp(method, "reverse") == 0) {
        int n = obj->array.len;
        for (int i = 0; i < n / 2; i++) {
            Value *tmp = obj->array.items[i];
            obj->array.items[i] = obj->array.items[n - 1 - i];
            obj->array.items[n - 1 - i] = tmp;
        }
        return value_null();
    }
    if (strcmp(method, "first") == 0) {
        if (obj->array.len == 0) return value_null();
        return value_retain(obj->array.items[0]);
    }
    if (strcmp(method, "last") == 0) {
        if (obj->array.len == 0) return value_null();
        return value_retain(obj->array.items[obj->array.len - 1]);
    }
    if (strcmp(method, "join") == 0) {
        const char *sep = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "";
        int cap = 256, sz = 0;
        char *buf = malloc(cap);
        buf[0] = '\0';
        for (int i = 0; i < obj->array.len; i++) {
            char *piece = value_to_string(obj->array.items[i]);
            size_t plen = strlen(piece);
            size_t slen = (i > 0) ? strlen(sep) : 0;
            while (sz + (int)(plen + slen) + 2 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            if (i > 0) { memcpy(buf + sz, sep, slen); sz += (int)slen; }
            memcpy(buf + sz, piece, plen); sz += (int)plen;
            buf[sz] = '\0';
            free(piece);
        }
        return value_string_take(buf);
    }
    char emsg[128];
    snprintf(emsg, sizeof(emsg), "array has no method '%s'", method);
    runtime_error(interp, emsg, line);
    return value_null();
}

/* ------------------------------------------------------------------ dict methods */

static Value *dict_method(Interpreter *interp, Value *obj, const char *method,
                           Value **args, int argc, int line) {
    (void)args; (void)argc;
    if (strcmp(method, "keys") == 0) {
        Value *arr = value_array_new();
        for (int i = 0; i < obj->dict.len; i++) value_array_push(arr, obj->dict.entries[i].key);
        return arr;
    }
    if (strcmp(method, "values") == 0) {
        Value *arr = value_array_new();
        for (int i = 0; i < obj->dict.len; i++) value_array_push(arr, obj->dict.entries[i].val);
        return arr;
    }
    if (strcmp(method, "items") == 0) {
        Value *arr = value_array_new();
        for (int i = 0; i < obj->dict.len; i++) {
            Value *pair_items[2] = { obj->dict.entries[i].key, obj->dict.entries[i].val };
            Value *pair = value_tuple_new(pair_items, 2);
            value_array_push(arr, pair);
            value_release(pair);
        }
        return arr;
    }
    if (strcmp(method, "erase") == 0) {
        for (int i = 0; i < obj->dict.len; i++) {
            value_release(obj->dict.entries[i].key);
            value_release(obj->dict.entries[i].val);
        }
        obj->dict.len = 0;
        return value_null();
    }
    if (strcmp(method, "get") == 0) {
        if (argc < 1) return value_null();
        Value *found = value_dict_get(obj, args[0]);
        if (!found) return (argc >= 2) ? value_retain(args[1]) : value_null();
        return value_retain(found);
    }
    if (strcmp(method, "has") == 0 || strcmp(method, "contains") == 0) {
        if (argc < 1) return value_bool(0);
        Value *found = value_dict_get(obj, args[0]);
        return value_bool(found ? 1 : 0);
    }
    if (strcmp(method, "remove") == 0 || strcmp(method, "delete") == 0) {
        if (argc < 1) return value_null();
        /* find and remove the entry */
        for (int i = 0; i < obj->dict.len; i++) {
            if (value_equals(obj->dict.entries[i].key, args[0])) {
                value_release(obj->dict.entries[i].key);
                value_release(obj->dict.entries[i].val);
                /* shift remaining entries down */
                for (int j = i; j < obj->dict.len - 1; j++)
                    obj->dict.entries[j] = obj->dict.entries[j + 1];
                obj->dict.len--;
                break;
            }
        }
        return value_null();
    }
    if (strcmp(method, "clear") == 0) {
        for (int i = 0; i < obj->dict.len; i++) {
            value_release(obj->dict.entries[i].key);
            value_release(obj->dict.entries[i].val);
        }
        obj->dict.len = 0;
        return value_null();
    }
    if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
        return value_int(obj->dict.len);
    }
    if (strcmp(method, "set") == 0) {
        if (argc < 2) { runtime_error(interp, "dict.set() requires 2 arguments", line); return value_null(); }
        value_dict_set(obj, args[0], args[1]);
        return value_null();
    }
    char emsg[128];
    snprintf(emsg, sizeof(emsg), "dict has no method '%s'", method);
    runtime_error(interp, emsg, line);
    return value_null();
}

/* ------------------------------------------------------------------ set methods */

static Value *set_method(Interpreter *interp, Value *obj, const char *method,
                          Value **args, int argc, int line) {
    if (strcmp(method, "add") == 0) {
        if (argc < 1) { runtime_error(interp, "add() requires 1 argument", line); return value_null(); }
        value_set_add(obj, args[0]);
        return value_null();
    }
    if (strcmp(method, "update") == 0) {
        for (int a = 0; a < argc; a++) {
            if (args[a]->type == VAL_SET) {
                for (int i = 0; i < args[a]->set.len; i++) value_set_add(obj, args[a]->set.items[i]);
            }
        }
        return value_null();
    }
    if (strcmp(method, "remove") == 0) {
        if (argc < 1) { runtime_error(interp, "remove() requires 1 argument", line); return value_null(); }
        value_set_remove(obj, args[0]);
        return value_null();
    }
    if (strcmp(method, "discard") == 0) {
        if (argc >= 1) value_set_remove(obj, args[0]);
        return value_null();
    }
    if (strcmp(method, "pop") == 0) {
        if (obj->set.len == 0) return value_null();
        Value *item = obj->set.items[0];
        /* remove first */
        memmove(&obj->set.items[0], &obj->set.items[1], (obj->set.len-1)*sizeof(Value*));
        obj->set.len--;
        return item;
    }
    char emsg[128];
    snprintf(emsg, sizeof(emsg), "set has no method '%s'", method);
    runtime_error(interp, emsg, line);
    return value_null();
}

/* ------------------------------------------------------------------ tuple methods */

static Value *tuple_method(Interpreter *interp, Value *obj, const char *method,
                            Value **args, int argc, int line) {
    if (strcmp(method, "count") == 0) {
        if (argc < 1) return value_int(0);
        long long cnt = 0;
        for (int i = 0; i < obj->tuple.len; i++)
            if (value_equals(obj->tuple.items[i], args[0])) cnt++;
        return value_int(cnt);
    }
    if (strcmp(method, "index") == 0) {
        if (argc < 1) return value_int(-1);
        for (int i = 0; i < obj->tuple.len; i++)
            if (value_equals(obj->tuple.items[i], args[0])) return value_int(i);
        return value_int(-1);
    }
    char emsg[128];
    snprintf(emsg, sizeof(emsg), "tuple has no method '%s'", method);
    runtime_error(interp, emsg, line);
    return value_null();
}

/* ------------------------------------------------------------------ slice helper */

static Value *do_slice(Interpreter *interp, Value *obj, Value *vstart, Value *vstop, Value *vstep, int line) {
    long long len = 0;
    if      (obj->type == VAL_STRING) len = (long long)strlen(obj->str_val);
    else if (obj->type == VAL_ARRAY)  len = obj->array.len;
    else if (obj->type == VAL_TUPLE)  len = obj->tuple.len;
    else { runtime_error(interp, "slice not supported on this type", line); return value_null(); }

    long long step  = vstep  ? (vstep->type  == VAL_INT ? vstep->int_val  : 1) : 1;
    long long start = vstart ? (vstart->type == VAL_INT ? vstart->int_val : (step > 0 ? 0 : len-1))
                             : (step > 0 ? 0 : len - 1);
    long long stop  = vstop  ? (vstop->type  == VAL_INT ? vstop->int_val  : (step > 0 ? len : -len-1))
                             : (step > 0 ? len : -len - 1);

    if (step == 0) { runtime_error(interp, "slice step cannot be zero", line); return value_null(); }

    if (start < 0) start += len;
    if (stop  < 0 && vstop) stop += len;
    if (start < 0) start = 0;
    if (stop  > len) stop = len;

    if (obj->type == VAL_STRING) {
        int cap = 64, sz = 0;
        char *buf = malloc(cap);
        buf[0] = '\0';
        for (long long i = start; step > 0 ? i < stop : i > stop; i += step) {
            if (i < 0 || i >= len) continue;
            if (sz + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[sz++] = obj->str_val[i];
        }
        buf[sz] = '\0';
        return value_string_take(buf);
    }

    Value *result;
    ValueArray *src;
    bool is_tuple = (obj->type == VAL_TUPLE);
    if (is_tuple) { src = &obj->tuple; result = NULL; }
    else          { src = &obj->array; result = value_array_new(); }

    Value **items_tmp = NULL; int items_count = 0, items_cap = 8;
    if (is_tuple) items_tmp = malloc(items_cap * sizeof(Value *));

    for (long long i = start; step > 0 ? i < stop : i > stop; i += step) {
        if (i < 0 || i >= src->len) continue;
        if (is_tuple) {
            if (items_count >= items_cap) { items_cap *= 2; items_tmp = realloc(items_tmp, items_cap * sizeof(Value *)); }
            items_tmp[items_count++] = src->items[i];
        } else {
            value_array_push(result, src->items[i]);
        }
    }
    if (is_tuple) {
        result = value_tuple_new(items_tmp, items_count);
        free(items_tmp);
    }
    return result;
}

/* ------------------------------------------------------------------ main eval */

static Value *eval_node(Interpreter *interp, ASTNode *node, Env *env) {
    if (!node || interp->had_error) return value_null();
    if (interp->returning || interp->breaking || interp->continuing) return value_null();

    gc_set_alloc_site(interp->filename, node->line);

    switch (node->type) {

    /* ---- literals ---- */
    case NODE_NULL_LIT:    return value_null();
    case NODE_INT_LIT:     return value_int(node->int_lit.value);
    case NODE_FLOAT_LIT:   return value_float(node->float_lit.value);
    case NODE_COMPLEX_LIT: return value_complex(node->complex_lit.real, node->complex_lit.imag);
    case NODE_BOOL_LIT:    return value_bool(node->bool_lit.value);

    case NODE_STRING_LIT:
        return value_string(node->string_lit.value);

    case NODE_FSTRING_LIT: {
        char *s = process_fstring(interp, node->string_lit.value, env);
        return value_string_take(s);
    }

    /* ---- array / dict / set / tuple literals ---- */
    case NODE_ARRAY_LIT: {
        Value *arr = value_array_new();
        for (int i = 0; i < node->list_lit.count; i++) {
            Value *item = eval_node(interp, node->list_lit.items[i], env);
            if (interp->had_error) { value_release(arr); return value_null(); }
            value_array_push(arr, item);
            value_release(item);
        }
        return arr;
    }

    case NODE_SET_LIT: {
        Value *set = value_set_new();
        for (int i = 0; i < node->list_lit.count; i++) {
            Value *item = eval_node(interp, node->list_lit.items[i], env);
            if (interp->had_error) { value_release(set); return value_null(); }
            value_set_add(set, item);
            value_release(item);
        }
        return set;
    }

    case NODE_TUPLE_LIT: {
        Value **items = malloc((node->list_lit.count + 1) * sizeof(Value *));
        for (int i = 0; i < node->list_lit.count; i++) {
            items[i] = eval_node(interp, node->list_lit.items[i], env);
            if (interp->had_error) {
                for (int j = 0; j < i; j++) value_release(items[j]);
                free(items); return value_null();
            }
        }
        Value *t = value_tuple_new(items, node->list_lit.count);
        for (int i = 0; i < node->list_lit.count; i++) value_release(items[i]);
        free(items);
        return t;
    }

    case NODE_DICT_LIT: {
        Value *dict = value_dict_new();
        for (int i = 0; i < node->dict_lit.count; i++) {
            Value *k = eval_node(interp, node->dict_lit.keys[i], env);
            if (interp->had_error) { value_release(dict); return value_null(); }
            /* Intern string dict keys so pointer-equality comparison is O(1) */
            if (k->type == VAL_STRING && !k->gc_immortal) {
                Value *ik = gc_intern_string(gc_global(), k->str_val);
                value_release(k);
                k = ik;
            }
            Value *v = eval_node(interp, node->dict_lit.vals[i], env);
            if (interp->had_error) { value_release(k); value_release(dict); return value_null(); }
            value_dict_set(dict, k, v);
            value_release(k); value_release(v);
        }
        return dict;
    }

    /* ---- identifier ---- */
    case NODE_IDENT: {
        Value *v = env_get(env, node->ident.name);
        if (!v) {
            char emsg[128];
            snprintf(emsg, sizeof(emsg), "undefined variable '%s'", node->ident.name);
            runtime_error(interp, emsg, node->line);
            return value_null();
        }
        return value_retain(v);
    }

    /* ---- var decl ---- */
    case NODE_VAR_DECL: {
        Value *val = node->var_decl.init
                        ? eval_node(interp, node->var_decl.init, env)
                        : value_null();
        if (interp->had_error) { value_release(val); return value_null(); }
        env_set(env, node->var_decl.name, val, node->var_decl.is_const);
        value_release(val);
        return value_null();
    }

    /* ---- assignment ---- */
    case NODE_ASSIGN: {
        Value *val = eval_node(interp, node->assign.value, env);
        if (interp->had_error) return value_null();

        ASTNode *target = node->assign.target;

        if (target->type == NODE_IDENT) {
            if (!env_assign(env, target->ident.name, val)) {
                char msg[256];
                if (env_is_const(env, target->ident.name)) {
                    snprintf(msg, sizeof(msg),
                        "cannot assign to '%s': it was declared as a constant",
                        target->ident.name);
                } else {
                    snprintf(msg, sizeof(msg),
                        "variable '%s' is not declared; use 'let %s = ...' to declare it",
                        target->ident.name, target->ident.name);
                }
                runtime_error(interp, msg, node->line);
            }
        } else if (target->type == NODE_INDEX) {
            Value *obj = eval_node(interp, target->index_expr.obj, env);
            Value *idx = eval_node(interp, target->index_expr.index, env);
            if (!interp->had_error) {
                if (obj->type == VAL_ARRAY) {
                    long long i = idx->int_val;
                    if (i < 0) i += obj->array.len;
                    if (i >= 0 && i < obj->array.len) {
                        value_release(obj->array.items[i]);
                        obj->array.items[i] = value_retain(val);
                    } else {
                        runtime_error(interp, "array index out of range", node->line);
                    }
                } else if (obj->type == VAL_DICT) {
                    value_dict_set(obj, idx, val);
                } else {
                    runtime_error(interp, "cannot index-assign this type", node->line);
                }
            }
            value_release(obj); value_release(idx);
        } else if (target->type == NODE_MEMBER) {
            /* dict member assignment: obj.key = val treated as obj["key"] = val */
            Value *obj = eval_node(interp, target->member.obj, env);
            if (!interp->had_error && obj->type == VAL_DICT) {
                Value *k = value_string(target->member.name);
                value_dict_set(obj, k, val);
                value_release(k);
            }
            value_release(obj);
        }

        value_release(val);
        return value_null();
    }

    /* ---- compound assignment ---- */
    case NODE_COMPOUND_ASSIGN: {
        ASTNode *target = node->assign.target;
        Value *cur_val  = eval_node(interp, target, env);
        Value *rhs      = eval_node(interp, node->assign.value, env);
        if (interp->had_error) { value_release(cur_val); value_release(rhs); return value_null(); }

        Value *result = NULL;
        const char *op = node->assign.op;
        if      (op[0] == '+') result = value_add(cur_val, rhs);
        else if (op[0] == '-') result = value_sub(cur_val, rhs);
        else if (op[0] == '*') result = value_mul(cur_val, rhs);
        else if (op[0] == '/') result = value_div(cur_val, rhs);

        value_release(cur_val); value_release(rhs);
        if (!result) { runtime_error(interp, "invalid compound assignment", node->line); return value_null(); }

        if (target->type == NODE_IDENT) {
            if (!env_assign(env, target->ident.name, result)) {
                char msg[256];
                if (env_is_const(env, target->ident.name)) {
                    snprintf(msg, sizeof(msg),
                        "cannot assign to '%s': it was declared as a constant",
                        target->ident.name);
                } else {
                    snprintf(msg, sizeof(msg),
                        "variable '%s' is not declared; use 'let %s = ...' to declare it",
                        target->ident.name, target->ident.name);
                }
                runtime_error(interp, msg, node->line);
            }
        }
        value_release(result);
        return value_null();
    }

    /* ---- block / program ---- */
    case NODE_PROGRAM:
    case NODE_BLOCK: {
        Env *block_env = (node->type == NODE_BLOCK) ? env_new(env) : env;
        Value *last = value_null();
        for (int i = 0; i < node->block.count; i++) {
            if (interp->had_error || interp->returning || interp->breaking || interp->continuing) break;
            value_release(last);
            last = eval_node(interp, node->block.stmts[i], block_env);
        }
        if (node->type == NODE_BLOCK) env_free(block_env);
        return last;
    }

    case NODE_EXPR_STMT: {
        Value *v = eval_node(interp, node->expr_stmt.expr, env);
        return v;
    }

    /* ---- control flow ---- */
    case NODE_RETURN: {
        Value *val = node->ret.value
                        ? eval_node(interp, node->ret.value, env)
                        : value_null();
        if (!interp->had_error) {
            if (interp->return_val) value_release(interp->return_val);
            interp->return_val = val;
            interp->returning  = true;
        } else {
            value_release(val);
        }
        return value_null();
    }

    case NODE_BREAK:
        interp->breaking = true;
        return value_null();

    case NODE_CONTINUE:
        interp->continuing = true;
        return value_null();

    /* ---- import ---- */
    case NODE_IMPORT: {
        const char *raw_path = node->import_stmt.path;

        /* Probe order: as-is, .pm, .pr, lib/<name>, lib/<name>.pm, lib/<name>.pr
         * This lets users write `import math` and have it resolve to lib/math.pr. */
        static const char *im_exts[]    = { "", ".pm", ".pr", NULL };
        static const char *im_prefixes[] = { "", "lib/", NULL };

        FILE *f = NULL;
        char resolved[512] = {0};
        for (int pi = 0; im_prefixes[pi] && !f; pi++) {
            for (int ei = 0; im_exts[ei] != NULL && !f; ei++) {
                snprintf(resolved, sizeof(resolved), "%s%s%s",
                         im_prefixes[pi], raw_path, im_exts[ei]);
                f = fopen(resolved, "r");
            }
        }
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "cannot import '%s': file not found", raw_path);
            runtime_error(interp, msg, node->line);
            return value_null();
        }

        /* Read source */
        fseek(f, 0, SEEK_END);
        long sz = ftell(f); rewind(f);
        char *src = malloc((size_t)sz + 1);
        { size_t _nr = fread(src, 1, (size_t)sz, f); (void)_nr; }
        src[sz] = '\0'; fclose(f);

        /* Parse */
        char errbuf[512] = {0};
        ASTNode *prog = parser_parse_source(src, errbuf, sizeof(errbuf));
        free(src);
        if (!prog) {
            char msg[512];
            snprintf(msg, sizeof(msg), "import '%s': %s", raw_path, errbuf);
            runtime_error(interp, msg, node->line);
            return value_null();
        }

        /* Execute in an isolated module environment whose parent is globals.
         * This gives module code access to all builtins without inheriting the
         * caller's local variables.  Functions defined here capture mod_env as
         * their closure; env_clear_slots + env_free below break the cycle once
         * we've extracted what we need. */
        Env *mod_env = env_new(interp->globals);
        Value *run_result = eval_node(interp, prog, mod_env);
        /* NOTE: prog is intentionally NOT freed here.  Function values created
         * during module execution store direct pointers into the AST (body,
         * params).  Freeing the AST while those functions are alive would leave
         * dangling pointers.  The module AST is a bounded allocation that lives
         * for the duration of the program — it is freed implicitly by the OS
         * when the process exits.  This is acceptable because import is a
         * one-time, program-startup operation. */
        value_release(run_result);

        if (interp->had_error) {
            env_free(mod_env);
            return value_null();
        }

        const char *symbol = node->import_stmt.symbol;
        const char *alias  = node->import_stmt.alias;

        if (symbol) {
            /* from X import Y [as Z] — bind a single name into the caller's env */
            Value *val = env_get(mod_env, symbol);
            if (!val) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "cannot import '%s' from '%s': name not found",
                         symbol, raw_path);
                runtime_error(interp, msg, node->line);
                env_free(mod_env);
                return value_null();
            }
            const char *bind_name = alias ? alias : symbol;
            env_set(env, bind_name, val, false);
        } else {
            /* import X [as alias] — wrap all module bindings in a dict namespace.
             * Functions in the dict retain mod_env as their closure so that they
             * can still call sibling functions defined in the same module. */
            Value *mod_dict = value_dict_new();
            for (int i = 0; i < mod_env->cap; i++) {
                if (!mod_env->slots[i].key) continue;
                Value *k = value_string(mod_env->slots[i].key);
                value_dict_set(mod_dict, k, mod_env->slots[i].val);
                value_release(k);
            }

            /* Derive binding name from the last path component, stripped of
             * any extension: "lib/math.pr" → "math", "utils" → "utils" */
            char mod_name[256];
            const char *base = strrchr(resolved, '/');
            base = base ? base + 1 : resolved;
            snprintf(mod_name, sizeof(mod_name), "%s", base);
            char *dot = strrchr(mod_name, '.');
            if (dot) *dot = '\0';

            const char *bind_name = alias ? alias : mod_name;
            env_set(env, bind_name, mod_dict, false);
            value_release(mod_dict);
        }

        /* Drop our initial reference to mod_env.  Module functions keep it alive
         * via their own env_retain (captured closure), so this is safe.  The env
         * will be freed when all functions referencing it are released. */
        env_free(mod_env);
        return value_null();
    }

    /* ---- if ---- */
    case NODE_IF: {
        for (int i = 0; i < node->if_stmt.branch_count; i++) {
            Value *cond = eval_node(interp, node->if_stmt.conds[i], env);
            bool ok = value_truthy(cond);
            value_release(cond);
            if (ok) {
                Value *r = eval_node(interp, node->if_stmt.bodies[i], env);
                return r;
            }
        }
        if (node->if_stmt.else_body)
            return eval_node(interp, node->if_stmt.else_body, env);
        return value_null();
    }

    /* ---- while ---- */
    case NODE_WHILE: {
        Value *result = value_null();
        while (true) {
            if (interp->had_error || interp->returning) break;
            Value *cond = eval_node(interp, node->while_stmt.cond, env);
            bool ok = value_truthy(cond);
            value_release(cond);
            if (!ok) break;
            value_release(result);
            result = eval_node(interp, node->while_stmt.body, env);
            if (interp->breaking)   { interp->breaking = false; break; }
            if (interp->continuing) { interp->continuing = false; }
        }
        return result;
    }

    /* ---- for in ---- */
    case NODE_FOR_IN: {
        Value *iter = eval_node(interp, node->for_in.iter, env);
        if (interp->had_error) return value_null();

        Value *result = value_null();
        Env *loop_env = env_new(env);

        #define FOR_BODY(item)                                            \
            do {                                                          \
                env_set(loop_env, node->for_in.var, (item), false);      \
                value_release(result);                                    \
                result = eval_node(interp, node->for_in.body, loop_env); \
                if (interp->breaking)   { interp->breaking = false; goto for_done; } \
                if (interp->continuing) { interp->continuing = false; }  \
                if (interp->returning || interp->had_error) goto for_done; \
            } while (0)

        if (iter->type == VAL_ARRAY) {
            for (int i = 0; i < iter->array.len; i++) FOR_BODY(iter->array.items[i]);
        } else if (iter->type == VAL_TUPLE) {
            for (int i = 0; i < iter->tuple.len; i++) FOR_BODY(iter->tuple.items[i]);
        } else if (iter->type == VAL_SET) {
            for (int i = 0; i < iter->set.len; i++) FOR_BODY(iter->set.items[i]);
        } else if (iter->type == VAL_STRING) {
            const char *s = iter->str_val;
            for (int i = 0; s[i]; i++) {
                char ch[2] = {s[i], '\0'};
                Value *sv = value_string(ch);
                FOR_BODY(sv);
                value_release(sv);
            }
        } else if (iter->type == VAL_DICT) {
            for (int i = 0; i < iter->dict.len; i++) FOR_BODY(iter->dict.entries[i].key);
        } else {
            runtime_error(interp, "value is not iterable", node->line);
        }
        #undef FOR_BODY

    for_done:
        env_free(loop_env);
        value_release(iter);
        return result;
    }

    /* ---- func decl ---- */
    case NODE_FUNC_DECL: {
        Value *fn = value_function(node->func_decl.name,
                                   node->func_decl.params,
                                   node->func_decl.param_count,
                                   node->func_decl.body,
                                   env);
        env_set(env, node->func_decl.name, fn, false);
        value_release(fn);
        return value_null();
    }

    /* ---- func call ---- */
    case NODE_FUNC_CALL: {
        Value *callee = eval_node(interp, node->func_call.callee, env);
        if (interp->had_error) return value_null();

        /* Evaluate args, expanding any spread (...expr) arguments inline. */
        int raw_argc = node->func_call.arg_count;
        int max_args = raw_argc + 64; /* extra room for spread expansion */
        Value **args = malloc((size_t)max_args * sizeof(Value *));
        int actual_argc = 0;
        for (int i = 0; i < raw_argc; i++) {
            ASTNode *arg_node = node->func_call.args[i];
            if (arg_node->type == NODE_SPREAD) {
                /* spread: evaluate inner, then flatten array items */
                Value *spread_val = eval_node(interp, arg_node->spread.expr, env);
                if (interp->had_error) {
                    for (int j = 0; j < actual_argc; j++) value_release(args[j]);
                    value_release(spread_val); free(args); value_release(callee); return value_null();
                }
                if (spread_val->type == VAL_ARRAY) {
                    int need = actual_argc + spread_val->array.len;
                    if (need > max_args) {
                        max_args = need + 8;
                        args = realloc(args, (size_t)max_args * sizeof(Value *));
                    }
                    for (int j = 0; j < spread_val->array.len; j++)
                        args[actual_argc++] = value_retain(spread_val->array.items[j]);
                    value_release(spread_val);
                } else {
                    args[actual_argc++] = spread_val;
                }
            } else {
                if (actual_argc >= max_args) {
                    max_args += 8;
                    args = realloc(args, (size_t)max_args * sizeof(Value *));
                }
                args[actual_argc] = eval_node(interp, arg_node, env);
                if (interp->had_error) {
                    for (int j = 0; j < actual_argc; j++) value_release(args[j]);
                    free(args); value_release(callee); return value_null();
                }
                actual_argc++;
            }
        }
        Value *result = value_null();

        if (callee->type == VAL_BUILTIN) {
            value_release(result);
            g_current_interp = interp;
            result = callee->builtin.fn(args, actual_argc);
            g_current_interp = NULL;
        } else if (callee->type == VAL_FUNCTION) {
            Env *fn_env = env_new(callee->func.closure);
            for (int i = 0; i < callee->func.param_count; i++) {
                const char *pname = callee->func.params[i].name;
                /* varargs: collect remaining call args into an array */
                if (pname[0] == '.' && pname[1] == '.' && pname[2] == '.') {
                    const char *real_name = pname + 3;
                    Value *vararg_arr = value_array_new();
                    for (int j = i; j < actual_argc; j++)
                        value_array_push(vararg_arr, args[j]);
                    env_set(fn_env, real_name, vararg_arr, false);
                    value_release(vararg_arr);
                    break; /* varargs must be last param */
                }
                Value *arg;
                if (i < actual_argc) {
                    arg = args[i];
                } else if (callee->func.params[i].default_val) {
                    /* evaluate default value expression in the function's closure */
                    arg = eval_node(interp, callee->func.params[i].default_val, callee->func.closure);
                    if (interp->had_error) { env_free(fn_env); goto call_cleanup; }
                } else {
                    arg = value_null();
                }
                env_set(fn_env, pname, arg, false);
                if (i >= actual_argc && !callee->func.params[i].default_val)
                    value_release(arg);
                else if (i >= actual_argc && callee->func.params[i].default_val)
                    value_release(arg);
            }

            bool prev_ret = interp->returning;
            interp->returning = false;

            value_release(eval_node(interp, callee->func.body, fn_env));

            if (interp->returning) {
                value_release(result);
                result = interp->return_val ? interp->return_val : value_null();
                interp->return_val = NULL;
                interp->returning  = false;
            }
            interp->returning = prev_ret && !interp->returning ? false : interp->returning;
            env_free(fn_env);
        } else {
            runtime_error(interp, "value is not callable", node->line);
        }

    call_cleanup:
        for (int i = 0; i < actual_argc; i++) value_release(args[i]);
        free(args);
        value_release(callee);
        return result;
    }

    /* ---- method call ---- */
    case NODE_METHOD_CALL: {
        Value *obj = eval_node(interp, node->method_call.obj, env);
        if (interp->had_error) return value_null();

        Value **args = malloc((node->method_call.arg_count + 1) * sizeof(Value *));
        for (int i = 0; i < node->method_call.arg_count; i++) {
            args[i] = eval_node(interp, node->method_call.args[i], env);
            if (interp->had_error) {
                for (int j = 0; j < i; j++) value_release(args[j]);
                free(args); value_release(obj); return value_null();
            }
        }

        Value *result;
        if (is_memory_module(obj)) {
            result = memory_method(interp, env, obj, node->method_call.method, args, node->method_call.arg_count, node->line);
        } else if (obj->type == VAL_DICT) {
            /* Check if it's a class instance (has __instance_of__) */
            Value *iof_key = value_string("__instance_of__");
            Value *iof = value_dict_get(obj, iof_key);
            value_release(iof_key);

            Value *method_fn = NULL;
            if (iof && iof->type == VAL_DICT) {
                /* Look for method in class dict */
                Value *mk = value_string(node->method_call.method);
                method_fn = value_dict_get(iof, mk);
                value_release(mk);
                /* Also check superclass chain */
                if (!method_fn) {
                    Value *super_key = value_string("__super__");
                    Value *super_name = value_dict_get(iof, super_key);
                    value_release(super_key);
                    if (super_name && super_name->type == VAL_STRING) {
                        Value *super_cls = env_get(env, super_name->str_val);
                        if (super_cls && super_cls->type == VAL_DICT) {
                            Value *smk = value_string(node->method_call.method);
                            method_fn = value_dict_get(super_cls, smk);
                            value_release(smk);
                        }
                    }
                }
            }

            if (method_fn && method_fn->type == VAL_FUNCTION) {
                /* Call the method with self=obj injected */
                Env *call_env = env_new(method_fn->func.closure);
                env_set(call_env, "self", obj, false);
                /* bind parameters (skip self if first param is named self) */
                int arg_offset = 0;
                if (method_fn->func.param_count > 0 &&
                    strcmp(method_fn->func.params[0].name, "self") == 0) {
                    arg_offset = 1;
                }
                for (int pi = arg_offset; pi < method_fn->func.param_count; pi++) {
                    const char *pname = method_fn->func.params[pi].name;
                    int ai = pi - arg_offset;
                    /* varargs: collect remaining args into an array */
                    if (pname[0] == '.' && pname[1] == '.' && pname[2] == '.') {
                        const char *real_name = pname + 3;
                        Value *vararg_arr = value_array_new();
                        for (int j = ai; j < node->method_call.arg_count; j++)
                            value_array_push(vararg_arr, value_retain(args[j]));
                        env_set(call_env, real_name, vararg_arr, false);
                        value_release(vararg_arr);
                        break;
                    }
                    Value *argval;
                    if (ai < node->method_call.arg_count) {
                        argval = value_retain(args[ai]);
                    } else if (method_fn->func.params[pi].default_val) {
                        argval = eval_node(interp, method_fn->func.params[pi].default_val, env);
                    } else {
                        argval = value_null();
                    }
                    env_set(call_env, pname, argval, false);
                    value_release(argval);
                }
                eval_node(interp, method_fn->func.body, call_env);
                result = value_null();
                if (interp->returning) {
                    interp->returning = false;
                    if (interp->return_val) {
                        value_release(result);
                        result = interp->return_val;
                        interp->return_val = NULL;
                    }
                }
                env_free(call_env);
            } else {
                /* Not a class instance.
                 * First check whether the dict has a user-defined function stored
                 * at this key — this is the module-namespace pattern where
                 *   import math  →  math is a dict  →  math.clamp(5,0,3)
                 * If the key holds a callable, invoke it without self injection.
                 * Otherwise fall through to built-in dict method dispatch. */
                Value *ns_mk  = value_string(node->method_call.method);
                Value *ns_fn  = value_dict_get(obj, ns_mk);
                value_release(ns_mk);

                if (ns_fn && ns_fn->type == VAL_BUILTIN) {
                    g_current_interp = interp;
                    result = ns_fn->builtin.fn(args, node->method_call.arg_count);
                    g_current_interp = NULL;
                    if (!result) result = value_null();
                } else if (ns_fn && ns_fn->type == VAL_FUNCTION) {
                    Env *ns_env = env_new(ns_fn->func.closure);
                    for (int pi = 0; pi < ns_fn->func.param_count; pi++) {
                        const char *pname = ns_fn->func.params[pi].name;
                        if (pname[0] == '.' && pname[1] == '.' && pname[2] == '.') {
                            const char *real_name = pname + 3;
                            Value *va = value_array_new();
                            for (int j = pi; j < node->method_call.arg_count; j++)
                                value_array_push(va, value_retain(args[j]));
                            env_set(ns_env, real_name, va, false);
                            value_release(va);
                            break;
                        }
                        Value *av;
                        if (pi < node->method_call.arg_count) {
                            av = value_retain(args[pi]);
                        } else if (ns_fn->func.params[pi].default_val) {
                            av = eval_node(interp, ns_fn->func.params[pi].default_val,
                                           ns_fn->func.closure);
                            if (interp->had_error) { env_free(ns_env); result = value_null(); goto method_cleanup; }
                        } else {
                            av = value_null();
                        }
                        env_set(ns_env, pname, av, false);
                        value_release(av);
                    }
                    bool prev_ret2 = interp->returning;
                    interp->returning = false;
                    value_release(eval_node(interp, ns_fn->func.body, ns_env));
                    result = value_null();
                    if (interp->returning) {
                        value_release(result);
                        result = interp->return_val ? interp->return_val : value_null();
                        interp->return_val = NULL;
                        interp->returning  = false;
                    }
                    interp->returning = prev_ret2 && !interp->returning ? false
                                                                        : interp->returning;
                    env_free(ns_env);
                } else {
                    /* Built-in dict methods: keys(), values(), has(), etc. */
                    result = dict_method(interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line);
                    if (result == NULL) result = value_null();
                }
            }
        } else {
            switch (obj->type) {
                case VAL_STRING: result = string_method(interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
                case VAL_ARRAY:  result = array_method (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
                case VAL_SET:    result = set_method   (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
                case VAL_TUPLE:  result = tuple_method (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
                default: {
                    char emsg[64]; snprintf(emsg, sizeof(emsg), "type '%s' has no methods", value_type_name(obj->type));
                    runtime_error(interp, emsg, node->line);
                    result = value_null();
                }
            }
        }

    method_cleanup:
        for (int i = 0; i < node->method_call.arg_count; i++) value_release(args[i]);
        free(args);
        value_release(obj);
        return result;
    }

    /* ---- indexing ---- */
    case NODE_INDEX: {
        Value *obj = eval_node(interp, node->index_expr.obj, env);
        Value *idx = eval_node(interp, node->index_expr.index, env);
        if (interp->had_error) { value_release(obj); value_release(idx); return value_null(); }

        Value *result = value_null();
        if (obj->type == VAL_ARRAY) {
            if (idx->type != VAL_INT) { runtime_error(interp, "array index must be an integer", node->line); }
            else {
                Value *item = value_array_get(obj, idx->int_val);
                if (!item) runtime_error(interp, "array index out of range", node->line);
                else { value_release(result); result = value_retain(item); }
            }
        } else if (obj->type == VAL_TUPLE) {
            if (idx->type != VAL_INT) { runtime_error(interp, "tuple index must be an integer", node->line); }
            else {
                long long i = idx->int_val;
                if (i < 0) i += obj->tuple.len;
                if (i < 0 || i >= obj->tuple.len) runtime_error(interp, "tuple index out of range", node->line);
                else { value_release(result); result = value_retain(obj->tuple.items[i]); }
            }
        } else if (obj->type == VAL_STRING) {
            if (idx->type != VAL_INT) { runtime_error(interp, "string index must be an integer", node->line); }
            else {
                long long i = idx->int_val;
                long long slen = (long long)strlen(obj->str_val);
                if (i < 0) i += slen;
                if (i < 0 || i >= slen) runtime_error(interp, "string index out of range", node->line);
                else { char ch[2] = {obj->str_val[i], '\0'}; value_release(result); result = value_string(ch); }
            }
        } else if (obj->type == VAL_DICT) {
            Value *found = value_dict_get(obj, idx);
            if (!found) { value_release(result); result = value_null(); }
            else { value_release(result); result = value_retain(found); }
        } else {
            runtime_error(interp, "value is not indexable", node->line);
        }
        value_release(obj); value_release(idx);
        return result;
    }

    /* ---- slice ---- */
    case NODE_SLICE: {
        Value *obj   = eval_node(interp, node->slice_expr.obj, env);
        Value *start = node->slice_expr.start ? eval_node(interp, node->slice_expr.start, env) : NULL;
        Value *stop  = node->slice_expr.stop  ? eval_node(interp, node->slice_expr.stop,  env) : NULL;
        Value *step  = node->slice_expr.step  ? eval_node(interp, node->slice_expr.step,  env) : NULL;
        if (interp->had_error) {
            value_release(obj); value_release(start); value_release(stop); value_release(step);
            return value_null();
        }
        Value *result = do_slice(interp, obj, start, stop, step, node->line);
        value_release(obj); value_release(start); value_release(stop); value_release(step);
        return result;
    }

    /* ---- member access ---- */
    case NODE_MEMBER: {
        Value *obj = eval_node(interp, node->member.obj, env);
        if (interp->had_error) return value_null();
        Value *result = value_null();
        if (obj->type == VAL_DICT) {
            Value *key = value_string(node->member.name);
            Value *found = value_dict_get(obj, key);
            value_release(key);
            if (found) {
                value_release(result);
                result = value_retain(found);
            } else {
                /* try class dict lookup for class attributes / methods */
                Value *iof_key = value_string("__instance_of__");
                Value *iof = value_dict_get(obj, iof_key);
                value_release(iof_key);
                if (iof && iof->type == VAL_DICT) {
                    Value *ck = value_string(node->member.name);
                    Value *cv = value_dict_get(iof, ck);
                    value_release(ck);
                    if (cv) { value_release(result); result = value_retain(cv); }
                }
            }
        }
        value_release(obj);
        return result;
    }

    /* ---- in / not in ---- */
    case NODE_IN_EXPR: {
        Value *item = eval_node(interp, node->in_expr.item, env);
        Value *cont = eval_node(interp, node->in_expr.container, env);
        if (interp->had_error) { value_release(item); value_release(cont); return value_null(); }
        bool found = false;
        if (cont->type == VAL_STRING && item->type == VAL_STRING) {
            found = strstr(cont->str_val, item->str_val) != NULL;
        } else if (cont->type == VAL_ARRAY) {
            for (int i = 0; i < cont->array.len; i++) { if (value_equals(cont->array.items[i], item)) { found = true; break; } }
        } else if (cont->type == VAL_SET) {
            found = value_set_has(cont, item);
        } else if (cont->type == VAL_TUPLE) {
            for (int i = 0; i < cont->tuple.len; i++) { if (value_equals(cont->tuple.items[i], item)) { found = true; break; } }
        } else if (cont->type == VAL_DICT) {
            for (int i = 0; i < cont->dict.len; i++) { if (value_equals(cont->dict.entries[i].key, item)) { found = true; break; } }
        }
        value_release(item); value_release(cont);
        return value_bool(found ? 1 : 0);
    }

    case NODE_NOT_IN_EXPR: {
        Value *item = eval_node(interp, node->in_expr.item, env);
        Value *cont = eval_node(interp, node->in_expr.container, env);
        if (interp->had_error) { value_release(item); value_release(cont); return value_null(); }
        bool found = false;
        if (cont->type == VAL_STRING && item->type == VAL_STRING) {
            found = strstr(cont->str_val, item->str_val) != NULL;
        } else if (cont->type == VAL_ARRAY) {
            for (int i = 0; i < cont->array.len; i++) { if (value_equals(cont->array.items[i], item)) { found = true; break; } }
        } else if (cont->type == VAL_SET) {
            found = value_set_has(cont, item);
        }
        value_release(item); value_release(cont);
        return value_bool(found ? 0 : 1);
    }

    /* ---- unary ops ---- */
    case NODE_UNOP: {
        Value *operand = eval_node(interp, node->unop.operand, env);
        if (interp->had_error) return value_null();
        const char *op = node->unop.op;
        Value *result  = value_null();

        if (strcmp(op, "-") == 0) {
            Value *neg = value_neg(operand);
            if (!neg) runtime_error(interp, "unary '-' not supported on this type", node->line);
            else { value_release(result); result = neg; }
        } else if (strcmp(op, "!") == 0 || strcmp(op, "not") == 0) {
            bool t = value_truthy(operand);
            value_release(result);
            result = value_bool(t ? 0 : 1);
        } else if (strcmp(op, "~") == 0) {
            if (operand->type == VAL_INT) { value_release(result); result = value_int(~operand->int_val); }
            else runtime_error(interp, "bitwise NOT requires integer", node->line);
        }
        value_release(operand);
        return result;
    }

    /* ---- binary ops ---- */
    case NODE_BINOP: {
        const char *op = node->binop.op;

        /* short-circuit logical */
        if (strcmp(op, "&&") == 0) {
            Value *left = eval_node(interp, node->binop.left, env);
            if (!value_truthy(left)) return left;
            value_release(left);
            return eval_node(interp, node->binop.right, env);
        }
        if (strcmp(op, "||") == 0) {
            Value *left = eval_node(interp, node->binop.left, env);
            if (value_truthy(left)) return left;
            value_release(left);
            return eval_node(interp, node->binop.right, env);
        }

        Value *left  = eval_node(interp, node->binop.left,  env);
        Value *right = eval_node(interp, node->binop.right, env);
        if (interp->had_error) { value_release(left); value_release(right); return value_null(); }

        Value *result = value_null();

        if (strcmp(op, "+")  == 0) { Value *r = value_add(left, right); if (r) { value_release(result); result = r; } else runtime_error(interp, "unsupported operands for '+'", node->line); }
        else if (strcmp(op, "-") == 0) { Value *r = value_sub(left, right); if (r) { value_release(result); result = r; } else runtime_error(interp, "unsupported operands for '-'", node->line); }
        else if (strcmp(op, "*") == 0) { Value *r = value_mul(left, right); if (r) { value_release(result); result = r; } else runtime_error(interp, "unsupported operands for '*'", node->line); }
        else if (strcmp(op, "/") == 0) { Value *r = value_div(left, right); if (r) { value_release(result); result = r; } else runtime_error(interp, "division by zero or unsupported type", node->line); }
        else if (strcmp(op, "%") == 0) { Value *r = value_mod(left, right); if (r) { value_release(result); result = r; } else runtime_error(interp, "modulo error", node->line); }
        else if (strcmp(op, "**") == 0) { Value *r = value_pow(left, right); if (r) { value_release(result); result = r; } else runtime_error(interp, "power error", node->line); }
        else if (strcmp(op, "//") == 0) {
            if (left->type == VAL_INT && right->type == VAL_INT) {
                if (right->int_val == 0) { runtime_error(interp, "floor division by zero", node->line); }
                else { value_release(result); result = value_int(left->int_val / right->int_val); }
            } else if ((left->type == VAL_FLOAT || left->type == VAL_INT) &&
                       (right->type == VAL_FLOAT || right->type == VAL_INT)) {
                double lv = left->type  == VAL_FLOAT ? left->float_val  : (double)left->int_val;
                double rv = right->type == VAL_FLOAT ? right->float_val : (double)right->int_val;
                if (rv == 0.0) { runtime_error(interp, "floor division by zero", node->line); }
                else { value_release(result); result = value_float(floor(lv / rv)); }
            } else { runtime_error(interp, "'//' requires numeric operands", node->line); }
        }
        else if (strcmp(op, "==") == 0) { value_release(result); result = value_bool(value_equals(left, right) ? 1 : 0); }
        else if (strcmp(op, "!=") == 0) { value_release(result); result = value_bool(value_equals(left, right) ? 0 : 1); }
        else if (strcmp(op, "<")  == 0) { value_release(result); result = value_bool(value_compare(left, right) < 0  ? 1 : 0); }
        else if (strcmp(op, ">")  == 0) { value_release(result); result = value_bool(value_compare(left, right) > 0  ? 1 : 0); }
        else if (strcmp(op, "<=") == 0) { value_release(result); result = value_bool(value_compare(left, right) <= 0 ? 1 : 0); }
        else if (strcmp(op, ">=") == 0) { value_release(result); result = value_bool(value_compare(left, right) >= 0 ? 1 : 0); }
        else if (strcmp(op, "&")  == 0) {
            if (left->type == VAL_INT && right->type == VAL_INT) { value_release(result); result = value_int(left->int_val & right->int_val); }
            else if (left->type == VAL_SET && right->type == VAL_SET) {
                value_release(result); result = value_set_new();
                for (int i = 0; i < left->set.len; i++)
                    if (value_set_has(right, left->set.items[i])) value_set_add(result, left->set.items[i]);
            }
        }
        else if (strcmp(op, "|")  == 0) {
            if (left->type == VAL_INT && right->type == VAL_INT) { value_release(result); result = value_int(left->int_val | right->int_val); }
            else if (left->type == VAL_SET && right->type == VAL_SET) {
                value_release(result); result = value_copy(left);
                for (int i = 0; i < right->set.len; i++) value_set_add(result, right->set.items[i]);
            }
        }
        else if (strcmp(op, "^")  == 0) {
            if (left->type == VAL_INT && right->type == VAL_INT) { value_release(result); result = value_int(left->int_val ^ right->int_val); }
            else if (left->type == VAL_SET && right->type == VAL_SET) {
                value_release(result); result = value_set_new();
                for (int i = 0; i < left->set.len; i++)
                    if (!value_set_has(right, left->set.items[i])) value_set_add(result, left->set.items[i]);
                for (int i = 0; i < right->set.len; i++)
                    if (!value_set_has(left, right->set.items[i])) value_set_add(result, right->set.items[i]);
            }
        }
        else if (strcmp(op, "<<") == 0) {
            if (left->type == VAL_INT && right->type == VAL_INT) {
                long long sh = right->int_val;
                value_release(result);
                result = (sh >= 0 && sh < 64) ? value_int(left->int_val << sh) : value_int(0);
            } else { runtime_error(interp, "'<<' requires integer operands", node->line); }
        }
        else if (strcmp(op, ">>") == 0) {
            if (left->type == VAL_INT && right->type == VAL_INT) {
                long long sh = right->int_val;
                value_release(result);
                result = (sh >= 0 && sh < 64) ? value_int(left->int_val >> sh) : value_int(0);
            } else { runtime_error(interp, "'>>' requires integer operands", node->line); }
        }
        else {
            char emsg[64]; snprintf(emsg, sizeof(emsg), "unknown operator '%s'", op);
            runtime_error(interp, emsg, node->line);
        }

        value_release(left); value_release(right);
        return result;
    }

    /* ---- repeat ---- */
    case NODE_REPEAT: {
        Value *result = value_null();
        Env *loop_env = env_new(env);

        if (node->repeat_stmt.count) {
            /* repeat N times */
            Value *cnt_val = eval_node(interp, node->repeat_stmt.count, env);
            if (interp->had_error) { env_free(loop_env); value_release(cnt_val); return value_null(); }
            long long n = (cnt_val->type == VAL_INT) ? cnt_val->int_val : 0;
            value_release(cnt_val);
            for (long long i = 0; i < n; i++) {
                value_release(result);
                result = eval_node(interp, node->repeat_stmt.body, loop_env);
                if (interp->breaking)   { interp->breaking = false; break; }
                if (interp->continuing) { interp->continuing = false; continue; }
                if (interp->returning || interp->had_error) break;
            }
        } else {
            /* repeat while / repeat until */
            while (true) {
                Value *cond = eval_node(interp, node->repeat_stmt.cond, env);
                if (interp->had_error) { value_release(cond); break; }
                bool ok = value_truthy(cond);
                value_release(cond);
                if (node->repeat_stmt.until ? ok : !ok) break;
                value_release(result);
                result = eval_node(interp, node->repeat_stmt.body, loop_env);
                if (interp->breaking)   { interp->breaking = false; break; }
                if (interp->continuing) { interp->continuing = false; continue; }
                if (interp->returning || interp->had_error) break;
            }
        }
        env_free(loop_env);
        return result;
    }

    /* ---- throw ---- */
    case NODE_THROW: {
        Value *v = eval_node(interp, node->throw_stmt.value, env);
        if (interp->had_error) { value_release(v); return value_null(); }
        /* Store as error message */
        char *msg = value_to_string(v);
        runtime_error(interp, msg, node->line);
        free(msg);
        value_release(v);
        return value_null();
    }

    /* ---- try/catch ---- */
    case NODE_TRY_CATCH: {
        /* Save error state */
        bool prev_error = interp->had_error;
        char prev_msg[sizeof(interp->error_msg)];
        memcpy(prev_msg, interp->error_msg, sizeof(prev_msg));

        interp->had_error = false;
        memset(interp->error_msg, 0, sizeof(interp->error_msg));

        Value *result = eval_node(interp, node->try_catch.try_body, env);

        if (interp->had_error && node->try_catch.catch_body) {
            /* error was caught */
            char caught_msg[sizeof(interp->error_msg)];
            memcpy(caught_msg, interp->error_msg, sizeof(caught_msg));
            interp->had_error = false;
            memset(interp->error_msg, 0, sizeof(interp->error_msg));

            Env *catch_env = env_new(env);
            if (node->try_catch.catch_var) {
                Value *err_val = value_string(caught_msg[0] ? caught_msg : "error");
                env_set(catch_env, node->try_catch.catch_var, err_val, false);
                value_release(err_val);
            }
            value_release(result);
            result = eval_node(interp, node->try_catch.catch_body, catch_env);
            env_free(catch_env);
        } else if (!interp->had_error && prev_error) {
            /* restore previous error state if try succeeded */
            interp->had_error = prev_error;
            memcpy(interp->error_msg, prev_msg, sizeof(prev_msg));
        }

        if (node->try_catch.finally_body) {
            value_release(eval_node(interp, node->try_catch.finally_body, env));
        }
        return result;
    }

    /* ---- match ---- */
    case NODE_MATCH: {
        Value *val = eval_node(interp, node->match_stmt.value, env);
        if (interp->had_error) return value_null();

        Value *result = value_null();
        bool matched = false;
        for (int i = 0; i < node->match_stmt.count && !matched; i++) {
            ASTNode *pat = node->match_stmt.patterns[i];
            /* Check if value matches pattern (equality, or range membership) */
            Value *pat_val = eval_node(interp, pat, env);
            if (interp->had_error) { value_release(val); value_release(pat_val); return value_null(); }

            bool match_ok = false;
            if (pat_val->type == VAL_ARRAY && pat->type == NODE_RANGE) {
                /* Range match: check if val is in range */
                if (pat_val->array.len == 0) {
                    match_ok = false;
                } else {
                    /* ranges are stored as an array of values when iterated */
                    for (int j = 0; j < pat_val->array.len; j++) {
                        if (value_equals(val, pat_val->array.items[j])) { match_ok = true; break; }
                    }
                }
            } else {
                match_ok = value_equals(val, pat_val);
            }
            value_release(pat_val);

            if (match_ok) {
                matched = true;
                value_release(result);
                result = eval_node(interp, node->match_stmt.bodies[i], env);
            }
        }

        if (!matched && node->match_stmt.else_body) {
            value_release(result);
            result = eval_node(interp, node->match_stmt.else_body, env);
        }

        value_release(val);
        return result;
    }

    /* ---- range literal ---- */
    case NODE_RANGE: {
        Value *start = eval_node(interp, node->range_lit.start, env);
        Value *end   = eval_node(interp, node->range_lit.end,   env);
        if (interp->had_error) { value_release(start); value_release(end); return value_null(); }

        long long step_n = 1;
        if (node->range_lit.step) {
            Value *step_val = eval_node(interp, node->range_lit.step, env);
            if (step_val->type == VAL_INT) step_n = step_val->int_val;
            value_release(step_val);
        }
        if (step_n == 0) step_n = 1;

        Value *arr = value_array_new();

        if (start->type == VAL_INT && end->type == VAL_INT) {
            long long a = start->int_val, b = end->int_val;
            if (step_n > 0) {
                for (long long v = a; node->range_lit.inclusive ? v <= b : v < b; v += step_n) {
                    Value *iv = value_int(v); value_array_push(arr, iv); value_release(iv);
                }
            } else {
                for (long long v = a; node->range_lit.inclusive ? v >= b : v > b; v += step_n) {
                    Value *iv = value_int(v); value_array_push(arr, iv); value_release(iv);
                }
            }
        } else if (start->type == VAL_FLOAT || end->type == VAL_FLOAT) {
            double a = (start->type == VAL_FLOAT ? start->float_val : (double)start->int_val);
            double b = (end->type   == VAL_FLOAT ? end->float_val   : (double)end->int_val);
            double step_f = (double)step_n;
            if (step_f > 0) {
                for (double v = a; node->range_lit.inclusive ? v <= b : v < b; v += step_f) {
                    Value *fv = value_float(v); value_array_push(arr, fv); value_release(fv);
                }
            }
        } else if (start->type == VAL_STRING && end->type == VAL_STRING) {
            /* character range: "a".."z" */
            char sc = start->str_val[0], ec = end->str_val[0];
            for (char c = sc; node->range_lit.inclusive ? c <= ec : c < ec; c += (char)step_n) {
                char buf[2] = {c, '\0'};
                Value *sv = value_string(buf); value_array_push(arr, sv); value_release(sv);
            }
        }

        value_release(start); value_release(end);
        return arr;
    }

    /* ---- null coalescing ---- */
    case NODE_NULLCOAL: {
        Value *left = eval_node(interp, node->nullcoal.left, env);
        if (interp->had_error) return left;
        /* Return right if left is null or unknown (bool_val == -1) */
        bool is_null_like = (left->type == VAL_NULL) ||
                            (left->type == VAL_BOOL && left->bool_val == -1);
        if (!is_null_like) {
            return left;
        }
        value_release(left);
        return eval_node(interp, node->nullcoal.right, env);
    }

    /* ---- safe access ---- */
    case NODE_SAFE_ACCESS: {
        Value *obj = eval_node(interp, node->safe_access.obj, env);
        if (interp->had_error) return value_null();
        if (obj->type == VAL_NULL) {
            value_release(obj);
            return value_null();
        }
        if (node->safe_access.is_call) {
            /* safe method call */
            int argc = node->safe_access.arg_count;
            Value **args = malloc((argc + 1) * sizeof(Value *));
            for (int i = 0; i < argc; i++) {
                args[i] = eval_node(interp, node->safe_access.args[i], env);
                if (interp->had_error) {
                    for (int j = 0; j < i; j++) value_release(args[j]);
                    free(args); value_release(obj); return value_null();
                }
            }
            Value *result;
            switch (obj->type) {
                case VAL_STRING: result = string_method(interp, obj, node->safe_access.name, args, argc, node->line); break;
                case VAL_ARRAY:  result = array_method (interp, obj, node->safe_access.name, args, argc, node->line); break;
                case VAL_DICT:   result = dict_method  (interp, obj, node->safe_access.name, args, argc, node->line); break;
                default: result = value_null(); break;
            }
            for (int i = 0; i < argc; i++) value_release(args[i]);
            free(args); value_release(obj);
            return result;
        } else {
            /* safe property access */
            Value *result = value_null();
            if (obj->type == VAL_DICT) {
                Value *key = value_string(node->safe_access.name);
                Value *found = value_dict_get(obj, key);
                value_release(key);
                if (found) { value_release(result); result = value_retain(found); }
            }
            value_release(obj);
            return result;
        }
    }

    /* ---- is / is not type check ---- */
    case NODE_IS_EXPR: {
        Value *obj = eval_node(interp, node->is_expr.obj, env);
        if (interp->had_error) return value_null();
        const char *tname = node->is_expr.type_name;
        bool match_ok = false;
        if (strcmp(tname, "int")     == 0) match_ok = (obj->type == VAL_INT);
        else if (strcmp(tname, "float")   == 0) match_ok = (obj->type == VAL_FLOAT);
        else if (strcmp(tname, "str")     == 0) match_ok = (obj->type == VAL_STRING);
        else if (strcmp(tname, "bool")    == 0) match_ok = (obj->type == VAL_BOOL);
        else if (strcmp(tname, "null")    == 0) match_ok = (obj->type == VAL_NULL);
        else if (strcmp(tname, "arr")     == 0) match_ok = (obj->type == VAL_ARRAY);
        else if (strcmp(tname, "array")   == 0) match_ok = (obj->type == VAL_ARRAY);
        else if (strcmp(tname, "dict")    == 0) match_ok = (obj->type == VAL_DICT);
        else if (strcmp(tname, "set")     == 0) match_ok = (obj->type == VAL_SET);
        else if (strcmp(tname, "tuple")   == 0) match_ok = (obj->type == VAL_TUPLE);
        else if (strcmp(tname, "unknown") == 0) match_ok = (obj->type == VAL_BOOL && obj->bool_val == -1);
        else {
            /* check type() string */
            char *tn = value_to_string(obj);
            /* compare by value_type_name */
            const char *actual = value_type_name(obj->type);
            match_ok = (strcmp(actual, tname) == 0 || strcmp(tn, tname) == 0);
            free(tn);
        }
        value_release(obj);
        return value_bool((node->is_expr.negate ? !match_ok : match_ok) ? 1 : 0);
    }

    /* ---- ternary: cond ? then_val : else_val ---- */
    case NODE_TERNARY: {
        Value *cond = eval_node(interp, node->ternary.cond, env);
        if (interp->had_error) return value_null();
        bool truthy = value_truthy(cond);
        value_release(cond);
        if (truthy) return eval_node(interp, node->ternary.then_val, env);
        return eval_node(interp, node->ternary.else_val, env);
    }

    /* ---- fn expression: fn(params) { body } ---- */
    case NODE_FN_EXPR: {
        Value *f = value_function("<anonymous>",
                                  node->fn_expr.params,
                                  node->fn_expr.param_count,
                                  node->fn_expr.body,
                                  env);
        return f;
    }

    /* ---- spread expression: ...expr ---- */
    case NODE_SPREAD: {
        /* Spread is handled at the call site; here just evaluate the inner expr */
        return eval_node(interp, node->spread.expr, env);
    }

    /* ---- class declaration ---- */
    case NODE_CLASS_DECL: {
        /* Store the class as a dict with __class_name__ and methods */
        Value *cls = value_dict_new();
        Value *cname_key = value_string("__class_name__");
        Value *cname_val = value_string(node->class_decl.name);
        value_dict_set(cls, cname_key, cname_val);
        value_release(cname_key); value_release(cname_val);

        if (node->class_decl.super) {
            Value *super_key = value_string("__super__");
            Value *super_val = value_string(node->class_decl.super);
            value_dict_set(cls, super_key, super_val);
            value_release(super_key); value_release(super_val);
        }

        /* Store each method */
        for (int i = 0; i < node->class_decl.method_count; i++) {
            ASTNode *mdecl = node->class_decl.methods[i];
            if (mdecl->type != NODE_FUNC_DECL) continue;
            Value *mfunc = value_function(mdecl->func_decl.name,
                                          mdecl->func_decl.params,
                                          mdecl->func_decl.param_count,
                                          mdecl->func_decl.body,
                                          env);
            Value *mkey = value_string(mdecl->func_decl.name);
            value_dict_set(cls, mkey, mfunc);
            value_release(mkey);
            value_release(mfunc);
        }

        env_set(env, node->class_decl.name, cls, false);
        value_release(cls);
        return value_null();
    }

    /* ---- struct declaration ---- */
    case NODE_STRUCT_DECL: {
        /* Create a constructor function as a builtin-like value.
           We store the struct definition as a class dict. */
        Value *cls = value_dict_new();
        Value *cname_key = value_string("__class_name__");
        Value *cname_val = value_string(node->struct_decl.name);
        value_dict_set(cls, cname_key, cname_val);
        value_release(cname_key); value_release(cname_val);

        Value *struct_key = value_string("__is_struct__");
        Value *struct_val = value_bool(1);
        value_dict_set(cls, struct_key, struct_val);
        value_release(struct_key); value_release(struct_val);

        /* Store field names array */
        Value *fields_key = value_string("__fields__");
        Value *fields_arr = value_array_new();
        for (int i = 0; i < node->struct_decl.field_count; i++) {
            Value *fn_v = value_string(node->struct_decl.fields[i]);
            value_array_push(fields_arr, fn_v);
            value_release(fn_v);
        }
        value_dict_set(cls, fields_key, fields_arr);
        value_release(fields_key); value_release(fields_arr);

        env_set(env, node->struct_decl.name, cls, false);
        value_release(cls);
        return value_null();
    }

    /* ---- new expression ---- */
    case NODE_NEW_EXPR: {
        /* Look up the class */
        Value *cls = env_get(env, node->new_expr.class_name);
        if (!cls) {
            char emsg[128];
            snprintf(emsg, sizeof(emsg), "class '%s' not defined", node->new_expr.class_name);
            runtime_error(interp, emsg, node->line);
            return value_null();
        }

        if (cls->type != VAL_DICT) {
            char emsg[128];
            snprintf(emsg, sizeof(emsg), "'%s' is not a class", node->new_expr.class_name);
            runtime_error(interp, emsg, node->line);
            return value_null();
        }

        /* Create a fresh dict instance */
        Value *instance = value_dict_new();

        /* Set __instance_of__ */
        Value *iof_key = value_string("__instance_of__");
        value_dict_set(instance, iof_key, cls);
        value_release(iof_key);

        Value *cname_key2 = value_string("__class_name__");
        Value *cname_val2 = value_dict_get(cls, cname_key2);
        if (cname_val2) value_dict_set(instance, cname_key2, cname_val2);
        value_release(cname_key2);

        /* For structs, fill fields from positional args */
        Value *is_struct_key = value_string("__is_struct__");
        Value *is_struct = value_dict_get(cls, is_struct_key);
        value_release(is_struct_key);

        if (is_struct && value_truthy(is_struct)) {
            Value *fields_key2 = value_string("__fields__");
            Value *fields_arr2 = value_dict_get(cls, fields_key2);
            value_release(fields_key2);
            if (fields_arr2 && fields_arr2->type == VAL_ARRAY) {
                for (int i = 0; i < fields_arr2->array.len && i < node->new_expr.arg_count; i++) {
                    Value *fkey = fields_arr2->array.items[i];
                    Value *fval = eval_node(interp, node->new_expr.args[i], env);
                    if (interp->had_error) { value_release(instance); return value_null(); }
                    value_dict_set(instance, fkey, fval);
                    value_release(fval);
                }
                /* remaining fields = null */
                for (int i = node->new_expr.arg_count; i < fields_arr2->array.len; i++) {
                    Value *fkey = fields_arr2->array.items[i];
                    Value *fnull = value_null();
                    value_dict_set(instance, fkey, fnull);
                    value_release(fnull);
                }
            }
        } else {
            /* class: look for 'init' method and call it */
            Value *init_key = value_string("init");
            Value *init_fn = value_dict_get(cls, init_key);
            value_release(init_key);
            if (!init_fn) {
                /* try __init__ */
                Value *init_key2 = value_string("__init__");
                init_fn = value_dict_get(cls, init_key2);
                value_release(init_key2);
            }
            if (init_fn && init_fn->type == VAL_FUNCTION) {
                /* Call init with self=instance */
                Env *call_env = env_new(init_fn->func.closure);
                env_set(call_env, "self", instance, false);
                /* bind parameters to args */
                for (int i = 0; i < init_fn->func.param_count; i++) {
                    const char *pname = init_fn->func.params[i].name;
                    if (strncmp(pname, "self", 4) == 0 && strlen(pname) == 4) continue;
                    Value *argval = value_null();
                    int arg_offset = 0; /* offset: skip 'self' if it's param 0 */
                    if (i < init_fn->func.param_count &&
                        strcmp(init_fn->func.params[0].name, "self") == 0) {
                        arg_offset = 1;
                    }
                    int ai = i - arg_offset;
                    if (ai >= 0 && ai < node->new_expr.arg_count) {
                        value_release(argval);
                        argval = eval_node(interp, node->new_expr.args[ai], env);
                        if (interp->had_error) { env_free(call_env); value_release(instance); return value_null(); }
                    } else if (init_fn->func.params[i].default_val) {
                        value_release(argval);
                        argval = eval_node(interp, init_fn->func.params[i].default_val, env);
                    }
                    env_set(call_env, pname, argval, false);
                    value_release(argval);
                }
                eval_node(interp, init_fn->func.body, call_env);
                interp->returning = false;
                if (interp->return_val) { value_release(interp->return_val); interp->return_val = NULL; }
                env_free(call_env);
            } else if (node->new_expr.arg_count > 0) {
                /* No init but args provided — treat as positional dict fields */
                for (int i = 0; i < node->new_expr.arg_count; i++) {
                    char kbuf[32]; snprintf(kbuf, sizeof(kbuf), "%d", i);
                    Value *fkey = value_string(kbuf);
                    Value *fval = eval_node(interp, node->new_expr.args[i], env);
                    if (interp->had_error) { value_release(fkey); value_release(instance); return value_null(); }
                    value_dict_set(instance, fkey, fval);
                    value_release(fkey); value_release(fval);
                }
            }
        }
        return instance;
    }

    /* ---- walrus / walrus_expr ---- */
    case NODE_WALRUS:
    case NODE_WALRUS_EXPR: {
        Value *val = eval_node(interp, node->walrus.value, env);
        if (interp->had_error) return value_null();
        env_set(env, node->walrus.name, val, false);
        return val; /* return without releasing so caller sees it */
    }

    default: {
        char emsg[64];
        snprintf(emsg, sizeof(emsg), "unknown AST node type %d", node->type);
        runtime_error(interp, emsg, node->line);
        return value_null();
    }
    }
}

/* ================================================================== public API */

Interpreter *interpreter_new(void) {
    Interpreter *i = calloc(1, sizeof(Interpreter));
    i->gc          = gc_global();
    i->globals     = env_new(NULL);
    register_builtins(i);
    return i;
}

void interpreter_free(Interpreter *interp) {
    if (!interp) return;
    /* Item 3: protect return_val so the sweep cannot collect it before we
     * release it below.  If return_val is NULL, gc_push_root is a no-op. */
    if (interp->return_val) gc_push_root(interp->gc, interp->return_val);
    gc_collect_audit(interp->gc, interp->globals, NULL, NULL);
    if (interp->return_val) gc_pop_root(interp->gc);
    if (interp->return_val) value_release(interp->return_val);
    env_free_root(interp->globals); /* use root destructor: bypasses ref-counting */
    /* Item 5: free the static HTML-GUI body buffer allocated by gui_body_append */
    if (g_gui.body) {
        free(g_gui.body);
        g_gui.body     = NULL;
        g_gui.body_len = 0;
        g_gui.body_cap = 0;
    }
#ifdef HAVE_X11
    /* Item 5: destroy the X11 GUI handle if the program exited without xgui_close */
    if (g_xgui) { xgui_destroy(g_xgui); g_xgui = NULL; }
#endif
    free(interp);
}

void interpreter_run(Interpreter *interp, ASTNode *program) {
    Value *result = eval_node(interp, program, interp->globals);
    /* Item 3: protect result so the sweep cannot collect it before value_release */
    gc_push_root(interp->gc, result);
    gc_collect_audit(interp->gc, interp->globals, NULL, NULL);
    gc_pop_root(interp->gc);
    value_release(result);
}

Value *interpreter_eval(Interpreter *interp, ASTNode *node, Env *env) {
    return eval_node(interp, node, env);
}

char *interpreter_process_fstring(Interpreter *interp, const char *tmpl, Env *env) {
    return process_fstring(interp, tmpl, env);
}
