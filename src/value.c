#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "value.h"
#include "chunk.h"
#include "gc.h"

/* Forward declarations for Env ref-counting (implemented in interpreter.c).
 * Using extern avoids a circular include between value.h <-> interpreter.h. */
extern Env *env_retain(Env *env);
extern void env_free(Env *env);

/* ================================================================== immortal singletons
 *
 * Common values that would otherwise be re-allocated on every use are
 * created once, marked immortal, and returned from the constructors.
 * Immortal values bypass reference counting and are never tracked by
 * or freed by the GC.
 * ================================================================== */

#define SMALL_INT_RANGE  (GC_SMALL_INT_MAX - GC_SMALL_INT_MIN + 1)

static Value *s_val_null    = NULL;
static Value *s_val_true    = NULL;
static Value *s_val_false   = NULL;
static Value *s_val_unknown = NULL;
static Value  s_small_ints[SMALL_INT_RANGE]; /* static storage, no heap alloc */
static bool   s_singletons_ready = false;

static void init_immortal(Value *v, ValueType t) {
    memset(v, 0, sizeof(*v));
    v->type        = t;
    v->ref_count   = 1;
    v->gc_immortal = 1;
    /* NOT gc_track_value — immortals live outside the GC object list */
}

void value_immortals_init(void) {
    if (s_singletons_ready) return;

    /* heap singletons for null / bool (small structs, but need stable ptrs) */
    s_val_null    = calloc(1, sizeof(Value));
    s_val_true    = calloc(1, sizeof(Value));
    s_val_false   = calloc(1, sizeof(Value));
    s_val_unknown = calloc(1, sizeof(Value));

    init_immortal(s_val_null,    VAL_NULL);
    init_immortal(s_val_true,    VAL_BOOL);  s_val_true->bool_val    =  1;
    init_immortal(s_val_false,   VAL_BOOL);  s_val_false->bool_val   =  0;
    init_immortal(s_val_unknown, VAL_BOOL);  s_val_unknown->bool_val = -1;

    /* small-integer cache: static array, in-place init */
    for (int i = 0; i < SMALL_INT_RANGE; i++) {
        init_immortal(&s_small_ints[i], VAL_INT);
        s_small_ints[i].int_val = (long long)(GC_SMALL_INT_MIN + i);
    }

    s_singletons_ready = true;

    /* tell the GC how many immortals we have (informational only) */
    gc_global()->stats.immortal_count = 4 + SMALL_INT_RANGE;
}

void value_immortals_free(void) {
    if (!s_singletons_ready) return;
    free(s_val_null);    s_val_null    = NULL;
    free(s_val_true);    s_val_true    = NULL;
    free(s_val_false);   s_val_false   = NULL;
    free(s_val_unknown); s_val_unknown = NULL;
    /* s_small_ints is a static array — no heap to free */
    s_singletons_ready = false;
}

/* ------------------------------------------------------------------ ref count */

Value *value_retain(Value *v) {
    if (!v || v->gc_immortal) return v;
    v->ref_count++;
    return v;
}

void value_release(Value *v) {
    if (!v || v->gc_immortal) return;
    if (--v->ref_count > 0) return;

    gc_untrack_value(v);

    switch (v->type) {
        case VAL_STRING:
            free(v->str_val);
            break;
        case VAL_ARRAY:
            for (int i = 0; i < v->array.len; i++) value_release(v->array.items[i]);
            free(v->array.items);
            break;
        case VAL_DICT:
            for (int i = 0; i < v->dict.len; i++) {
                value_release(v->dict.entries[i].key);
                value_release(v->dict.entries[i].val);
            }
            free(v->dict.entries);
            break;
        case VAL_SET:
            for (int i = 0; i < v->set.len; i++) value_release(v->set.items[i]);
            free(v->set.items);
            break;
        case VAL_TUPLE:
            for (int i = 0; i < v->tuple.len; i++) value_release(v->tuple.items[i]);
            free(v->tuple.items);
            break;
        case VAL_FUNCTION:
            if (v->func.owns_chunk && v->func.chunk) {
                chunk_free(v->func.chunk);
                free(v->func.chunk);
            }
            free(v->func.name);
            env_free(v->func.closure); /* release reference to captured env */
            break;
        case VAL_BUILTIN:
            free(v->builtin.name);
            break;
        default:
            break;
    }
    free(v);
}

/* ------------------------------------------------------------------ constructors */

static Value *val_new(ValueType t) {
    Value *v = calloc(1, sizeof(Value));
    v->type         = t;
    v->ref_count    = 1;
    v->gc_marked    = 0;
    v->gc_immortal  = 0;
    v->gc_generation = GC_GEN_YOUNG;
    v->gc_next      = NULL;
    gc_track_value(v);
    return v;
}

Value *value_null(void) {
    if (s_singletons_ready) return s_val_null;
    return val_new(VAL_NULL);
}

Value *value_int(long long n) {
    if (s_singletons_ready && n >= GC_SMALL_INT_MIN && n <= GC_SMALL_INT_MAX)
        return &s_small_ints[n - GC_SMALL_INT_MIN];
    Value *v = val_new(VAL_INT);
    v->int_val = n;
    return v;
}

Value *value_float(double d) {
    Value *v = val_new(VAL_FLOAT);
    v->float_val = d;
    return v;
}

Value *value_bool(int b) {
    if (s_singletons_ready) {
        if (b > 0)  return s_val_true;
        if (b == 0) return s_val_false;
        return s_val_unknown;
    }
    Value *v = val_new(VAL_BOOL);
    v->bool_val = b;
    return v;
}

Value *value_complex(double real, double imag) {
    Value *v = val_new(VAL_COMPLEX);
    v->complex_val.real = real;
    v->complex_val.imag = imag;
    return v;
}

Value *value_string(const char *s) {
    Value *v = val_new(VAL_STRING);
    v->str_val = strdup(s ? s : "");
    return v;
}

Value *value_string_take(char *s) {
    Value *v = val_new(VAL_STRING);
    v->str_val = s ? s : strdup("");
    return v;
}

Value *value_string_intern(const char *s) {
    return gc_intern_string(gc_global(), s ? s : "");
}

Value *value_array_new(void) {
    Value *v = val_new(VAL_ARRAY);
    v->array.items = malloc(8 * sizeof(Value *));
    v->array.len   = 0;
    v->array.cap   = 8;
    return v;
}

Value *value_dict_new(void) {
    Value *v = val_new(VAL_DICT);
    v->dict.entries = malloc(8 * sizeof(DictEntry));
    v->dict.len = 0;
    v->dict.cap = 8;
    v->dict.version = 1;
    return v;
}

Value *value_set_new(void) {
    Value *v = val_new(VAL_SET);
    v->set.items = malloc(8 * sizeof(Value *));
    v->set.len   = 0;
    v->set.cap   = 8;
    return v;
}

Value *value_tuple_new(Value **items, int count) {
    Value *v = val_new(VAL_TUPLE);
    v->tuple.items = malloc((count ? count : 1) * sizeof(Value *));
    v->tuple.len   = count;
    v->tuple.cap   = count;
    for (int i = 0; i < count; i++) v->tuple.items[i] = value_retain(items[i]);
    return v;
}

Value *value_function(const char *name, Param *params, int param_count,
                      ASTNode *body, Env *closure) {
    Value *v = val_new(VAL_FUNCTION);
    v->func.name        = strdup(name ? name : "<anonymous>");
    v->func.params      = params;
    v->func.param_count = param_count;
    v->func.body        = body;
    v->func.closure     = env_retain(closure); /* keep env alive for closures */
    v->func.chunk       = NULL;
    v->func.owns_chunk  = false;
    return v;
}

Value *value_builtin(const char *name, BuiltinFn fn) {
    Value *v = val_new(VAL_BUILTIN);
    v->builtin.name = strdup(name);
    v->builtin.fn   = fn;
    return v;
}

/* ------------------------------------------------------------------ array ops */

void value_array_push(Value *arr, Value *item) {
    if (arr->array.len >= arr->array.cap) {
        arr->array.cap *= 2;
        arr->array.items = realloc(arr->array.items, arr->array.cap * sizeof(Value *));
    }
    arr->array.items[arr->array.len++] = value_retain(item);
}

Value *value_array_get(Value *arr, long long idx) {
    long long len = arr->array.len;
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) return NULL;
    return arr->array.items[idx];
}

void value_array_insert(Value *arr, long long idx, Value *item) {
    if (arr->array.len >= arr->array.cap) {
        arr->array.cap *= 2;
        arr->array.items = realloc(arr->array.items, arr->array.cap * sizeof(Value *));
    }
    if (idx < 0) idx = 0;
    if (idx > arr->array.len) idx = arr->array.len;
    memmove(&arr->array.items[idx+1], &arr->array.items[idx],
            (arr->array.len - idx) * sizeof(Value *));
    arr->array.items[idx] = value_retain(item);
    arr->array.len++;
}

bool value_array_remove(Value *arr, Value *item) {
    for (int i = 0; i < arr->array.len; i++) {
        if (value_equals(arr->array.items[i], item)) {
            value_release(arr->array.items[i]);
            memmove(&arr->array.items[i], &arr->array.items[i+1],
                    (arr->array.len - i - 1) * sizeof(Value *));
            arr->array.len--;
            return true;
        }
    }
    return false;
}

Value *value_array_pop(Value *arr, long long idx) {
    long long len = arr->array.len;
    if (len == 0) return value_null();
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) idx = len - 1;
    Value *item = arr->array.items[idx];
    memmove(&arr->array.items[idx], &arr->array.items[idx+1],
            (arr->array.len - idx - 1) * sizeof(Value *));
    arr->array.len--;
    return item; /* caller owns ref */
}

static int cmp_values(const void *a, const void *b) {
    Value *va = *(Value **)a;
    Value *vb = *(Value **)b;
    return value_compare(va, vb);
}

void value_array_sort(Value *arr) {
    qsort(arr->array.items, arr->array.len, sizeof(Value *), cmp_values);
}

void value_array_extend(Value *arr, Value *other) {
    if (other->type == VAL_ARRAY || other->type == VAL_TUPLE) {
        ValueArray *src = (other->type == VAL_ARRAY) ? &other->array : &other->tuple;
        for (int i = 0; i < src->len; i++) value_array_push(arr, src->items[i]);
    }
}

/* ------------------------------------------------------------------ dict ops */

static bool keys_equal(Value *a, Value *b) { return value_equals(a, b); }

int value_dict_find_index(Value *dict, Value *key) {
    for (int i = 0; i < dict->dict.len; i++)
        if (keys_equal(dict->dict.entries[i].key, key))
            return i;
    return -1;
}

Value *value_dict_get_at(Value *dict, int index) {
    if (index < 0 || index >= dict->dict.len) return NULL;
    return dict->dict.entries[index].val;
}

Value *value_dict_get(Value *dict, Value *key) {
    int index = value_dict_find_index(dict, key);
    if (index >= 0) return dict->dict.entries[index].val;
    return NULL;
}

Value *value_dict_get_cached(Value *dict, Value *key, int *index, unsigned int *version) {
    if (*index >= 0 && *version == dict->dict.version) {
        Value *cached = value_dict_get_at(dict, *index);
        if (cached && keys_equal(dict->dict.entries[*index].key, key)) return cached;
    }
    *index = value_dict_find_index(dict, key);
    *version = dict->dict.version;
    return *index >= 0 ? dict->dict.entries[*index].val : NULL;
}

void value_dict_set(Value *dict, Value *key, Value *val) {
    for (int i = 0; i < dict->dict.len; i++) {
        if (keys_equal(dict->dict.entries[i].key, key)) {
            value_release(dict->dict.entries[i].val);
            dict->dict.entries[i].val = value_retain(val);
            dict->dict.version++;
            return;
        }
    }
    if (dict->dict.len >= dict->dict.cap) {
        dict->dict.cap *= 2;
        dict->dict.entries = realloc(dict->dict.entries, dict->dict.cap * sizeof(DictEntry));
    }
    dict->dict.entries[dict->dict.len].key = value_retain(key);
    dict->dict.entries[dict->dict.len].val = value_retain(val);
    dict->dict.len++;
    dict->dict.version++;
}

bool value_dict_remove(Value *dict, Value *key) {
    for (int i = 0; i < dict->dict.len; i++) {
        if (keys_equal(dict->dict.entries[i].key, key)) {
            value_release(dict->dict.entries[i].key);
            value_release(dict->dict.entries[i].val);
            memmove(&dict->dict.entries[i], &dict->dict.entries[i+1],
                    (dict->dict.len - i - 1) * sizeof(DictEntry));
            dict->dict.len--;
            dict->dict.version++;
            return true;
        }
    }
    return false;
}

void value_dict_clear(Value *dict) {
    for (int i = 0; i < dict->dict.len; i++) {
        value_release(dict->dict.entries[i].key);
        value_release(dict->dict.entries[i].val);
    }
    dict->dict.len = 0;
    dict->dict.version++;
}

/* ------------------------------------------------------------------ set ops */

bool value_set_has(Value *set, Value *item) {
    for (int i = 0; i < set->set.len; i++)
        if (value_equals(set->set.items[i], item)) return true;
    return false;
}

void value_set_add(Value *set, Value *item) {
    if (!value_set_has(set, item)) {
        if (set->set.len >= set->set.cap) {
            set->set.cap *= 2;
            set->set.items = realloc(set->set.items, set->set.cap * sizeof(Value *));
        }
        set->set.items[set->set.len++] = value_retain(item);
    }
}

bool value_set_remove(Value *set, Value *item) {
    for (int i = 0; i < set->set.len; i++) {
        if (value_equals(set->set.items[i], item)) {
            value_release(set->set.items[i]);
            memmove(&set->set.items[i], &set->set.items[i+1],
                    (set->set.len - i - 1) * sizeof(Value *));
            set->set.len--;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ equality & compare */

bool value_equals(Value *a, Value *b) {
    if (!a || !b) return a == b;
    /* immortal singletons can use pointer equality for bool/null */
    if (a == b) return true;
    if (a->type == VAL_NULL && b->type == VAL_NULL) return true;
    if (a->type != b->type) {
        if (a->type == VAL_INT   && b->type == VAL_FLOAT)
            return (double)a->int_val == b->float_val;
        if (a->type == VAL_FLOAT && b->type == VAL_INT)
            return a->float_val == (double)b->int_val;
        return false;
    }
    switch (a->type) {
        case VAL_INT:    return a->int_val   == b->int_val;
        case VAL_FLOAT:  return a->float_val == b->float_val;
        case VAL_BOOL:   return a->bool_val  == b->bool_val;
        case VAL_STRING:
            /* O(1) for interned strings: same content → same immortal Value pointer */
            if (a->gc_immortal && b->gc_immortal) return a == b;
            return strcmp(a->str_val, b->str_val) == 0;
        case VAL_COMPLEX:
            return a->complex_val.real == b->complex_val.real &&
                   a->complex_val.imag == b->complex_val.imag;
        case VAL_ARRAY:
        case VAL_TUPLE:
        {
            ValueArray *aa = (a->type == VAL_ARRAY) ? &a->array : &a->tuple;
            ValueArray *bb = (b->type == VAL_ARRAY) ? &b->array : &b->tuple;
            if (aa->len != bb->len) return false;
            for (int i = 0; i < aa->len; i++)
                if (!value_equals(aa->items[i], bb->items[i])) return false;
            return true;
        }
        case VAL_SET: {
            if (a->set.len != b->set.len) return false;
            for (int i = 0; i < a->set.len; i++)
                if (!value_set_has(b, a->set.items[i])) return false;
            return true;
        }
        case VAL_DICT: {
            if (a->dict.len != b->dict.len) return false;
            for (int i = 0; i < a->dict.len; i++) {
                Value *bval = value_dict_get(b, a->dict.entries[i].key);
                if (!bval) return false;
                if (!value_equals(a->dict.entries[i].val, bval)) return false;
            }
            return true;
        }
        default: return a == b;
    }
}

int value_compare(Value *a, Value *b) {
    if (!a || !b) return 0;
    if (a->type == VAL_INT && b->type == VAL_INT)
        return (a->int_val > b->int_val) - (a->int_val < b->int_val);
    if (a->type == VAL_FLOAT && b->type == VAL_FLOAT)
        return (a->float_val > b->float_val) - (a->float_val < b->float_val);
    if (a->type == VAL_INT && b->type == VAL_FLOAT) {
        double d = (double)a->int_val - b->float_val;
        return (d > 0) - (d < 0);
    }
    if (a->type == VAL_FLOAT && b->type == VAL_INT) {
        double d = a->float_val - (double)b->int_val;
        return (d > 0) - (d < 0);
    }
    if (a->type == VAL_STRING && b->type == VAL_STRING)
        return strcmp(a->str_val, b->str_val);
    return 0;
}

bool value_truthy(Value *v) {
    if (!v) return false;
    switch (v->type) {
        case VAL_NULL:    return false;
        case VAL_BOOL:    return v->bool_val == 1;
        case VAL_INT:     return v->int_val != 0;
        case VAL_FLOAT:   return v->float_val != 0.0;
        case VAL_STRING:  return strlen(v->str_val) > 0;
        case VAL_ARRAY:   return v->array.len > 0;
        case VAL_DICT:    return v->dict.len > 0;
        case VAL_SET:     return v->set.len > 0;
        case VAL_TUPLE:   return v->tuple.len > 0;
        default:          return true;
    }
}

/* ------------------------------------------------------------------ to_string */

char *value_to_string(Value *v) {
    if (!v) return strdup("null");
    char buf[256];
    switch (v->type) {
        case VAL_NULL:   return strdup("null");
        case VAL_INT:    snprintf(buf, sizeof(buf), "%lld", v->int_val); return strdup(buf);
        case VAL_FLOAT: {
            snprintf(buf, sizeof(buf), "%g", v->float_val);
            if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'n') && !strchr(buf, 'i'))
                snprintf(buf, sizeof(buf), "%g", v->float_val);
            return strdup(buf);
        }
        case VAL_COMPLEX:
            snprintf(buf, sizeof(buf), "%g+%gj", v->complex_val.real, v->complex_val.imag);
            return strdup(buf);
        case VAL_STRING: return strdup(v->str_val);
        case VAL_BOOL:
            if (v->bool_val == 1)  return strdup("true");
            if (v->bool_val == 0)  return strdup("false");
            return strdup("unknown");

        case VAL_ARRAY: {
            char *out = strdup("[");
            for (int i = 0; i < v->array.len; i++) {
                char *item = value_to_string(v->array.items[i]);
                bool is_str = v->array.items[i]->type == VAL_STRING;
                size_t need = strlen(out) + strlen(item) + 8;
                out = realloc(out, need);
                if (is_str) strcat(out, "\"");
                strcat(out, item);
                if (is_str) strcat(out, "\"");
                if (i < v->array.len - 1) strcat(out, ", ");
                free(item);
            }
            size_t need = strlen(out) + 4;
            out = realloc(out, need);
            strcat(out, "]");
            return out;
        }

        case VAL_TUPLE: {
            char *out = strdup("(");
            for (int i = 0; i < v->tuple.len; i++) {
                char *item = value_to_string(v->tuple.items[i]);
                bool is_str = v->tuple.items[i]->type == VAL_STRING;
                size_t need = strlen(out) + strlen(item) + 8;
                out = realloc(out, need);
                if (is_str) strcat(out, "\"");
                strcat(out, item);
                if (is_str) strcat(out, "\"");
                if (i < v->tuple.len - 1) strcat(out, ", ");
                free(item);
            }
            size_t need = strlen(out) + 4;
            out = realloc(out, need);
            if (v->tuple.len == 1) strcat(out, ",");
            strcat(out, ")");
            return out;
        }

        case VAL_DICT: {
            char *out = strdup("{");
            for (int i = 0; i < v->dict.len; i++) {
                char *k  = value_to_string(v->dict.entries[i].key);
                char *vv = value_to_string(v->dict.entries[i].val);
                bool ks  = v->dict.entries[i].key->type == VAL_STRING;
                bool vs  = v->dict.entries[i].val->type == VAL_STRING;
                size_t need = strlen(out) + strlen(k) + strlen(vv) + 16;
                out = realloc(out, need);
                if (ks) strcat(out, "\"");
                strcat(out, k);
                if (ks) strcat(out, "\"");
                strcat(out, ": ");
                if (vs) strcat(out, "\"");
                strcat(out, vv);
                if (vs) strcat(out, "\"");
                if (i < v->dict.len - 1) strcat(out, ", ");
                free(k); free(vv);
            }
            size_t need = strlen(out) + 4;
            out = realloc(out, need);
            strcat(out, "}");
            return out;
        }

        case VAL_SET: {
            char *out = strdup("{");
            for (int i = 0; i < v->set.len; i++) {
                char *item = value_to_string(v->set.items[i]);
                bool is_str = v->set.items[i]->type == VAL_STRING;
                size_t need = strlen(out) + strlen(item) + 8;
                out = realloc(out, need);
                if (is_str) strcat(out, "\"");
                strcat(out, item);
                if (is_str) strcat(out, "\"");
                if (i < v->set.len - 1) strcat(out, ", ");
                free(item);
            }
            size_t need = strlen(out) + 4;
            out = realloc(out, need);
            strcat(out, "}");
            return out;
        }

        case VAL_FUNCTION:
            snprintf(buf, sizeof(buf), "<function %s>", v->func.name);
            return strdup(buf);
        case VAL_BUILTIN:
            snprintf(buf, sizeof(buf), "<builtin %s>", v->builtin.name);
            return strdup(buf);
        default:
            return strdup("?");
    }
}

void value_print(Value *v) {
    char *s = value_to_string(v);
    printf("%s", s);
    free(s);
}

/* ------------------------------------------------------------------ copy */

Value *value_copy(Value *v) {
    if (!v) return value_null();
    switch (v->type) {
        case VAL_NULL:    return value_null();
        case VAL_INT:     return value_int(v->int_val);
        case VAL_FLOAT:   return value_float(v->float_val);
        case VAL_BOOL:    return value_bool(v->bool_val);
        case VAL_STRING:  return value_string(v->str_val);
        case VAL_COMPLEX: return value_complex(v->complex_val.real, v->complex_val.imag);
        case VAL_ARRAY: {
            Value *a = value_array_new();
            for (int i = 0; i < v->array.len; i++) value_array_push(a, v->array.items[i]);
            return a;
        }
        case VAL_TUPLE:   return value_tuple_new(v->tuple.items, v->tuple.len);
        case VAL_SET: {
            Value *s = value_set_new();
            for (int i = 0; i < v->set.len; i++) value_set_add(s, v->set.items[i]);
            return s;
        }
        default:          return value_retain(v);
    }
}

const char *value_type_name(ValueType t) {
    switch (t) {
        case VAL_NULL:     return "null";
        case VAL_INT:      return "int";
        case VAL_FLOAT:    return "float";
        case VAL_COMPLEX:  return "complex";
        case VAL_STRING:   return "string";
        case VAL_BOOL:     return "bool";
        case VAL_ARRAY:    return "array";
        case VAL_DICT:     return "dict";
        case VAL_SET:      return "set";
        case VAL_TUPLE:    return "tuple";
        case VAL_FUNCTION: return "function";
        case VAL_BUILTIN:  return "builtin";
        default:           return "?";
    }
}

/* ------------------------------------------------------------------ arithmetic */

Value *value_add(Value *a, Value *b) {
    if (a->type == VAL_INT && b->type == VAL_INT)
        return value_int(a->int_val + b->int_val);
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double da = (a->type == VAL_INT) ? (double)a->int_val : a->float_val;
        double db = (b->type == VAL_INT) ? (double)b->int_val : b->float_val;
        return value_float(da + db);
    }
    if (a->type == VAL_COMPLEX || b->type == VAL_COMPLEX) {
        double ar = (a->type == VAL_COMPLEX) ? a->complex_val.real : (a->type == VAL_INT ? (double)a->int_val : a->float_val);
        double ai = (a->type == VAL_COMPLEX) ? a->complex_val.imag : 0.0;
        double br = (b->type == VAL_COMPLEX) ? b->complex_val.real : (b->type == VAL_INT ? (double)b->int_val : b->float_val);
        double bi = (b->type == VAL_COMPLEX) ? b->complex_val.imag : 0.0;
        return value_complex(ar + br, ai + bi);
    }
    if (a->type == VAL_STRING && b->type == VAL_STRING) {
        size_t la = strlen(a->str_val), lb = strlen(b->str_val);
        char *s = malloc(la + lb + 1);
        memcpy(s, a->str_val, la);
        memcpy(s + la, b->str_val, lb);
        s[la + lb] = '\0';
        return value_string_take(s);
    }
    if (a->type == VAL_ARRAY && b->type == VAL_ARRAY) {
        Value *arr = value_copy(a);
        value_array_extend(arr, b);
        return arr;
    }
    return NULL;
}

Value *value_sub(Value *a, Value *b) {
    if (a->type == VAL_INT && b->type == VAL_INT)
        return value_int(a->int_val - b->int_val);
    if ((a->type == VAL_FLOAT || b->type == VAL_FLOAT) &&
        (a->type == VAL_INT || a->type == VAL_FLOAT) &&
        (b->type == VAL_INT || b->type == VAL_FLOAT)) {
        double da = (a->type == VAL_INT) ? (double)a->int_val : a->float_val;
        double db = (b->type == VAL_INT) ? (double)b->int_val : b->float_val;
        return value_float(da - db);
    }
    if (a->type == VAL_SET && b->type == VAL_SET) {
        Value *res = value_set_new();
        for (int i = 0; i < a->set.len; i++)
            if (!value_set_has(b, a->set.items[i]))
                value_set_add(res, a->set.items[i]);
        return res;
    }
    return NULL;
}

Value *value_mul(Value *a, Value *b) {
    if (a->type == VAL_INT && b->type == VAL_INT)
        return value_int(a->int_val * b->int_val);
    if ((a->type == VAL_FLOAT || b->type == VAL_FLOAT) &&
        (a->type == VAL_INT || a->type == VAL_FLOAT) &&
        (b->type == VAL_INT || b->type == VAL_FLOAT)) {
        double da = (a->type == VAL_INT) ? (double)a->int_val : a->float_val;
        double db = (b->type == VAL_INT) ? (double)b->int_val : b->float_val;
        return value_float(da * db);
    }
    if (a->type == VAL_STRING && b->type == VAL_INT) {
        long long n = b->int_val;
        if (n <= 0) return value_string("");
        size_t la = strlen(a->str_val);
        char *s = malloc(la * (size_t)n + 1);
        s[0] = '\0';
        for (long long i = 0; i < n; i++) strcat(s, a->str_val);
        return value_string_take(s);
    }
    if (a->type == VAL_INT && b->type == VAL_STRING) return value_mul(b, a);
    return NULL;
}

Value *value_div(Value *a, Value *b) {
    double da, db;
    if (a->type == VAL_INT)        da = (double)a->int_val;
    else if (a->type == VAL_FLOAT) da = a->float_val;
    else return NULL;
    if (b->type == VAL_INT)        db = (double)b->int_val;
    else if (b->type == VAL_FLOAT) db = b->float_val;
    else return NULL;
    if (db == 0.0) return NULL;
    if (a->type == VAL_INT && b->type == VAL_INT && a->int_val % b->int_val == 0)
        return value_int(a->int_val / b->int_val);
    return value_float(da / db);
}

Value *value_mod(Value *a, Value *b) {
    if (a->type == VAL_INT && b->type == VAL_INT) {
        if (b->int_val == 0) return NULL;
        return value_int(a->int_val % b->int_val);
    }
    double da = (a->type == VAL_INT) ? (double)a->int_val : a->float_val;
    double db = (b->type == VAL_INT) ? (double)b->int_val : b->float_val;
    if (db == 0.0) return NULL;
    return value_float(fmod(da, db));
}

Value *value_pow(Value *a, Value *b) {
    double da = (a->type == VAL_INT) ? (double)a->int_val : a->float_val;
    double db = (b->type == VAL_INT) ? (double)b->int_val : b->float_val;
    double result = pow(da, db);
    if (a->type == VAL_INT && b->type == VAL_INT && b->int_val >= 0)
        return value_int((long long)result);
    return value_float(result);
}

Value *value_neg(Value *a) {
    if (a->type == VAL_INT)     return value_int(-a->int_val);
    if (a->type == VAL_FLOAT)   return value_float(-a->float_val);
    if (a->type == VAL_COMPLEX) return value_complex(-a->complex_val.real, -a->complex_val.imag);
    return NULL;
}
