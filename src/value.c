#include "value.h"
#include "gc.h"
#include "chunk.h"

extern Env *env_retain(Env *env);
extern void env_free(Env *env);

Value value_null(void) { return VAL_SPEC_NULL; }
Value value_int(long long n) { return TO_INT(n); }
Value value_bool(int b) { return TO_BOOL(b); }

static Value alloc_v(ValueType type) {
    ValueStruct *vs = calloc(1, sizeof(ValueStruct));
    vs->type = type; vs->ref_count = 1; gc_track_value((Value)vs); return (Value)vs;
}

/* Slow path: called only for heap-pointer Values (IS_PTR true).             */
Value value_retain_ptr(Value v) {
    ValueStruct *vs = AS_PTR(v);
    if (!vs->gc_immortal) vs->ref_count++;
    return v;
}

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
            if (vs->func.owns_params && vs->func.params) {
                for (int i = 0; i < vs->func.param_count; i++) { free(vs->func.params[i].name); if(vs->func.params[i].type_hint) free(vs->func.params[i].type_hint); }
                free(vs->func.params);
            }
            break;
        case VAL_BUILTIN: if (vs->builtin.name) free(vs->builtin.name); break;
        default: break;
    }
    free(vs);
}

/* Slow path: called only for heap-pointer Values (IS_PTR true).             */
void value_release_ptr(Value v) {
    ValueStruct *vs = AS_PTR(v);
    if (vs->gc_immortal) return;
    if (--vs->ref_count <= 0) { gc_untrack_value(v); vs_free_internal(vs); }
}

Value value_float(double d) { Value v = alloc_v(VAL_FLOAT); AS_FLOAT(v) = d; return v; }
Value value_complex(double r, double i) { Value v = alloc_v(VAL_COMPLEX); AS_COMPLEX(v).real = r; AS_COMPLEX(v).imag = i; return v; }
Value value_string(const char *s) { Value v = alloc_v(VAL_STRING); AS_STR(v) = strdup(s ? s : ""); return v; }
Value value_string_take(char *s) { Value v = alloc_v(VAL_STRING); AS_STR(v) = s ? s : strdup(""); return v; }
Value value_string_intern(const char *s) { return gc_intern_string(gc_global(), s ? s : ""); }

Value value_array_new(void) { Value v = alloc_v(VAL_ARRAY); AS_ARRAY(v).cap = 8; AS_ARRAY(v).items = calloc(8, sizeof(Value)); return v; }
void value_array_push(Value arr, Value item) {
    ValueStruct *vs = AS_PTR(arr);
    if (vs->array.len >= vs->array.cap) {
        int new_cap = vs->array.cap < 8 ? 8 : vs->array.cap * 2;
        vs->array.items = realloc(vs->array.items, new_cap * sizeof(Value)); vs->array.cap = new_cap;
    }
    vs->array.items[vs->array.len++] = value_retain(item);
}
Value value_array_get(Value arr, long long idx) {
    ValueStruct *vs = AS_PTR(arr); if (idx < 0) idx += vs->array.len;
    return (idx < 0 || idx >= vs->array.len) ? 0 : vs->array.items[idx];
}
Value value_array_pop(Value arr, long long idx) {
    ValueStruct *vs = AS_PTR(arr); if (vs->array.len == 0) return 0;
    if (idx < 0) idx += vs->array.len; if (idx < 0 || idx >= vs->array.len) return 0;
    Value res = vs->array.items[idx];
    memmove(&vs->array.items[idx], &vs->array.items[idx+1], (vs->array.len - idx - 1) * sizeof(Value));
    vs->array.len--; return res;
}
void value_array_extend(Value arr, Value other) {
    if (VAL_TYPE(other) != VAL_ARRAY) return;
    ValueStruct *src = AS_PTR(other);
    for (int i = 0; i < src->array.len; i++) value_array_push(arr, src->array.items[i]);
}
static int cmp_values_qsort(const void *pa, const void *pb) {
    return value_compare(*(const Value *)pa, *(const Value *)pb);
}
void value_array_sort(Value arr) {
    ValueStruct *vs = AS_PTR(arr);
    if (vs->array.len > 1)
        qsort(vs->array.items, (size_t)vs->array.len, sizeof(Value), cmp_values_qsort);
}
void value_array_insert(Value arr, long long idx, Value item) {
    ValueStruct *vs = AS_PTR(arr);
    if (idx < 0) idx += vs->array.len; if (idx < 0) idx = 0; if (idx > vs->array.len) idx = vs->array.len;
    if (vs->array.len >= vs->array.cap) {
        int new_cap = vs->array.cap < 8 ? 8 : vs->array.cap * 2;
        vs->array.items = realloc(vs->array.items, new_cap * sizeof(Value)); vs->array.cap = new_cap;
    }
    memmove(&vs->array.items[idx+1], &vs->array.items[idx], (vs->array.len - idx) * sizeof(Value));
    vs->array.items[idx] = value_retain(item); vs->array.len++;
}
bool value_array_remove(Value arr, Value item) {
    ValueStruct *vs = AS_PTR(arr);
    for (int i = 0; i < vs->array.len; i++) {
        if (value_equals(vs->array.items[i], item)) {
            value_release(vs->array.items[i]);
            memmove(&vs->array.items[i], &vs->array.items[i+1], (vs->array.len - i - 1) * sizeof(Value));
            vs->array.len--; return true;
        }
    }
    return false;
}

Value value_dict_new(void) { Value v = alloc_v(VAL_DICT); AS_DICT(v).cap = 8; AS_DICT(v).entries = calloc(8, sizeof(DictEntry)); return v; }
void value_dict_set(Value dict, Value key, Value val) {
    ValueStruct *vs = AS_PTR(dict);
    for (int i = 0; i < vs->dict.cap; i++) if (vs->dict.entries[i].key && value_equals(vs->dict.entries[i].key, key)) {
        value_release(vs->dict.entries[i].val); vs->dict.entries[i].val = value_retain(val); vs->dict.version++; return;
    }
    if (vs->dict.cap == 0 || vs->dict.len >= vs->dict.cap * 0.75) {
        int old_cap = vs->dict.cap; DictEntry *old_entries = vs->dict.entries;
        int new_cap = (old_cap < 8) ? 8 : old_cap * 2;
        vs->dict.entries = calloc(new_cap, sizeof(DictEntry)); vs->dict.cap = new_cap;
        int old_len = vs->dict.len; vs->dict.len = 0;
        for (int i = 0; i < old_cap; i++) if (old_entries && old_entries[i].key) { value_dict_set(dict, old_entries[i].key, old_entries[i].val); value_release(old_entries[i].key); value_release(old_entries[i].val); }
        if (old_entries) free(old_entries);
        vs = AS_PTR(dict);
    }
    for (int i = 0; i < vs->dict.cap; i++) if (!vs->dict.entries[i].key) {
        vs->dict.entries[i].key = value_retain(key); vs->dict.entries[i].val = value_retain(val);
        vs->dict.len++; vs->dict.version++; return;
    }
}
Value value_dict_get(Value dict, Value key) {
    ValueStruct *vs = AS_PTR(dict);
    for (int i = 0; i < vs->dict.cap; i++) if (vs->dict.entries[i].key && value_equals(vs->dict.entries[i].key, key)) return vs->dict.entries[i].val;
    return 0;
}

Value value_set_new(void) { Value v = alloc_v(VAL_SET); AS_SET(v).cap = 8; AS_SET(v).items = calloc(8, sizeof(Value)); return v; }
bool value_set_has(Value set, Value item) { ValueStruct *vs = AS_PTR(set); for (int i = 0; i < vs->set.len; i++) if (value_equals(vs->set.items[i], item)) return true; return false; }
void value_set_add(Value set, Value item) {
    if (!value_set_has(set, item)) {
        ValueStruct *vs = AS_PTR(set);
        if (vs->set.len >= vs->set.cap) {
            int new_cap = vs->set.cap < 8 ? 8 : vs->set.cap * 2;
            vs->set.items = realloc(vs->set.items, new_cap * sizeof(Value)); vs->set.cap = new_cap;
        }
        vs->set.items[vs->set.len++] = value_retain(item);
    }
}
bool value_set_remove(Value set, Value item) {
    ValueStruct *vs = AS_PTR(set);
    for (int i = 0; i < vs->set.len; i++) if (value_equals(vs->set.items[i], item)) {
        value_release(vs->set.items[i]); memmove(&vs->set.items[i], &vs->set.items[i+1], (vs->set.len - i - 1) * sizeof(Value)); vs->set.len--; return true;
    }
    return false;
}

Value value_tuple_new(Value *items, int count) {
    Value v = alloc_v(VAL_TUPLE); AS_TUPLE(v).len = count; AS_TUPLE(v).cap = count;
    AS_TUPLE(v).items = malloc((count > 0 ? count : 1) * sizeof(Value));
    for (int i = 0; i < count; i++) AS_TUPLE(v).items[i] = value_retain(items[i]); return v;
}

Value value_function(const char *name, Param *params, int param_count, ASTNode *body, Env *closure) {
    Value v = alloc_v(VAL_FUNCTION);
    AS_FUNC(v).name = strdup(name ? name : "<anon>");
    AS_FUNC(v).param_count = param_count;
    AS_FUNC(v).params = malloc((param_count > 0 ? param_count : 1) * sizeof(Param));
    for (int i = 0; i < param_count; i++) {
        AS_FUNC(v).params[i].name = strdup(params[i].name);
        AS_FUNC(v).params[i].type_hint = params[i].type_hint ? strdup(params[i].type_hint) : NULL;
        AS_FUNC(v).params[i].default_val = params[i].default_val;
    }
    AS_FUNC(v).body = body; AS_FUNC(v).closure = env_retain(closure);
    AS_FUNC(v).owns_params = true;
    return v;
}

Value value_function_copy(Value proto, Env *closure) {
    Value v = alloc_v(VAL_FUNCTION);
    AS_FUNC(v).name = strdup(AS_FUNC(proto).name);
    AS_FUNC(v).param_count = AS_FUNC(proto).param_count;
    AS_FUNC(v).params = malloc((AS_FUNC(v).param_count > 0 ? AS_FUNC(v).param_count : 1) * sizeof(Param));
    for (int i = 0; i < AS_FUNC(v).param_count; i++) {
        AS_FUNC(v).params[i].name = strdup(AS_FUNC(proto).params[i].name);
        AS_FUNC(v).params[i].type_hint = AS_FUNC(proto).params[i].type_hint ? strdup(AS_FUNC(proto).params[i].type_hint) : NULL;
        AS_FUNC(v).params[i].default_val = AS_FUNC(proto).params[i].default_val;
    }
    AS_FUNC(v).body = AS_FUNC(proto).body; AS_FUNC(v).chunk = AS_FUNC(proto).chunk;
    AS_FUNC(v).closure = env_retain(closure); AS_FUNC(v).owns_params = true; AS_FUNC(v).owns_chunk = false;
    return v;
}

Value value_builtin(const char *name, BuiltinFn fn) { Value v = alloc_v(VAL_BUILTIN); AS_BUILTIN(v).name = strdup(name); AS_BUILTIN(v).fn = fn; return v; }

bool value_equals(Value a, Value b) {
    if (a == b) return true;
    ValueType ta = VAL_TYPE(a), tb = VAL_TYPE(b);
    /* Cross-type numeric comparison: int == float or float == int */
    if (ta != tb) {
        if ((ta == VAL_INT || ta == VAL_FLOAT) && (tb == VAL_INT || tb == VAL_FLOAT)) {
            double va = (ta == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a);
            double vb = (tb == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b);
            return va == vb;
        }
        return false;
    }
    switch (ta) {
        case VAL_INT:    return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT:  return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_STRING: return strcmp(AS_STR(a), AS_STR(b)) == 0;
        case VAL_BOOL:   return a == b;
        case VAL_NULL:   return true;
        case VAL_ARRAY: {
            if (AS_ARRAY(a).len != AS_ARRAY(b).len) return false;
            for (int i = 0; i < AS_ARRAY(a).len; i++)
                if (!value_equals(AS_ARRAY(a).items[i], AS_ARRAY(b).items[i])) return false;
            return true;
        }
        case VAL_TUPLE: {
            if (AS_TUPLE(a).len != AS_TUPLE(b).len) return false;
            for (int i = 0; i < AS_TUPLE(a).len; i++)
                if (!value_equals(AS_TUPLE(a).items[i], AS_TUPLE(b).items[i])) return false;
            return true;
        }
        default: return false;
    }
}
int value_compare(Value a, Value b) {
    ValueType ta = VAL_TYPE(a), tb = VAL_TYPE(b);
    if (ta == VAL_INT && tb == VAL_INT) { long long va = AS_INT(a), vb = AS_INT(b); return (va < vb) ? -1 : (va > vb); }
    if ((ta == VAL_INT || ta == VAL_FLOAT) && (tb == VAL_INT || tb == VAL_FLOAT)) {
        double va = (ta == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a); double vb = (tb == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b); return (va < vb) ? -1 : (va > vb);
    }
    return 0;
}
bool value_truthy(Value v) {
    ValueType t = VAL_TYPE(v); if (t == VAL_NULL) return false; if (t == VAL_BOOL) return AS_BOOL(v) > 0; if (t == VAL_INT) return AS_INT(v) != 0;
    if (t == VAL_FLOAT) return AS_FLOAT(v) != 0.0; if (t == VAL_STRING) return AS_STR(v)[0] != '\0'; if (t == VAL_ARRAY || t == VAL_SET || t == VAL_TUPLE) return AS_ARRAY(v).len > 0; return true;
}
char *value_to_string(Value v) {
    char buf[512];
    switch (VAL_TYPE(v)) {
        case VAL_NULL: return strdup("null"); case VAL_INT: snprintf(buf, sizeof(buf), "%lld", AS_INT(v)); return strdup(buf);
        case VAL_FLOAT: snprintf(buf, sizeof(buf), "%g", AS_FLOAT(v)); return strdup(buf);
        case VAL_BOOL: { int b = AS_BOOL(v); return strdup(b > 0 ? "true" : (b == 0 ? "false" : "unknown")); }
        case VAL_STRING: return strdup(AS_STR(v)); case VAL_COMPLEX: snprintf(buf, sizeof(buf), "%g+%gj", AS_COMPLEX(v).real, AS_COMPLEX(v).imag); return strdup(buf);
        case VAL_ARRAY: snprintf(buf, sizeof(buf), "[array len=%d]", AS_ARRAY(v).len); return strdup(buf);
        case VAL_DICT: snprintf(buf, sizeof(buf), "{dict len=%d}", AS_DICT(v).len); return strdup(buf);
        case VAL_SET: snprintf(buf, sizeof(buf), "set(len=%d)", AS_SET(v).len); return strdup(buf);
        case VAL_TUPLE: snprintf(buf, sizeof(buf), "(tuple len=%d)", AS_TUPLE(v).len); return strdup(buf);
        case VAL_FUNCTION: snprintf(buf, sizeof(buf), "<function %s>", AS_FUNC(v).name); return strdup(buf);
        case VAL_BUILTIN: snprintf(buf, sizeof(buf), "<builtin %s>", AS_BUILTIN(v).name); return strdup(buf);
        default: return strdup("<object>");
    }
}
void value_print(Value v) { char *s = value_to_string(v); printf("%s", s); free(s); }
Value value_copy(Value v) { return value_retain(v); }
const char *value_type_name(ValueType t) {
    static const char *names[] = {"null","int","float","complex","string","bool","array","dict","set","tuple","function","builtin"};
    return (t >= 0 && t <= VAL_BUILTIN) ? names[t] : "unknown";
}
Value value_add(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return value_int(AS_INT(a) + AS_INT(b));
    ValueType ta = VAL_TYPE(a), tb = VAL_TYPE(b);
    if ((ta == VAL_INT || ta == VAL_FLOAT) && (tb == VAL_INT || tb == VAL_FLOAT)) {
        double va = (ta == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a); double vb = (tb == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b); return value_float(va + vb);
    }
    if (ta == VAL_STRING && tb == VAL_STRING) {
        size_t la = strlen(AS_STR(a)), lb = strlen(AS_STR(b)); char *s = malloc(la + lb + 1); memcpy(s, AS_STR(a), la); memcpy(s + la, AS_STR(b), lb); s[la+lb] = '\0'; return value_string_take(s);
    }
    /* Array concatenation */
    if (ta == VAL_ARRAY && tb == VAL_ARRAY) {
        int la = AS_ARRAY(a).len, lb = AS_ARRAY(b).len;
        Value r = value_array_new();
        ValueStruct *rv = AS_PTR(r);
        int total = la + lb;
        free(rv->array.items); /* free the default-allocated items from value_array_new */
        rv->array.items = malloc((total > 0 ? total : 1) * sizeof(Value));
        rv->array.cap = total > 0 ? total : 1;
        rv->array.len = 0;
        for (int i = 0; i < la; i++) rv->array.items[rv->array.len++] = value_retain(AS_ARRAY(a).items[i]);
        for (int i = 0; i < lb; i++) rv->array.items[rv->array.len++] = value_retain(AS_ARRAY(b).items[i]);
        return r;
    }
    return 0;
}
Value value_sub(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return value_int(AS_INT(a) - AS_INT(b));
    ValueType ta = VAL_TYPE(a), tb = VAL_TYPE(b);
    if ((ta == VAL_INT || ta == VAL_FLOAT) && (tb == VAL_INT || tb == VAL_FLOAT)) {
        double va = (ta == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a); double vb = (tb == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b); return value_float(va - vb);
    }
    /* Set difference: a - b removes elements of b from a */
    if (ta == VAL_SET && tb == VAL_SET) {
        Value r = value_set_new();
        for (int i = 0; i < AS_SET(a).len; i++)
            if (!value_set_has(b, AS_SET(a).items[i])) value_set_add(r, AS_SET(a).items[i]);
        return r;
    }
    return 0;
}
Value value_mul(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return value_int(AS_INT(a) * AS_INT(b));
    ValueType ta = VAL_TYPE(a), tb = VAL_TYPE(b);
    if ((ta == VAL_INT || ta == VAL_FLOAT) && (tb == VAL_INT || tb == VAL_FLOAT)) {
        double va = (ta == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a); double vb = (tb == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b); return value_float(va * vb);
    }
    /* String repetition: str * int or int * str */
    const char *src = NULL; long long count = 0;
    if (ta == VAL_STRING && tb == VAL_INT) { src = AS_STR(a); count = AS_INT(b); }
    else if (ta == VAL_INT && tb == VAL_STRING) { src = AS_STR(b); count = AS_INT(a); }
    if (src) {
        if (count <= 0) return value_string("");
        size_t slen = strlen(src);
        char *buf = malloc(slen * (size_t)count + 1);
        for (long long i = 0; i < count; i++) memcpy(buf + i * slen, src, slen);
        buf[slen * count] = '\0';
        return value_string_take(buf);
    }
    return 0;
}
Value value_div(Value a, Value b) { double va = (VAL_TYPE(a) == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a); double vb = (VAL_TYPE(b) == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b); return (vb == 0) ? 0 : value_float(va / vb); }
Value value_mod(Value a, Value b) { if (IS_INT(a) && IS_INT(b)) return (AS_INT(b) == 0) ? 0 : value_int(AS_INT(a) % AS_INT(b)); return 0; }
Value value_pow(Value a, Value b) {
    /* Integer ** non-negative integer => return integer result */
    if (VAL_TYPE(a) == VAL_INT && VAL_TYPE(b) == VAL_INT) {
        long long base = AS_INT(a), exp = AS_INT(b);
        if (exp >= 0) {
            long long result = 1;
            long long bb = base;
            long long ee = exp;
            while (ee > 0) { if (ee & 1) result *= bb; bb *= bb; ee >>= 1; }
            return value_int(result);
        }
    }
    double va = (VAL_TYPE(a) == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a);
    double vb = (VAL_TYPE(b) == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b);
    return value_float(pow(va, vb));
}
Value value_neg(Value v) { if (IS_INT(v)) return value_int(-AS_INT(v)); if (VAL_TYPE(v) == VAL_FLOAT) return value_float(-AS_FLOAT(v)); return 0; }
void value_immortals_init(void) {}
void value_immortals_free(void) {}
void value_dict_clear(Value dict) {
    ValueStruct *vs = AS_PTR(dict); for (int i = 0; i < vs->dict.cap; i++) if (vs->dict.entries[i].key) { value_release(vs->dict.entries[i].key); value_release(vs->dict.entries[i].val); vs->dict.entries[i].key = 0; vs->dict.entries[i].val = 0; }
    vs->dict.len = 0; vs->dict.version++;
}
Value value_dict_get_at(Value dict, int index) { ValueStruct *vs = AS_PTR(dict); return (index < 0 || index >= vs->dict.cap) ? 0 : vs->dict.entries[index].val; }
Value value_dict_get_cached(Value dict, Value key, int *index, unsigned int *version) {
    ValueStruct *vs = AS_PTR(dict); if (*version == vs->dict.version && *index >= 0 && *index < vs->dict.cap) { if (vs->dict.entries[*index].key && value_equals(vs->dict.entries[*index].key, key)) return vs->dict.entries[*index].val; }
    *index = value_dict_find_index(dict, key); *version = vs->dict.version; return (*index >= 0) ? vs->dict.entries[*index].val : 0;
}
int value_dict_find_index(Value dict, Value key) { ValueStruct *vs = AS_PTR(dict); for (int i = 0; i < vs->dict.cap; i++) if (vs->dict.entries[i].key && value_equals(vs->dict.entries[i].key, key)) return i; return -1; }
bool value_dict_remove(Value dict, Value key) {
    ValueStruct *vs = AS_PTR(dict); int idx = value_dict_find_index(dict, key);
    if (idx >= 0) { value_release(vs->dict.entries[idx].key); value_release(vs->dict.entries[idx].val); vs->dict.entries[idx].key = 0; vs->dict.entries[idx].val = 0; vs->dict.len--; vs->dict.version++; return true; }
    return false;
}
