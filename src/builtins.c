#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "builtins.h"
#include "value.h"
#include "interpreter.h"
#include "gc.h"

static Value bi_output(Value *args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (VAL_TYPE(args[i]) == VAL_STRING) printf("%s", AS_STR(args[i]));
        else value_print(args[i]);
    }
    printf("\n");
    return value_null();
}

static Value bi_len(Value *args, int argc) {
    if (argc < 1) return value_int(0);
    Value v = args[0];
    switch (VAL_TYPE(v)) {
        case VAL_STRING: return value_int(strlen(AS_STR(v)));
        case VAL_ARRAY:  return value_int(AS_ARRAY(v).len);
        case VAL_DICT:   return value_int(AS_DICT(v).len);
        case VAL_SET:    return value_int(AS_SET(v).len);
        case VAL_TUPLE:  return value_int(AS_TUPLE(v).len);
        default: return value_int(0);
    }
}

static Value bi_int_fn(Value *args, int argc) {
    if (argc < 1) return value_int(0);
    Value v = args[0];
    if (VAL_TYPE(v) == VAL_INT) return v;
    if (VAL_TYPE(v) == VAL_FLOAT) return value_int((long long)AS_FLOAT(v));
    if (VAL_TYPE(v) == VAL_STRING) return value_int(atoll(AS_STR(v)));
    return value_int(0);
}

static Value bi_float_fn(Value *args, int argc) {
    if (argc < 1) return value_float(0.0);
    Value v = args[0];
    if (VAL_TYPE(v) == VAL_FLOAT) return v;
    if (VAL_TYPE(v) == VAL_INT) return value_float((double)AS_INT(v));
    if (VAL_TYPE(v) == VAL_STRING) return value_float(atof(AS_STR(v)));
    return value_float(0.0);
}

static Value bi_str_fn(Value *args, int argc) {
    if (argc < 1) return value_string("");
    char *s = value_to_string(args[0]);
    Value res = value_string_take(s);
    return res;
}

static Value bi_clock(Value *args, int argc) {
    (void)args; (void)argc;
    return value_float((double)clock() / CLOCKS_PER_SEC);
}

static Value bi_range(Value *args, int argc) {
    long long start = 0, end = 0, step = 1;
    if (argc == 1) end = AS_INT(args[0]);
    else if (argc >= 2) { start = AS_INT(args[0]); end = AS_INT(args[1]); }
    if (argc >= 3) step = AS_INT(args[2]);
    if (step == 0) step = 1;
    Value arr = value_array_new();
    if (step > 0) {
        for (long long i = start; i < end; i += step) value_array_push(arr, value_int(i));
    } else {
        for (long long i = start; i > end; i += step) value_array_push(arr, value_int(i));
    }
    return arr;
}

void prism_register_stdlib(Env *env) {
    struct { const char *name; BuiltinFn fn; } tbl[] = {
        {"output", bi_output},
        {"len",    bi_len},
        {"int",    bi_int_fn},
        {"float",  bi_float_fn},
        {"str",    bi_str_fn},
        {"clock",  bi_clock},
        {"range",  bi_range},
        {NULL, NULL}
    };
    for (int i = 0; tbl[i].name; i++) {
        Value v = value_builtin(tbl[i].name, tbl[i].fn);
        env_set(env, tbl[i].name, v, false);
        value_release(v);
    }
}
