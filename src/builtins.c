#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include "builtins.h"
#include "value.h"
#include "interpreter.h"
#include "gc.h"

static Value bi_output(Value *args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        char *s = value_to_string(args[i]); printf("%s", s); free(s);
    }
    printf("\n"); return value_null();
}

static Value bi_int(Value *args, int argc) { if (argc < 1) return value_int(0); Value v = args[0]; if (VAL_TYPE(v) == VAL_INT) return value_retain(v); if (VAL_TYPE(v) == VAL_FLOAT) return value_int((long long)AS_FLOAT(v)); if (VAL_TYPE(v) == VAL_STRING) return value_int(strtoll(AS_STR(v), NULL, 0)); return value_int(0); }
static Value bi_float(Value *args, int argc) { if (argc < 1) return value_float(0.0); Value v = args[0]; if (VAL_TYPE(v) == VAL_FLOAT) return value_retain(v); if (VAL_TYPE(v) == VAL_INT) return value_float((double)AS_INT(v)); if (VAL_TYPE(v) == VAL_STRING) return value_float(strtod(AS_STR(v), NULL)); return value_float(0.0); }
static Value bi_str(Value *args, int argc) { if (argc < 1) return value_string(""); char *s = value_to_string(args[0]); return value_string_take(s); }
static Value bi_len(Value *args, int argc) { if (argc < 1) return value_int(0); Value v = args[0]; if (VAL_TYPE(v) == VAL_STRING) return value_int(strlen(AS_STR(v))); if (VAL_TYPE(v) == VAL_ARRAY) return value_int(AS_ARRAY(v).len); return value_int(0); }
static Value bi_type(Value *args, int argc) { if (argc < 1) return value_string("null"); return value_string(value_type_name(VAL_TYPE(args[0]))); }

static Value bi_range(Value *args, int n) {
    long long start = 0, stop = 0, step = 1;
    if (n == 1) stop = (VAL_TYPE(args[0]) == VAL_INT) ? AS_INT(args[0]) : (long long)AS_FLOAT(args[0]);
    else if (n >= 2) {
        start = (VAL_TYPE(args[0]) == VAL_INT) ? AS_INT(args[0]) : (long long)AS_FLOAT(args[0]);
        stop = (VAL_TYPE(args[1]) == VAL_INT) ? AS_INT(args[1]) : (long long)AS_FLOAT(args[1]);
        if (n >= 3) step = (VAL_TYPE(args[2]) == VAL_INT) ? AS_INT(args[2]) : (long long)AS_FLOAT(args[2]);
    }
    if (step == 0) step = 1;
    Value arr = value_array_new();
    if (step > 0) { for (long long i = start; i < stop; i += step) value_array_push(arr, value_int(i)); }
    else { for (long long i = start; i > stop; i += step) value_array_push(arr, value_int(i)); }
    return arr;
}

void prism_register_stdlib(Env *env) {
    struct { const char *name; BuiltinFn fn; } tbl[] = {
        {"output", bi_output}, {"int", bi_int}, {"float", bi_float}, {"str", bi_str}, {"len", bi_len}, {"type", bi_type}, {"range", bi_range}, {NULL, NULL}
    };
    for (int i = 0; tbl[i].name; i++) { Value v = value_builtin(tbl[i].name, tbl[i].fn); env_set(env, tbl[i].name, v, false); value_release(v); }
}
