#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "value.h"
#include "gc.h"

Value value_null(void) { return TO_NULL(); }
Value value_bool(int b) { return TO_BOOL(b); }
Value value_int(long long n) { return TO_INT(n); }

Value value_retain(Value v) {
    if (IS_PTR(v)) {
        ValueStruct *vs = AS_PTR(v);
        if (!vs->gc_immortal) vs->ref_count++;
    }
    return v;
}

static void value_free_internal(ValueStruct *vs) {
    if (!vs) return;
    switch (vs->_type_) {
        case VAL_STRING:
            if (vs->_str_val_) free(vs->_str_val_);
            break;
        case VAL_ARRAY: case VAL_SET: case VAL_TUPLE:
            if (vs->_array_.items) {
                for (int i = 0; i < vs->_array_.len; i++) value_release(vs->_array_.items[i]);
                free(vs->_array_.items);
            }
            break;
        case VAL_DICT:
            if (vs->_dict_.entries) {
                for (int i = 0; i < vs->_dict_.cap; i++) {
                    if (vs->_dict_.entries[i].key) {
                        value_release(vs->_dict_.entries[i].key);
                        value_release(vs->_dict_.entries[i].val);
                    }
                }
                free(vs->_dict_.entries);
            }
            break;
        case VAL_FUNCTION:
            if (vs->_func_.name) free(vs->_func_.name);
            break;
        case VAL_BUILTIN:
            if (vs->_builtin_.name) free(vs->_builtin_.name);
            break;
        default: break;
    }
    gc_untrack_value((Value)vs);
    free(vs);
}

void value_release(Value v) {
    if (!IS_PTR(v)) return;
    ValueStruct *vs = AS_PTR(v);
    if (vs->gc_immortal) return;
    if (--vs->ref_count <= 0) {
        value_free_internal(vs);
    }
}

static ValueStruct *alloc_struct(ValueType t) {
    ValueStruct *vs = calloc(1, sizeof(ValueStruct));
    vs->_type_ = t;
    vs->ref_count = 1;
    gc_track_value((Value)vs);
    return vs;
}

Value value_float(double d) { ValueStruct *vs = alloc_struct(VAL_FLOAT); vs->_float_val_ = d; return (Value)vs; }
Value value_complex(double r, double i) { ValueStruct *vs = alloc_struct(VAL_COMPLEX); vs->_complex_val_._real_ = r; vs->_complex_val_._imag_ = i; return (Value)vs; }
Value value_string(const char *s) { ValueStruct *vs = alloc_struct(VAL_STRING); vs->_str_val_ = strdup(s); return (Value)vs; }
Value value_string_take(char *s) { ValueStruct *vs = alloc_struct(VAL_STRING); vs->_str_val_ = s; return (Value)vs; }
Value value_string_intern(const char *s) { return gc_intern_string(gc_global(), s); }

Value value_array_new(void) {
    ValueStruct *vs = alloc_struct(VAL_ARRAY);
    vs->_array_.cap = 8;
    vs->_array_.items = calloc(8, sizeof(Value));
    return (Value)vs;
}

void value_array_push(Value arr, Value item) {
    ValueStruct *vs = AS_PTR(arr);
    if (vs->_array_.len >= vs->_array_.cap) {
        vs->_array_.cap = vs->_array_.cap < 8 ? 8 : vs->_array_.cap * 2;
        vs->_array_.items = realloc(vs->_array_.items, (size_t)vs->_array_.cap * sizeof(Value));
    }
    vs->_array_.items[vs->_array_.len++] = value_retain(item);
}

Value value_array_get(Value arr, long long idx) {
    ValueStruct *vs = AS_PTR(arr);
    if (idx < 0) idx += vs->_array_.len;
    if (idx < 0 || idx >= vs->_array_.len) return TO_NULL();
    return vs->_array_.items[idx];
}

Value value_dict_new(void) {
    ValueStruct *vs = alloc_struct(VAL_DICT);
    vs->_dict_.cap = 8;
    vs->_dict_.entries = calloc(8, sizeof(DictEntry));
    return (Value)vs;
}

Value value_set_new(void) { return value_array_new(); }
Value value_tuple_new(Value *items, int count) {
    ValueStruct *vs = alloc_struct(VAL_TUPLE);
    vs->_tuple_.len = count;
    vs->_tuple_.items = malloc((size_t)count * sizeof(Value));
    if (items) {
        for (int i = 0; i < count; i++) vs->_tuple_.items[i] = value_retain(items[i]);
    }
    return (Value)vs;
}

Value value_range_new(long long start, long long stop, long long step) {
    ValueStruct *vs = alloc_struct(VAL_RANGE);
    vs->_range_.start = start;
    vs->_range_.stop = stop;
    vs->_range_.step = step;
    return (Value)vs;
}

Value value_function(const char *n, Param *p, int pc, ASTNode *b, Env *c) {
    ValueStruct *vs = alloc_struct(VAL_FUNCTION);
    vs->_func_.name = strdup(n ? n : "<anon>");
    vs->_func_.params = p;
    vs->_func_.param_count = pc;
    vs->_func_.body = b;
    vs->_func_.closure = c;
    return (Value)vs;
}

Value value_builtin(const char *n, BuiltinFn f) {
    ValueStruct *vs = alloc_struct(VAL_BUILTIN);
    vs->_builtin_.name = strdup(n);
    vs->_builtin_.fn = f;
    return (Value)vs;
}

void value_dict_set(Value dict, Value key, Value val) {
    ValueStruct *vs = AS_PTR(dict);
    for (int i = 0; i < vs->_dict_.cap; i++) {
        if (vs->_dict_.entries[i].key && value_equals(vs->_dict_.entries[i].key, key)) {
            value_release(vs->_dict_.entries[i].val);
            vs->_dict_.entries[i].val = value_retain(val);
            return;
        }
    }
    if (vs->_dict_.len >= vs->_dict_.cap) {
        int old_cap = vs->_dict_.cap;
        DictEntry *old = vs->_dict_.entries;
        vs->_dict_.cap *= 2;
        vs->_dict_.entries = calloc((size_t)vs->_dict_.cap, sizeof(DictEntry));
        vs->_dict_.len = 0;
        for (int i = 0; i < old_cap; i++) {
            if (old[i].key) value_dict_set(dict, old[i].key, old[i].val);
        }
        for (int i = 0; i < old_cap; i++) {
            if (old[i].key) { value_release(old[i].key); value_release(old[i].val); }
        }
        free(old);
    }
    for (int i = 0; i < vs->_dict_.cap; i++) {
        if (!vs->_dict_.entries[i].key) {
            vs->_dict_.entries[i].key = value_retain(key);
            vs->_dict_.entries[i].val = value_retain(val);
            vs->_dict_.len++;
            return;
        }
    }
}

Value value_dict_get(Value dict, Value key) {
    ValueStruct *vs = AS_PTR(dict);
    for (int i = 0; i < vs->_dict_.cap; i++) {
        if (vs->_dict_.entries[i].key && value_equals(vs->_dict_.entries[i].key, key))
            return vs->_dict_.entries[i].val;
    }
    return TO_NULL();
}

bool value_equals(Value a, Value b) {
    if (a == b) return true;
    ValueType ta = VAL_TYPE(a), tb = VAL_TYPE(b);
    if (ta != tb) return false;
    if (IS_INT(a)) return AS_INT(a) == AS_INT(b);
    if (ta == VAL_STRING) return strcmp(AS_STR(a), AS_STR(b)) == 0;
    if (ta == VAL_FLOAT) return AS_FLOAT(a) == AS_FLOAT(b);
    return false;
}

int value_compare(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) {
        long long va = AS_INT(a), vb = AS_INT(b);
        return (va < vb) ? -1 : (va > vb);
    }
    return 0;
}

bool value_truthy(Value v) {
    if (IS_NULL(v)) return false;
    if (IS_INT(v)) return AS_INT(v) != 0;
    if (IS_SPEC(v)) return AS_BOOL(v) == 1;
    if (VAL_TYPE(v) == VAL_STRING) return AS_STR(v)[0] != '\0';
    if (VAL_TYPE(v) == VAL_ARRAY) return AS_ARRAY(v).len > 0;
    return true;
}

char *value_to_string(Value v) {
    char buf[128];
    ValueType t = VAL_TYPE(v);
    if (IS_INT(v)) { snprintf(buf, sizeof(buf), "%lld", AS_INT(v)); return strdup(buf); }
    if (IS_NULL(v)) return strdup("null");
    if (IS_UNDEFINED(v)) return strdup("undefined");
    if (t == VAL_BOOL) return strdup(AS_BOOL(v) == 1 ? "true" : "false");
    ValueStruct *vs = AS_PTR(v);
    if (t == VAL_STRING) return strdup(vs->_str_val_);
    if (t == VAL_FLOAT) { snprintf(buf, sizeof(buf), "%g", vs->_float_val_); return strdup(buf); }
    if (t == VAL_ARRAY) {
        int cap = 128, sz = 1; char *res = malloc(cap); res[0] = '[';
        for (int i = 0; i < vs->_array_.len; i++) {
            char *s = value_to_string(vs->_array_.items[i]);
            size_t len = strlen(s);
            while (sz + len + 4 >= (size_t)cap) { cap *= 2; res = realloc(res, (size_t)cap); }
            memcpy(res + sz, s, len); sz += len; free(s);
            if (i < vs->_array_.len - 1) { res[sz++] = ','; res[sz++] = ' '; }
        }
        res[sz++] = ']'; res[sz] = '\0'; return res;
    }
    snprintf(buf, sizeof(buf), "<%s>", value_type_name(t));
    return strdup(buf);
}

void value_print(Value v) { char *s = value_to_string(v); printf("%s", s); free(s); }
Value value_copy(Value v) { return value_retain(v); }

const char *value_type_name(ValueType t) {
    switch (t) {
        case VAL_NULL: return "null";
        case VAL_INT: return "int";
        case VAL_FLOAT: return "float";
        case VAL_STRING: return "string";
        case VAL_BOOL: return "bool";
        case VAL_ARRAY: return "array";
        case VAL_DICT: return "dict";
        case VAL_RANGE: return "range";
        default: return "unknown";
    }
}

Value value_add(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return TO_INT(AS_INT(a) + AS_INT(b));
    double va = (VAL_TYPE(a) == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a);
    double vb = (VAL_TYPE(b) == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b);
    return value_float(va + vb);
}

Value value_sub(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return TO_INT(AS_INT(a) - AS_INT(b));
    double va = (VAL_TYPE(a) == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a);
    double vb = (VAL_TYPE(b) == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b);
    return value_float(va - vb);
}

Value value_mul(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return TO_INT(AS_INT(a) * AS_INT(b));
    double va = (VAL_TYPE(a) == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a);
    double vb = (VAL_TYPE(b) == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b);
    return value_float(va * vb);
}

Value value_div(Value a, Value b) {
    double va = (VAL_TYPE(a) == VAL_INT) ? (double)AS_INT(a) : AS_FLOAT(a);
    double vb = (VAL_TYPE(b) == VAL_INT) ? (double)AS_INT(b) : AS_FLOAT(b);
    if (vb == 0) return TO_NULL();
    return value_float(va / vb);
}

Value value_mod(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return TO_INT(AS_INT(a) % AS_INT(b));
    return value_float(0);
}

Value value_pow(Value a, Value b) {
    return value_float(pow((VAL_TYPE(a)==VAL_INT?(double)AS_INT(a):AS_FLOAT(a)), (VAL_TYPE(b)==VAL_INT?(double)AS_INT(b):AS_FLOAT(b))));
}

Value value_neg(Value a) {
    if (IS_INT(a)) return TO_INT(-AS_INT(a));
    if (VAL_TYPE(a) == VAL_FLOAT) return value_float(-AS_FLOAT(a));
    return TO_NULL();
}

void value_array_insert(Value a, long long i, Value it) { (void)a; (void)i; (void)it; }
bool value_array_remove(Value a, Value it) { (void)a; (void)it; return false; }
Value value_array_pop(Value a, long long i) { (void)a; (void)i; return TO_NULL(); }
void value_array_sort(Value a) { (void)a; }
void value_array_extend(Value a, Value o) { (void)a; (void)o; }
int value_dict_find_index(Value d, Value k) { (void)d; (void)k; return -1; }
Value value_dict_get_at(Value d, int i) { (void)d; (void)i; return TO_NULL(); }
Value value_dict_get_cached(Value d, Value k, int *i, unsigned int *v) { (void)d; (void)k; (void)i; (void)v; return TO_NULL(); }
void value_dict_clear(Value d) { (void)d; }
bool value_set_has(Value s, Value it) { (void)s; (void)it; return false; }
void value_set_add(Value s, Value it) { (void)s; (void)it; }
bool value_set_remove(Value s, Value it) { (void)s; (void)it; return false; }
