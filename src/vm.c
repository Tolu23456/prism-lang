#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "vm.h"
#include "opcode.h"
#include "chunk.h"
#include "value.h"
#include "interpreter.h"
#include "compiler.h"
#include "builtins.h"
#include "jit.h"
#include "parser.h"
#include "ast.h"
#ifdef HAVE_X11
#include "xgui.h"
#endif

/* ================================================================== helpers */

/* Forward declaration needed by vm_error for stack/frame unwinding. */
static void vm_close_frame_env(CallFrame *frame);

static void vm_error(VM *vm, const char *msg, int line) {
    if (vm->had_error) return;

    /* If execution is inside a try block, divert the error to the catch handler
     * instead of terminating.  This avoids the overhead of setjmp/longjmp while
     * still providing correct exception semantics. */
    if (vm->try_depth > 0) {
        VMTryFrame *tf = &vm->try_frames[vm->try_depth - 1];

        /* Save the full error message before we reset state. */
        snprintf(vm->exception_msg, sizeof(vm->exception_msg),
                 "line %d: %s", line, msg);

        /* Unwind any call frames that were pushed after the try was entered. */
        while (vm->frame_count > tf->frame_count) {
            CallFrame *f = &vm->frames[vm->frame_count - 1];
            for (int _li = 0; _li < f->local_count; _li++) {
                if (f->locals[_li]) {
                    value_release(f->locals[_li]);
                    f->locals[_li] = 0;
                }
            }
            f->local_count = 0;
            vm_close_frame_env(f);
            if (f->func) { value_release(f->func); f->func = 0; }
            vm->frame_count--;
        }

        /* Discard any stack values pushed since the try block began. */
        while (vm->stack_top > tf->stack_top)
            value_release(vm->stack[--vm->stack_top]);

        /* Redirect the current frame's instruction pointer to the catch handler. */
        vm->frames[vm->frame_count - 1].ip = tf->handler_ip;

        /* Push the exception message string as the caught error value. */
        vm->stack[vm->stack_top++] = value_string(vm->exception_msg);

        /* Pop this try frame — it has been consumed. */
        vm->try_depth--;

        /* Signal DISPATCH() to re-sync its local `frame` pointer. */
        vm->exception_handled = 1;
        return;
    }

    vm->had_error = 1;
    snprintf(vm->error_msg, sizeof(vm->error_msg), "line %d: %s", line, msg);
}

/* Free a single scope env without recursing into the parent chain.
 * Used by vm_close_frame_env when manually walking up the env chain to avoid
 * double-frees while also correctly releasing scope-local values. */
static void env_free_scope_only(Env *env) {
    if (!env) return;
    /* For root envs (no parent), refcount is not managed here. */
    if (!env->parent) return;
    if (--env->refcount > 0) return; /* still referenced by a closure */
    /* Break function-closure back-references to prevent double-free cycles.
     * Functions whose closure IS this env would call env_free(env) again
     * during their own cleanup if we don't null the back-pointer first. */
    for (int i = 0; i < env->cap; i++) {
        if (!env->slots[i].key) continue;
        Value v = env->slots[i].val;
        if (IS_PTR(v) && AS_PTR(v)->type == VAL_FUNCTION) {
            if (AS_PTR(v)->func.closure == env)
                AS_PTR(v)->func.closure = NULL;
        }
    }
    /* Free slot values and the memory for this env only. */
    for (int i = 0; i < env->cap; i++)
        if (env->slots[i].key) value_release(env->slots[i].val);
    free(env->slots);
    /* Release the reference we took on parent via env_new, without freeing it
     * (the caller will walk up to it next). */
    env_free(env->parent);
    free(env);
}

static void vm_close_frame_env(CallFrame *frame) {
    /* Walk up any inner scopes pushed by OP_PUSH_SCOPE that weren't closed. */
    while (frame->env && frame->env != frame->root_env) {
        Env *old = frame->env;
        frame->env = old->parent; /* advance before freeing */
        env_free_scope_only(old);
    }
    /* Release the function's root call env (preserving parent for closures). */
    if (frame->owns_env && frame->root_env) {
        env_free(frame->root_env); /* correct refcount decrement, no parent nulling */
    }
    frame->env = NULL;
    frame->root_env = NULL;
    frame->owns_env = 0;
}

/* Push / pop helpers — bounds checked only in debug builds. */
static inline void vm_push(VM *vm, Value v) {
#ifndef NDEBUG
    if (vm->stack_top >= VM_STACK_MAX) {
        vm->had_error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "stack overflow");
        return;
    }
#endif
    vm->stack[vm->stack_top++] = v;
}

static inline Value vm_pop(VM *vm) {
#ifndef NDEBUG
    if (vm->stack_top <= 0) {
        vm->had_error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "stack underflow");
        return value_null();
    }
#endif
    return vm->stack[--vm->stack_top];
}

static inline Value vm_peek(VM *vm, int offset) {
    return vm->stack[vm->stack_top - 1 - offset];
}

static inline long long vm_fast_iadd(long long a, long long b) { return a + b; }
static inline long long vm_fast_isub(long long a, long long b) { return a - b; }
static inline long long vm_fast_imul(long long a, long long b) { return a * b; }
static inline long long vm_fast_iand(long long a, long long b) { return a & b; }
static inline long long vm_fast_ior (long long a, long long b) { return a | b; }
static inline long long vm_fast_ixor(long long a, long long b) { return a ^ b; }

/* Read a uint16_t from bytecode (little-endian) and advance ip by 2. */
static inline uint16_t read16(CallFrame *f) {
    uint8_t lo = f->chunk->code[f->ip++];
    uint8_t hi = f->chunk->code[f->ip++];
    return (uint16_t)(lo | (hi << 8));
}

/* ================================================================== built-in output via VM */

static Value vm_builtin_output(Value *args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (VAL_TYPE(args[i]) == VAL_STRING) printf("%s", AS_STR(args[i]));
        else value_print(args[i]);
    }
    printf("\n");
    return value_null();
}

static Value vm_builtin_input(Value *args, int argc) {
    if (argc > 0 && VAL_TYPE(args[0]) == VAL_STRING)
        printf("%s", AS_STR(args[0]));
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return value_string("");
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return value_string(buf);
}

static Value vm_builtin_len(Value *args, int argc) {
    if (argc < 1) return value_int(0);
    Value v = args[0];
    switch (VAL_TYPE(v)) {
        case VAL_STRING: return value_int((long long)strlen(AS_STR(v)));
        case VAL_ARRAY:  return value_int(AS_ARRAY(v).len);
        case VAL_TUPLE:  return value_int(AS_TUPLE(v).len);
        case VAL_DICT:   return value_int(AS_DICT(v).len);
        case VAL_SET:    return value_int(AS_SET(v).len);
        default: return value_int(0);
    }
}

static Value vm_builtin_bool_fn(Value *args, int argc) {
    if (argc < 1) return value_bool(0);
    Value v = args[0];
    if (VAL_TYPE(v) == VAL_BOOL) return value_retain(v);
    if (VAL_TYPE(v) == VAL_INT)  return value_bool(AS_INT(v) == 0 ? 0 : (AS_INT(v) < 0 ? -1 : 1));
    if (VAL_TYPE(v) == VAL_STRING) {
        if (strcmp(AS_STR(v), "true")    == 0) return value_bool(1);
        if (strcmp(AS_STR(v), "false")   == 0) return value_bool(0);
        if (strcmp(AS_STR(v), "unknown") == 0) return value_bool(-1);
    }
    return value_bool(value_truthy(v) ? 1 : 0);
}

static Value vm_builtin_int_fn(Value *args, int argc) {
    if (argc < 1) return value_int(0);
    Value v = args[0];
    if (VAL_TYPE(v) == VAL_INT)     return value_retain(v);
    if (VAL_TYPE(v) == VAL_FLOAT)   return value_int((long long)AS_FLOAT(v));
    if (VAL_TYPE(v) == VAL_BOOL)    return value_int(AS_BOOL(v) == 1 ? 1 : 0);
    if (VAL_TYPE(v) == VAL_NULL)    return value_int(0);
    if (VAL_TYPE(v) == VAL_COMPLEX) return value_int((long long)AS_COMPLEX(v).real);
    if (VAL_TYPE(v) == VAL_STRING) {
        const char *s = AS_STR(v);
        if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
            return value_int(strtoll(s + 2, NULL, 2));
        if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
            return value_int(strtoll(s + 2, NULL, 8));
        return value_int(strtoll(s, NULL, 0));
    }
    return value_int(0);
}

static Value vm_builtin_float_fn(Value *args, int argc) {
    if (argc < 1) return value_float(0.0);
    Value v = args[0];
    if (VAL_TYPE(v) == VAL_FLOAT)   return value_retain(v);
    if (VAL_TYPE(v) == VAL_INT)     return value_float((double)AS_INT(v));
    if (VAL_TYPE(v) == VAL_BOOL)    return value_float(AS_BOOL(v) == 1 ? 1.0 : 0.0);
    if (VAL_TYPE(v) == VAL_NULL)    return value_float(0.0);
    if (VAL_TYPE(v) == VAL_COMPLEX) return value_float(AS_COMPLEX(v).real);
    if (VAL_TYPE(v) == VAL_STRING)  return value_float(strtod(AS_STR(v), NULL));
    return value_float(0.0);
}

static Value vm_builtin_str_fn(Value *args, int argc) {
    if (argc < 1) return value_string("");
    return value_string_take(value_to_string(args[0]));
}

static Value vm_builtin_set_fn(Value *args, int argc) {
    Value s = value_set_new();
    if (argc >= 1) {
        Value src = args[0];
        if (VAL_TYPE(src) == VAL_ARRAY || VAL_TYPE(src) == VAL_TUPLE) {
            ValueArray *arr = (VAL_TYPE(src) == VAL_ARRAY) ? &AS_ARRAY(src) : &AS_TUPLE(src);
            for (int i = 0; i < arr->len; i++) value_set_add(s, arr->items[i]);
        } else if (VAL_TYPE(src) == VAL_SET) {
            for (int i = 0; i < AS_SET(src).len; i++) value_set_add(s, AS_SET(src).items[i]);
        } else if (VAL_TYPE(src) == VAL_DICT) {
            for (int i = 0; i < AS_DICT(src).len; i++) value_set_add(s, AS_DICT(src).entries[i].key);
        } else if (VAL_TYPE(src) == VAL_STRING) {
            const char *p = AS_STR(src);
            while (*p) {
                char buf[5] = {0};
                buf[0] = *p++;
                value_set_add(s, value_string(buf));
            }
        }
    }
    return s;
}

static Value vm_builtin_array_fn(Value *args, int argc) {
    Value a = value_array_new();
    if (argc >= 1) {
        Value src = args[0];
        if (VAL_TYPE(src) == VAL_ARRAY) {
            for (int i = 0; i < AS_ARRAY(src).len; i++) value_array_push(a, AS_ARRAY(src).items[i]);
        } else if (VAL_TYPE(src) == VAL_TUPLE) {
            for (int i = 0; i < AS_TUPLE(src).len; i++) value_array_push(a, AS_TUPLE(src).items[i]);
        } else if (VAL_TYPE(src) == VAL_SET) {
            for (int i = 0; i < AS_SET(src).len; i++) value_array_push(a, AS_SET(src).items[i]);
        } else if (VAL_TYPE(src) == VAL_DICT) {
            for (int i = 0; i < AS_DICT(src).len; i++) value_array_push(a, AS_DICT(src).entries[i].key);
        } else if (VAL_TYPE(src) == VAL_STRING) {
            const char *p = AS_STR(src);
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

static Value vm_builtin_tuple_fn(Value *args, int argc) {
    if (argc < 1) return value_tuple_new(NULL, 0);
    Value src = args[0];
    if (VAL_TYPE(src) == VAL_TUPLE) return value_retain(src);
    if (VAL_TYPE(src) == VAL_ARRAY)
        return value_tuple_new(AS_ARRAY(src).items, AS_ARRAY(src).len);
    if (VAL_TYPE(src) == VAL_SET)
        return value_tuple_new(AS_SET(src).items, AS_SET(src).len);
    if (VAL_TYPE(src) == VAL_DICT) {
        Value *keys = malloc(AS_DICT(src).len * sizeof(Value *));
        for (int i = 0; i < AS_DICT(src).len; i++) keys[i] = AS_DICT(src).entries[i].key;
        Value t = value_tuple_new(keys, AS_DICT(src).len);
        free(keys);
        return t;
    }
    return value_tuple_new(&src, 1);
}

static Value vm_builtin_complex_fn(Value *args, int argc) {
    if (argc < 1) return value_complex(0.0, 0.0);
    double real = 0.0, imag = 0.0;
    if (argc >= 1) {
        Value v = args[0];
        if (VAL_TYPE(v) == VAL_COMPLEX)  return value_retain(v);
        if (VAL_TYPE(v) == VAL_INT)      real = (double)AS_INT(v);
        else if (VAL_TYPE(v) == VAL_FLOAT) real = AS_FLOAT(v);
        else if (VAL_TYPE(v) == VAL_STRING) {
            char *end;
            real = strtod(AS_STR(v), &end);
            if (*end == '+' || *end == '-') imag = strtod(end, NULL);
        }
    }
    if (argc >= 2) {
        Value v = args[1];
        if (VAL_TYPE(v) == VAL_INT)        imag = (double)AS_INT(v);
        else if (VAL_TYPE(v) == VAL_FLOAT) imag = AS_FLOAT(v);
    }
    return value_complex(real, imag);
}

static Value vm_builtin_type_fn(Value *args, int argc) {
    if (argc < 1) return value_string("null");
    return value_string(value_type_name(VAL_TYPE(args[0])));
}

static Value vm_builtin_assert(Value *args, int argc) {
    if (argc < 1 || !value_truthy(args[0])) {
        const char *msg = (argc > 1 && VAL_TYPE(args[1]) == VAL_STRING)
            ? AS_STR(args[1]) : "assertion failed";
        fprintf(stderr, "[FAIL] %s\n", msg);
        exit(1);
    }
    return value_null();
}

static Value vm_builtin_assert_eq(Value *args, int argc) {
    if (argc < 2) {
        fprintf(stderr, "[FAIL] assert_eq requires 2 arguments\n");
        exit(1);
    }
    if (!value_equals(args[0], args[1])) {
        if (argc > 2 && VAL_TYPE(args[2]) == VAL_STRING) {
            fprintf(stderr, "[FAIL] %s\n", AS_STR(args[2]));
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

static Value vm_builtin_round(Value *args, int argc) {
    if (argc < 1) return value_null();
    double v = VAL_TYPE(args[0]) == VAL_INT ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    int places = (argc >= 2 && VAL_TYPE(args[1]) == VAL_INT) ? (int)AS_INT(args[1]) : 0;
    double factor = pow(10.0, places);
    double r = round(v * factor) / factor;
    return places == 0 ? value_int((long long)r) : value_float(r);
}
static Value vm_builtin_abs(Value *args, int argc) {
    if (argc < 1) return value_null();
    if (VAL_TYPE(args[0]) == VAL_INT) return value_int(llabs(AS_INT(args[0])));
    if (VAL_TYPE(args[0]) == VAL_FLOAT) return value_float(fabs(AS_FLOAT(args[0])));
    return value_null();
}
static Value vm_builtin_floor(Value *args, int argc) {
    if (argc < 1) return value_null();
    double v = VAL_TYPE(args[0]) == VAL_INT ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return value_int((long long)floor(v));
}
static Value vm_builtin_ceil(Value *args, int argc) {
    if (argc < 1) return value_null();
    double v = VAL_TYPE(args[0]) == VAL_INT ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return value_int((long long)ceil(v));
}
static Value vm_builtin_min(Value *args, int argc) {
    if (argc == 0) return value_null();
    if (argc == 1 && VAL_TYPE(args[0]) == VAL_ARRAY) {
        int n = AS_ARRAY(args[0]).len;
        if (n == 0) return value_null();
        Value m = AS_ARRAY(args[0]).items[0];
        for (int i = 1; i < n; i++) {
            Value el = AS_ARRAY(args[0]).items[i];
            if (value_compare(el, m) < 0) m = el;
        }
        return value_retain(m);
    }
    Value m = args[0];
    for (int i = 1; i < argc; i++) if (value_compare(args[i], m) < 0) m = args[i];
    return value_retain(m);
}
static Value vm_builtin_max(Value *args, int argc) {
    if (argc == 0) return value_null();
    if (argc == 1 && VAL_TYPE(args[0]) == VAL_ARRAY) {
        int n = AS_ARRAY(args[0]).len;
        if (n == 0) return value_null();
        Value m = AS_ARRAY(args[0]).items[0];
        for (int i = 1; i < n; i++) {
            Value el = AS_ARRAY(args[0]).items[i];
            if (value_compare(el, m) > 0) m = el;
        }
        return value_retain(m);
    }
    Value m = args[0];
    for (int i = 1; i < argc; i++) if (value_compare(args[i], m) > 0) m = args[i];
    return value_retain(m);
}
static Value vm_builtin_pow(Value *args, int argc) {
    if (argc < 2) return value_null();
    double base = VAL_TYPE(args[0]) == VAL_INT ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    double exp  = VAL_TYPE(args[1]) == VAL_INT ? (double)AS_INT(args[1]) : AS_FLOAT(args[1]);
    return value_float(pow(base, exp));
}
static Value vm_builtin_sqrt(Value *args, int argc) {
    if (argc < 1) return value_null();
    double v = VAL_TYPE(args[0]) == VAL_INT ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return value_float(sqrt(v));
}
static Value vm_builtin_print(Value *args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        char *s = value_to_string(args[i]);
        printf("%s", s);
        free(s);
    }
    printf("\n");
    return value_null();
}

/* ================================================================== stdlib builtins */

/* --- Array mutating --- */
static Value vm_builtin_push(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_null();
    value_array_push(args[0], value_retain(args[1]));
    return value_null();
}
static Value vm_builtin_pop(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_null();
    int n = AS_ARRAY(args[0]).len;
    if (n == 0) return value_null();
    Value v = value_array_pop(args[0], (long long)(n - 1));
    return v;
}
static Value vm_builtin_append(Value *args, int argc) {
    return vm_builtin_push(args, argc);
}

/* --- Array non-mutating --- */
static Value vm_builtin_reverse(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_null();
    int n = AS_ARRAY(args[0]).len;
    Value *items = AS_ARRAY(args[0]).items;
    for (int i = 0; i < n / 2; i++) {
        Value tmp = items[i]; items[i] = items[n-1-i]; items[n-1-i] = tmp;
    }
    return value_null();
}
static Value vm_bi_delete(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_DICT) return value_null();
    if (VAL_TYPE(args[1]) != VAL_STRING) return value_null();
    const char *key = AS_STR(args[1]);
    ValueStruct *vs = AS_PTR(args[0]);
    for (int i = 0; i < vs->dict.cap; i++) {
        if (vs->dict.entries[i].key && VAL_TYPE(vs->dict.entries[i].key) == VAL_STRING &&
            strcmp(AS_STR(vs->dict.entries[i].key), key) == 0) {
            value_release(vs->dict.entries[i].key);
            value_release(vs->dict.entries[i].val);
            vs->dict.entries[i].key = 0;
            vs->dict.entries[i].val = 0;
            vs->dict.len--;
            return value_null();
        }
    }
    return value_null();
}
static Value vm_builtin_copy(Value *args, int argc) {
    if (argc < 1) return value_array_new();
    if (VAL_TYPE(args[0]) == VAL_ARRAY) {
        Value out = value_array_new();
        for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
            value_array_push(out, value_retain(AS_ARRAY(args[0]).items[i]));
        return out;
    }
    return value_retain(args[0]);
}
static Value vm_builtin_sorted(Value *args, int argc) {
    Value cp = vm_builtin_copy(args, argc);
    if (VAL_TYPE(cp) == VAL_ARRAY) value_array_sort(cp);
    return cp;
}
static Value vm_builtin_sum(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_int(0);
    double s = 0; int is_float = 0;
    for (int i = 0; i < AS_ARRAY(args[0]).len; i++) {
        Value v = AS_ARRAY(args[0]).items[i];
        if (VAL_TYPE(v) == VAL_INT) s += (double)AS_INT(v);
        else if (VAL_TYPE(v) == VAL_FLOAT) { s += AS_FLOAT(v); is_float = 1; }
    }
    return is_float ? value_float(s) : value_int((long long)s);
}
static Value vm_builtin_unique(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_array_new();
    Value out = value_array_new();
    Value seen = value_set_new();
    for (int i = 0; i < AS_ARRAY(args[0]).len; i++) {
        Value v = AS_ARRAY(args[0]).items[i];
        if (!value_set_has(seen, v)) {
            value_set_add(seen, v);
            value_array_push(out, value_retain(v));
        }
    }
    value_release(seen);
    return out;
}
static Value vm_builtin_enumerate(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_array_new();
    Value out = value_array_new();
    for (int i = 0; i < AS_ARRAY(args[0]).len; i++) {
        Value items[2] = { value_int(i), value_retain(AS_ARRAY(args[0]).items[i]) };
        Value pair = value_tuple_new(items, 2);
        value_release(items[0]); value_release(items[1]);
        value_array_push(out, pair);
    }
    return out;
}
static Value vm_builtin_zip2(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_ARRAY || VAL_TYPE(args[1]) != VAL_ARRAY)
        return value_array_new();
    int n = AS_ARRAY(args[0]).len;
    if (AS_ARRAY(args[1]).len < n) n = AS_ARRAY(args[1]).len;
    Value out = value_array_new();
    for (int i = 0; i < n; i++) {
        Value items[2] = { value_retain(AS_ARRAY(args[0]).items[i]),
                           value_retain(AS_ARRAY(args[1]).items[i]) };
        Value pair = value_tuple_new(items, 2);
        value_release(items[0]); value_release(items[1]);
        value_array_push(out, pair);
    }
    return out;
}
static Value vm_builtin_contains(Value *args, int argc) {
    if (argc < 2) return value_bool(0);
    if (VAL_TYPE(args[0]) == VAL_ARRAY) {
        for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
            if (value_equals(AS_ARRAY(args[0]).items[i], args[1])) return value_bool(1);
        return value_bool(0);
    }
    if (VAL_TYPE(args[0]) == VAL_STRING && VAL_TYPE(args[1]) == VAL_STRING)
        return value_bool(strstr(AS_STR(args[0]), AS_STR(args[1])) != NULL);
    if (VAL_TYPE(args[0]) == VAL_DICT) {
        int idx = value_dict_find_index(args[0], args[1]);
        return value_bool(idx >= 0);
    }
    if (VAL_TYPE(args[0]) == VAL_SET)
        return value_bool(value_set_has(args[0], args[1]));
    return value_bool(0);
}
static Value vm_builtin_has(Value *args, int argc) {
    if (argc < 2) return value_bool(0);
    if (VAL_TYPE(args[0]) == VAL_DICT) {
        int idx = value_dict_find_index(args[0], args[1]);
        return value_bool(idx >= 0);
    }
    return vm_builtin_contains(args, argc);
}
static Value vm_builtin_all_arr(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_bool(1);
    for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
        if (!value_truthy(AS_ARRAY(args[0]).items[i])) return value_bool(0);
    return value_bool(1);
}
static Value vm_builtin_any_arr(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_bool(0);
    for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
        if (value_truthy(AS_ARRAY(args[0]).items[i])) return value_bool(1);
    return value_bool(0);
}

/* --- Dict --- */
static Value vm_builtin_keys(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_DICT) return value_array_new();
    Value out = value_array_new();
    for (int i = 0; i < AS_DICT(args[0]).cap; i++) {
        if (AS_DICT(args[0]).entries[i].key)
            value_array_push(out, value_retain(AS_DICT(args[0]).entries[i].key));
    }
    return out;
}
static Value vm_builtin_values(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_DICT) return value_array_new();
    Value out = value_array_new();
    for (int i = 0; i < AS_DICT(args[0]).cap; i++) {
        if (AS_DICT(args[0]).entries[i].key)
            value_array_push(out, value_retain(AS_DICT(args[0]).entries[i].val));
    }
    return out;
}
static Value vm_builtin_merge(Value *args, int argc) {
    Value out = value_dict_new();
    for (int a = 0; a < argc; a++) {
        if (VAL_TYPE(args[a]) != VAL_DICT) continue;
        for (int i = 0; i < AS_DICT(args[a]).cap; i++) {
            if (AS_DICT(args[a]).entries[i].key)
                value_dict_set(out, AS_DICT(args[a]).entries[i].key,
                               AS_DICT(args[a]).entries[i].val);
        }
    }
    return out;
}

/* --- String --- */
static Value vm_builtin_split(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_array_new();
    const char *s   = AS_STR(args[0]);
    const char *sep = (argc >= 2 && VAL_TYPE(args[1]) == VAL_STRING) ? AS_STR(args[1]) : " ";
    Value out = value_array_new();
    if (sep[0] == '\0') {
        for (size_t i = 0; s[i]; i++) {
            char buf[2] = { s[i], '\0' };
            value_array_push(out, value_string(buf));
        }
        return out;
    }
    size_t seplen = strlen(sep);
    const char *p = s;
    while (1) {
        const char *found = strstr(p, sep);
        if (!found) { value_array_push(out, value_string(p)); break; }
        size_t chunk = (size_t)(found - p);
        char *piece = strndup(p, chunk);
        value_array_push(out, value_string_take(piece));
        p = found + seplen;
    }
    return out;
}
static Value vm_builtin_join(Value *args, int argc) {
    const char *sep = "";
    Value arr_val;
    if (argc >= 2 && VAL_TYPE(args[0]) == VAL_STRING && VAL_TYPE(args[1]) == VAL_ARRAY) {
        sep = AS_STR(args[0]); arr_val = args[1];
    } else if (argc >= 1 && VAL_TYPE(args[0]) == VAL_ARRAY) {
        arr_val = args[0];
        if (argc >= 2 && VAL_TYPE(args[1]) == VAL_STRING) sep = AS_STR(args[1]);
    } else return value_string("");
    int n = AS_ARRAY(arr_val).len;
    if (n == 0) return value_string("");
    size_t total = 0; size_t seplen = strlen(sep);
    char **parts = malloc((size_t)n * sizeof(char *));
    for (int i = 0; i < n; i++) {
        parts[i] = value_to_string(AS_ARRAY(arr_val).items[i]);
        total += strlen(parts[i]) + (i > 0 ? seplen : 0);
    }
    char *buf = malloc(total + 1); buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) strcat(buf, sep);
        strcat(buf, parts[i]);
        free(parts[i]);
    }
    free(parts);
    return value_string_take(buf);
}
static Value vm_builtin_trim(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_string("");
    const char *s = AS_STR(args[0]);
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n'||s[len-1]=='\r')) len--;
    char *buf = strndup(s, len);
    return value_string_take(buf);
}
static Value vm_builtin_upper(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_string("");
    char *s = strdup(AS_STR(args[0]));
    for (int i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
    return value_string_take(s);
}
static Value vm_builtin_lower(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_string("");
    char *s = strdup(AS_STR(args[0]));
    for (int i = 0; s[i]; i++) s[i] = (char)tolower((unsigned char)s[i]);
    return value_string_take(s);
}
static Value vm_builtin_replace(Value *args, int argc) {
    if (argc < 3 || VAL_TYPE(args[0]) != VAL_STRING ||
        VAL_TYPE(args[1]) != VAL_STRING || VAL_TYPE(args[2]) != VAL_STRING)
        return argc >= 1 ? value_retain(args[0]) : value_string("");
    const char *src = AS_STR(args[0]);
    const char *old = AS_STR(args[1]);
    const char *rep = AS_STR(args[2]);
    size_t oldlen = strlen(old); size_t replen = strlen(rep);
    if (oldlen == 0) return value_retain(args[0]);
    size_t count = 0;
    const char *p = src;
    while ((p = strstr(p, old))) { count++; p += oldlen; }
    size_t srclen = strlen(src);
    char *buf = malloc(srclen + count * (replen + oldlen) + 1);
    char *w = buf; p = src;
    while (1) {
        const char *f = strstr(p, old);
        if (!f) { strcpy(w, p); break; }
        size_t n = (size_t)(f - p); memcpy(w, p, n); w += n;
        memcpy(w, rep, replen); w += replen;
        p = f + oldlen;
    }
    return value_string_take(buf);
}
static Value vm_builtin_startswith(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING || VAL_TYPE(args[1]) != VAL_STRING)
        return value_bool(0);
    return value_bool(strncmp(AS_STR(args[0]), AS_STR(args[1]), strlen(AS_STR(args[1]))) == 0);
}
static Value vm_builtin_endswith(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING || VAL_TYPE(args[1]) != VAL_STRING)
        return value_bool(0);
    size_t sl = strlen(AS_STR(args[0])); size_t pl = strlen(AS_STR(args[1]));
    if (pl > sl) return value_bool(0);
    return value_bool(strcmp(AS_STR(args[0]) + sl - pl, AS_STR(args[1])) == 0);
}
static Value vm_builtin_padleft(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING) return argc >= 1 ? value_retain(args[0]) : value_string("");
    const char *s = AS_STR(args[0]);
    int width = (int)AS_INT(args[1]);
    const char *pad = (argc >= 3 && VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : " ";
    int slen = (int)strlen(s); int padlen = (int)strlen(pad);
    if (slen >= width || padlen == 0) return value_retain(args[0]);
    int need = width - slen;
    char *buf = malloc((size_t)(width + 1));
    char *w = buf;
    for (int i = 0; i < need; i++) *w++ = pad[i % padlen];
    memcpy(w, s, (size_t)slen + 1);
    return value_string_take(buf);
}
static Value vm_builtin_padright(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING) return argc >= 1 ? value_retain(args[0]) : value_string("");
    const char *s = AS_STR(args[0]);
    int width = (int)AS_INT(args[1]);
    const char *pad = (argc >= 3 && VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : " ";
    int slen = (int)strlen(s); int padlen = (int)strlen(pad);
    if (slen >= width || padlen == 0) return value_retain(args[0]);
    int need = width - slen;
    char *buf = malloc((size_t)(width + 1));
    memcpy(buf, s, (size_t)slen);
    for (int i = 0; i < need; i++) buf[slen + i] = pad[i % padlen];
    buf[width] = '\0';
    return value_string_take(buf);
}
static Value vm_builtin_repeat_str(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING) return value_string("");
    const char *s = AS_STR(args[0]);
    int n = (VAL_TYPE(args[1]) == VAL_INT) ? (int)AS_INT(args[1]) : 0;
    if (n <= 0) return value_string("");
    size_t slen = strlen(s);
    char *buf = malloc(slen * (size_t)n + 1);
    buf[0] = '\0';
    for (int i = 0; i < n; i++) memcpy(buf + i * slen, s, slen);
    buf[slen * (size_t)n] = '\0';
    return value_string_take(buf);
}
static Value vm_builtin_chr(Value *args, int argc) {
    if (argc < 1) return value_string("");
    int code = (VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : (int)AS_FLOAT(args[0]);
    char buf[4] = {0};
    buf[0] = (char)(code & 0x7f);
    return value_string(buf);
}
static Value vm_builtin_ord(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING || !AS_STR(args[0])[0]) return value_int(0);
    return value_int((long long)(unsigned char)AS_STR(args[0])[0]);
}
static Value vm_builtin_hex(Value *args, int argc) {
    if (argc < 1) return value_string("0x0");
    long long n = (VAL_TYPE(args[0]) == VAL_INT) ? AS_INT(args[0]) : (long long)AS_FLOAT(args[0]);
    char buf[32]; snprintf(buf, sizeof(buf), "0x%llx", n);
    return value_string(buf);
}
static Value vm_builtin_bin(Value *args, int argc) {
    if (argc < 1) return value_string("0b0");
    long long n = (VAL_TYPE(args[0]) == VAL_INT) ? AS_INT(args[0]) : (long long)AS_FLOAT(args[0]);
    if (n == 0) return value_string("0b0");
    char buf[68]; int pos = (int)sizeof(buf) - 1; buf[pos] = '\0';
    unsigned long long u = (unsigned long long)n;
    while (u) { buf[--pos] = '0' + (int)(u & 1); u >>= 1; }
    buf[--pos] = 'b'; buf[--pos] = '0';
    return value_string(buf + pos);
}
static Value vm_builtin_oct(Value *args, int argc) {
    if (argc < 1) return value_string("0o0");
    long long n = (VAL_TYPE(args[0]) == VAL_INT) ? AS_INT(args[0]) : (long long)AS_FLOAT(args[0]);
    char buf[32]; snprintf(buf, sizeof(buf), "0o%llo", n);
    return value_string(buf);
}

/* --- File I/O --- */
static Value vm_builtin_write_file(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING || VAL_TYPE(args[1]) != VAL_STRING)
        return value_bool(0);
    FILE *f = fopen(AS_STR(args[0]), "w");
    if (!f) return value_bool(0);
    fputs(AS_STR(args[1]), f);
    fclose(f);
    return value_bool(1);
}
static Value vm_builtin_read_file(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_null();
    FILE *f = fopen(AS_STR(args[0]), "r");
    if (!f) return value_null();
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    char *buf = malloc((size_t)size + 1);
    size_t nr = fread(buf, 1, (size_t)size, f); fclose(f);
    buf[nr] = '\0';
    return value_string_take(buf);
}
static Value vm_builtin_file_exists(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    FILE *f = fopen(AS_STR(args[0]), "r");
    if (!f) return value_bool(0);
    fclose(f); return value_bool(1);
}

/* --- Index / find --- */
static Value vm_builtin_index_of(Value *args, int argc) {
    if (argc < 2) return value_int(-1);
    if (VAL_TYPE(args[0]) == VAL_ARRAY) {
        for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
            if (value_equals(AS_ARRAY(args[0]).items[i], args[1])) return value_int(i);
        return value_int(-1);
    }
    if (VAL_TYPE(args[0]) == VAL_STRING && VAL_TYPE(args[1]) == VAL_STRING) {
        const char *p = strstr(AS_STR(args[0]), AS_STR(args[1]));
        return p ? value_int(p - AS_STR(args[0])) : value_int(-1);
    }
    return value_int(-1);
}
static Value vm_builtin_count_fn(Value *args, int argc) {
    if (argc < 2) return value_int(0);
    if (VAL_TYPE(args[0]) == VAL_ARRAY) {
        long long c = 0;
        for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
            if (value_equals(AS_ARRAY(args[0]).items[i], args[1])) c++;
        return value_int(c);
    }
    if (VAL_TYPE(args[0]) == VAL_STRING && VAL_TYPE(args[1]) == VAL_STRING) {
        const char *s = AS_STR(args[0]); const char *p = AS_STR(args[1]);
        size_t pl = strlen(p); long long c = 0;
        while ((s = strstr(s, p))) { c++; s += pl; }
        return value_int(c);
    }
    return value_int(0);
}

static bool vm_is_memory_module(Value obj) {
    if (!obj || VAL_TYPE(obj) != VAL_DICT) return false;
    Value key = value_string("__module");
    Value found = value_dict_get(obj, key);
    value_release(key);
    return !IS_NULL(found) && VAL_TYPE(found) == VAL_STRING && strcmp(AS_STR(found), "memory") == 0;
}

static Value vm_memory_method(VM *vm, Value obj, const char *method, Value *args, int argc, int line) {
    (void)obj;
    PrismGC *gc = vm->gc ? vm->gc : gc_global();
    if (strcmp(method, "stats") == 0) return gc_stats_dict(gc);
    if (strcmp(method, "collect") == 0) {
        size_t freed = gc_collect_major(gc, NULL, vm, NULL);
        return value_int((long long)freed);
    }
    if (strcmp(method, "limit") == 0) {
        if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) {
            vm_error(vm, "memory.limit() requires a string like \"512mb\"", line);
            return value_null();
        }
        return gc_set_soft_limit(gc, AS_STR(args[0]));
    }
    if (strcmp(method, "profile") == 0) return gc_stats_dict(gc);
    char msg[128];
    snprintf(msg, sizeof(msg), "memory has no method '%s'", method);
    vm_error(vm, msg, line);
    return value_null();
}

/* ---- GUI built-ins (same as in interpreter.c) ---- */

typedef struct {
    char  title[256];
    int   width, height;
    char *body;
    int   body_len, body_cap;
    int   active;
} VmGuiState;

static VmGuiState g_vmgui = {"Prism Window", 800, 600, NULL, 0, 0, 0};

static void vmgui_append(const char *html) {
    int add = (int)strlen(html);
    if (!g_vmgui.body) {
        g_vmgui.body_cap = 4096;
        g_vmgui.body = malloc(g_vmgui.body_cap);
        g_vmgui.body[0] = '\0';
        g_vmgui.body_len = 0;
    }
    while (g_vmgui.body_len + add + 1 >= g_vmgui.body_cap) {
        g_vmgui.body_cap *= 2;
        g_vmgui.body = realloc(g_vmgui.body, g_vmgui.body_cap);
    }
    memcpy(g_vmgui.body + g_vmgui.body_len, html, add + 1);
    g_vmgui.body_len += add;
}

static Value vmbi_gui_window(Value *args, int argc) {
    if (argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING)
        snprintf(g_vmgui.title, sizeof(g_vmgui.title), "%s", AS_STR(args[0]));
    if (argc >= 2 && VAL_TYPE(args[1]) == VAL_INT) g_vmgui.width  = (int)AS_INT(args[1]);
    if (argc >= 3 && VAL_TYPE(args[2]) == VAL_INT) g_vmgui.height = (int)AS_INT(args[2]);
    g_vmgui.active = 1;
    return value_null();
}

static Value vmbi_gui_label(Value *args, int argc) {
    char buf[2048];
    const char *t = (argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    snprintf(buf, sizeof(buf), "  <p class=\"gui-label\">%s</p>\n", t);
    vmgui_append(buf); return value_null();
}

static Value vmbi_gui_button(Value *args, int argc) {
    char buf[2048];
    const char *t = (argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "Button";
    snprintf(buf, sizeof(buf), "  <button class=\"gui-btn\">%s</button>\n", t);
    vmgui_append(buf); return value_null();
}

static Value vmbi_gui_input(Value *args, int argc) {
    char buf[2048];
    const char *ph = (argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    snprintf(buf, sizeof(buf), "  <input class=\"gui-input\" type=\"text\" placeholder=\"%s\" />\n", ph);
    vmgui_append(buf); return value_null();
}

static Value vmbi_gui_run(Value *args, int argc) {
    (void)args; (void)argc;
    if (!g_vmgui.active) {
        fprintf(stderr, "[prism] gui_run() called without gui_window()\n");
        return value_null();
    }
    FILE *fp = fopen("prism_gui.html", "w");
    if (!fp) { fprintf(stderr, "[prism] could not write prism_gui.html\n"); return value_null(); }
    fprintf(fp,
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        "  <meta charset=\"UTF-8\">\n  <title>%s</title>\n"
        "  <style>\n"
        "    body{font-family:sans-serif;margin:0;padding:20px;width:%dpx;min-height:%dpx;box-sizing:border-box}\n"
        "    .gui-label{font-size:16px;margin:8px 0}\n"
        "    .gui-btn{padding:8px 18px;font-size:15px;cursor:pointer;background:#4f8ef7;color:#fff;"
        "             border:none;border-radius:4px;margin:6px 4px}\n"
        "    .gui-btn:hover{background:#2563c7}\n"
        "    .gui-input{padding:7px 12px;font-size:15px;border:1px solid #ccc;"
        "               border-radius:4px;margin:6px 0;width:100%%;box-sizing:border-box}\n"
        "  </style>\n</head>\n<body>\n"
        "  <h2>%s</h2>\n%s</body>\n</html>\n",
        g_vmgui.title, g_vmgui.width, g_vmgui.height,
        g_vmgui.title, g_vmgui.body ? g_vmgui.body : "");
    fclose(fp);
    printf("[prism] GUI written to prism_gui.html\n");
    return value_null();
}

/* ================================================================== XGUI builtins (VM) */

#ifdef HAVE_X11

static XGui *g_vm_xgui = NULL;

static Value vm_bi_xgui_init(Value *args, int argc) {
    int w = (argc > 0 && VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : 800;
    int h = (argc > 1 && VAL_TYPE(args[1]) == VAL_INT) ? (int)AS_INT(args[1]) : 600;
    const char *title = (argc > 2 && VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : "Prism";
    if (g_vm_xgui) xgui_destroy(g_vm_xgui);
    g_vm_xgui = xgui_init(w, h, title);
    return value_null();
}

static Value vm_bi_xgui_style(Value *args, int argc) {
    if (g_vm_xgui && argc > 0 && VAL_TYPE(args[0]) == VAL_STRING)
        xgui_load_style(g_vm_xgui, AS_STR(args[0]));
    return value_null();
}

static Value vm_bi_xgui_running(Value *args, int argc) {
    (void)args; (void)argc;
    return value_bool(xgui_running(g_vm_xgui) ? 1 : 0);
}

static Value vm_bi_xgui_begin(Value *args, int argc) {
    (void)args; (void)argc;
    xgui_begin(g_vm_xgui); return value_null();
}

static Value vm_bi_xgui_end(Value *args, int argc) {
    (void)args; (void)argc;
    xgui_end(g_vm_xgui); return value_null();
}

static Value vm_bi_xgui_label(Value *args, int argc) {
    if (g_vm_xgui && argc > 0 && VAL_TYPE(args[0]) == VAL_STRING)
        xgui_label(g_vm_xgui, AS_STR(args[0]));
    return value_null();
}

static Value vm_bi_xgui_button(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    return value_bool(xgui_button(g_vm_xgui, AS_STR(args[0])) ? 1 : 0);
}

static Value vm_bi_xgui_input(Value *args, int argc) {
    if (!g_vm_xgui) return value_string("");
    const char *id  = (argc > 0 && VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "input";
    const char *ph  = (argc > 1 && VAL_TYPE(args[1]) == VAL_STRING) ? AS_STR(args[1]) : "";
    const char *val = xgui_input(g_vm_xgui, id, ph);
    return value_string(val ? val : "");
}

static Value vm_bi_xgui_spacer(Value *args, int argc) {
    int h = (argc > 0 && VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : 16;
    xgui_spacer(g_vm_xgui, h); return value_null();
}

static Value vm_bi_xgui_row_begin(Value *args, int argc) {
    (void)args; (void)argc; xgui_row_begin(g_vm_xgui); return value_null();
}

static Value vm_bi_xgui_row_end(Value *args, int argc) {
    (void)args; (void)argc; xgui_row_end(g_vm_xgui); return value_null();
}

static Value vm_bi_xgui_close(Value *args, int argc) {
    (void)args; (void)argc;
    if (g_vm_xgui) { xgui_destroy(g_vm_xgui); g_vm_xgui = NULL; }
    return value_null();
}

static Value vm_bi_xgui_title(Value *args, int argc) {
    if (g_vm_xgui && argc > 0 && VAL_TYPE(args[0]) == VAL_STRING)
        xgui_title(g_vm_xgui, AS_STR(args[0]));
    return value_null();
}
static Value vm_bi_xgui_subtitle(Value *args, int argc) {
    if (g_vm_xgui && argc > 0 && VAL_TYPE(args[0]) == VAL_STRING)
        xgui_subtitle(g_vm_xgui, AS_STR(args[0]));
    return value_null();
}
static Value vm_bi_xgui_separator(Value *args, int argc) {
    (void)args; (void)argc;
    if (g_vm_xgui) xgui_separator(g_vm_xgui);
    return value_null();
}
static Value vm_bi_xgui_checkbox(Value *args, int argc) {
    if (!g_vm_xgui || argc < 2) return value_bool(0);
    const char *id    = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "cb";
    const char *label = (argc > 1 && VAL_TYPE(args[1]) == VAL_STRING) ? AS_STR(args[1]) : "";
    return value_bool(xgui_checkbox(g_vm_xgui, id, label) ? 1 : 0);
}
static Value vm_bi_xgui_progress(Value *args, int argc) {
    if (!g_vm_xgui || argc < 2) return value_null();
    int val = (VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : (int)AS_FLOAT(args[0]);
    int mx  = (VAL_TYPE(args[1]) == VAL_INT) ? (int)AS_INT(args[1]) : (int)AS_FLOAT(args[1]);
    xgui_progress(g_vm_xgui, val, mx);
    return value_null();
}
static Value vm_bi_xgui_slider(Value *args, int argc) {
    if (!g_vm_xgui || argc < 4) return value_float(0.0);
    const char *id = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "sl";
    double mn  = (VAL_TYPE(args[1]) == VAL_INT) ? (double)AS_INT(args[1]) : AS_FLOAT(args[1]);
    double mx  = (VAL_TYPE(args[2]) == VAL_INT) ? (double)AS_INT(args[2]) : AS_FLOAT(args[2]);
    double cur = (VAL_TYPE(args[3]) == VAL_INT) ? (double)AS_INT(args[3]) : AS_FLOAT(args[3]);
    return value_float((double)xgui_slider(g_vm_xgui, id, (float)mn, (float)mx, (float)cur));
}
static Value vm_bi_xgui_textarea(Value *args, int argc) {
    if (!g_vm_xgui) return value_string("");
    const char *id = (argc > 0 && VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "ta";
    const char *ph = (argc > 1 && VAL_TYPE(args[1]) == VAL_STRING) ? AS_STR(args[1]) : "";
    const char *val = xgui_textarea(g_vm_xgui, id, ph);
    return value_string(val ? val : "");
}
static Value vm_bi_xgui_badge(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_null();
    const char *text = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    uint32_t color = 0x4f8ef7;
    if (argc > 1 && VAL_TYPE(args[1]) == VAL_INT) color = (uint32_t)AS_INT(args[1]);
    xgui_badge(g_vm_xgui, text, color);
    return value_null();
}
static Value vm_bi_xgui_set_dark(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_null();
    bool dark = false;
    if (VAL_TYPE(args[0]) == VAL_BOOL)     dark = (AS_BOOL(args[0]) != 0);
    else if (VAL_TYPE(args[0]) == VAL_INT) dark = (AS_INT(args[0])  != 0);
    xgui_set_dark(g_vm_xgui, dark);
    return value_null();
}
static Value vm_bi_xgui_card_begin(Value *args, int argc) {
    (void)args; (void)argc;
    if (g_vm_xgui) xgui_card_begin(g_vm_xgui);
    return value_null();
}
static Value vm_bi_xgui_card_end(Value *args, int argc) {
    (void)args; (void)argc;
    if (g_vm_xgui) xgui_card_end(g_vm_xgui);
    return value_null();
}
static Value vm_bi_xgui_tooltip(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_null();
    const char *text = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    xgui_tooltip(g_vm_xgui, text);
    return value_null();
}

/* ── new v4 widgets ─────────────────────────────────────────── */
static Value vm_bi_xgui_toggle(Value *args, int argc) {
    if (!g_vm_xgui || argc < 3) return value_bool(0);
    const char *id  = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "toggle";
    bool        v   = (VAL_TYPE(args[1]) == VAL_BOOL)   ? (AS_BOOL(args[1]) != 0)
                    : (VAL_TYPE(args[1]) == VAL_INT)    ? (AS_INT(args[1])  != 0) : false;
    const char *lbl = (VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : "";
    return value_bool(xgui_toggle(g_vm_xgui, id, v, lbl) ? 1 : 0);
}
static Value vm_bi_xgui_chip(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_bool(0);
    const char *text = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    bool removable   = (argc >= 2 && VAL_TYPE(args[1]) == VAL_BOOL) ? (AS_BOOL(args[1]) != 0) : false;
    return value_bool(xgui_chip(g_vm_xgui, text, removable) ? 1 : 0);
}
static Value vm_bi_xgui_tabs(Value *args, int argc) {
    if (!g_vm_xgui || argc < 2) return value_int(0);
    const char *id = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "tabs";
    const char *labels_arr[16]; int n = 0;
    if (VAL_TYPE(args[1]) == VAL_ARRAY) {
        for (int i = 0; i < AS_ARRAY(args[1]).len && i < 16; i++) {
            Value lv = AS_ARRAY(args[1]).items[i];
            labels_arr[n++] = (VAL_TYPE(lv) == VAL_STRING) ? AS_STR(lv) : "";
        }
    }
    return value_int(xgui_tabs(g_vm_xgui, id, labels_arr, n));
}
static Value vm_bi_xgui_select(Value *args, int argc) {
    if (!g_vm_xgui || argc < 3) return value_int(0);
    const char *id  = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "sel";
    int         cur = (VAL_TYPE(args[2]) == VAL_INT)    ? (int)AS_INT(args[2]) : 0;
    const char *opts[32]; int n = 0;
    if (VAL_TYPE(args[1]) == VAL_ARRAY) {
        for (int i = 0; i < AS_ARRAY(args[1]).len && i < 32; i++) {
            Value ov = AS_ARRAY(args[1]).items[i];
            opts[n++] = (VAL_TYPE(ov) == VAL_STRING) ? AS_STR(ov) : "";
        }
    }
    return value_int(xgui_select(g_vm_xgui, id, opts, n, cur));
}
static Value vm_bi_xgui_spinner(Value *args, int argc) {
    int sz = (argc >= 1 && VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : 32;
    if (g_vm_xgui) xgui_spinner(g_vm_xgui, sz);
    return value_null();
}
static Value vm_bi_xgui_list_item(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_bool(0);
    const char *title    = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    const char *subtitle = (argc >= 2 && VAL_TYPE(args[1]) == VAL_STRING) ? AS_STR(args[1]) : NULL;
    const char *trailing = (argc >= 3 && VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : NULL;
    return value_bool(xgui_list_item(g_vm_xgui, title, subtitle, trailing) ? 1 : 0);
}
static Value vm_bi_xgui_show_toast(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_null();
    const char *text = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    int dur = (argc >= 2 && VAL_TYPE(args[1]) == VAL_INT) ? (int)AS_INT(args[1]) : 90;
    xgui_show_toast(g_vm_xgui, text, dur);
    return value_null();
}
static Value vm_bi_xgui_section(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_null();
    xgui_section(g_vm_xgui, (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "");
    return value_null();
}
static Value vm_bi_xgui_icon_button(Value *args, int argc) {
    if (!g_vm_xgui || argc < 2) return value_bool(0);
    const char *icon  = (VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    const char *label = (VAL_TYPE(args[1]) == VAL_STRING) ? AS_STR(args[1]) : "";
    return value_bool(xgui_icon_button(g_vm_xgui, icon, label) ? 1 : 0);
}
static Value vm_bi_xgui_group_begin(Value *args, int argc) {
    if (!g_vm_xgui) return value_null();
    const char *t = (argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING) ? AS_STR(args[0]) : "";
    xgui_group_begin(g_vm_xgui, t);
    return value_null();
}
static Value vm_bi_xgui_group_end(Value *args, int argc) {
    (void)args; (void)argc;
    if (g_vm_xgui) xgui_group_end(g_vm_xgui);
    return value_null();
}
static Value vm_bi_xgui_grid_begin(Value *args, int argc) {
    int cols = (argc >= 1 && VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : 2;
    if (g_vm_xgui) xgui_grid_begin(g_vm_xgui, cols);
    return value_null();
}
static Value vm_bi_xgui_grid_end(Value *args, int argc) {
    (void)args; (void)argc;
    if (g_vm_xgui) xgui_grid_end(g_vm_xgui);
    return value_null();
}
static Value vm_bi_xgui_clear_bg(Value *args, int argc) {
    if (!g_vm_xgui || argc < 1) return value_null();
    xgui_clear_bg(g_vm_xgui, (uint32_t)AS_INT(args[0]));
    return value_null();
}
static Value vm_bi_xgui_fill_rect_at(Value *args, int argc) {
    if (!g_vm_xgui || argc < 5) return value_null();
    int r = (argc >= 6 && VAL_TYPE(args[4]) == VAL_INT) ? (int)AS_INT(args[4]) : 0;
    uint32_t c = (argc >= 6) ? (uint32_t)AS_INT(args[5]) : (uint32_t)AS_INT(args[4]);
    xgui_fill_rect_at(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                      (int)AS_INT(args[2]), (int)AS_INT(args[3]), r, c);
    return value_null();
}
static Value vm_bi_xgui_fill_circle_at(Value *args, int argc) {
    if (!g_vm_xgui || argc < 4) return value_null();
    xgui_fill_circle_at(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                        (int)AS_INT(args[2]), (uint32_t)AS_INT(args[3]));
    return value_null();
}
static Value vm_bi_xgui_draw_line_at(Value *args, int argc) {
    if (!g_vm_xgui || argc < 5) return value_null();
    int thick = (argc >= 6) ? (int)AS_INT(args[4]) : 1;
    uint32_t c = (argc >= 6) ? (uint32_t)AS_INT(args[5]) : (uint32_t)AS_INT(args[4]);
    xgui_draw_line_at(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                      (int)AS_INT(args[2]), (int)AS_INT(args[3]), thick, c);
    return value_null();
}
static Value vm_bi_xgui_draw_text_at(Value *args, int argc) {
    if (!g_vm_xgui || argc < 5) return value_null();
    const char *t = (VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : "";
    xgui_draw_text_at(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                      t, (int)AS_INT(args[3]), (uint32_t)AS_INT(args[4]));
    return value_null();
}
static Value vm_bi_xgui_draw_text_centered(Value *args, int argc) {
    if (!g_vm_xgui || argc < 5) return value_null();
    const char *t = (VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : "";
    xgui_draw_text_centered(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                            t, (int)AS_INT(args[3]), (uint32_t)AS_INT(args[4]));
    return value_null();
}
static Value vm_bi_xgui_draw_text_bold_at(Value *args, int argc) {
    if (!g_vm_xgui || argc < 5) return value_null();
    const char *t = (VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : "";
    xgui_draw_text_bold_at(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                           t, (int)AS_INT(args[3]), (uint32_t)AS_INT(args[4]));
    return value_null();
}
static Value vm_bi_xgui_draw_text_bold_centered(Value *args, int argc) {
    if (!g_vm_xgui || argc < 5) return value_null();
    const char *t = (VAL_TYPE(args[2]) == VAL_STRING) ? AS_STR(args[2]) : "";
    xgui_draw_text_bold_centered(g_vm_xgui, (int)AS_INT(args[0]), (int)AS_INT(args[1]),
                                  t, (int)AS_INT(args[3]), (uint32_t)AS_INT(args[4]));
    return value_null();
}
static Value vm_bi_xgui_key_w(Value *args, int argc)      { (void)args;(void)argc; return value_bool(xgui_key_w(g_vm_xgui)      ? 1:0); }
static Value vm_bi_xgui_key_s(Value *args, int argc)      { (void)args;(void)argc; return value_bool(xgui_key_s(g_vm_xgui)      ? 1:0); }
static Value vm_bi_xgui_key_a(Value *args, int argc)      { (void)args;(void)argc; return value_bool(xgui_key_a(g_vm_xgui)      ? 1:0); }
static Value vm_bi_xgui_key_d(Value *args, int argc)      { (void)args;(void)argc; return value_bool(xgui_key_d(g_vm_xgui)      ? 1:0); }
static Value vm_bi_xgui_key_up(Value *args, int argc)     { (void)args;(void)argc; return value_bool(xgui_key_up(g_vm_xgui)     ? 1:0); }
static Value vm_bi_xgui_key_down(Value *args, int argc)   { (void)args;(void)argc; return value_bool(xgui_key_down(g_vm_xgui)   ? 1:0); }
static Value vm_bi_xgui_key_left(Value *args, int argc)   { (void)args;(void)argc; return value_bool(xgui_key_left(g_vm_xgui)   ? 1:0); }
static Value vm_bi_xgui_key_right(Value *args, int argc)  { (void)args;(void)argc; return value_bool(xgui_key_right(g_vm_xgui)  ? 1:0); }
static Value vm_bi_xgui_key_space(Value *args, int argc)  { (void)args;(void)argc; return value_bool(xgui_key_space(g_vm_xgui)  ? 1:0); }
static Value vm_bi_xgui_key_escape(Value *args, int argc) { (void)args;(void)argc; return value_bool(xgui_key_escape(g_vm_xgui) ? 1:0); }
static Value vm_bi_xgui_mouse_down(Value *args, int argc) { (void)args;(void)argc; return value_bool(xgui_mouse_down(g_vm_xgui) ? 1:0); }
static Value vm_bi_xgui_mouse_x(Value *args, int argc)    { (void)args;(void)argc; return value_int(xgui_mouse_x(g_vm_xgui));  }
static Value vm_bi_xgui_mouse_y(Value *args, int argc)    { (void)args;(void)argc; return value_int(xgui_mouse_y(g_vm_xgui));  }
static Value vm_bi_xgui_win_w(Value *args, int argc)      { (void)args;(void)argc; return value_int(xgui_win_w(g_vm_xgui));    }
static Value vm_bi_xgui_win_h(Value *args, int argc)      { (void)args;(void)argc; return value_int(xgui_win_h(g_vm_xgui));    }
static Value vm_bi_xgui_delta_ms(Value *args, int argc)   { (void)args;(void)argc; return value_float(xgui_delta_ms(g_vm_xgui)); }
static Value vm_bi_xgui_clock_ms(Value *args, int argc)   { (void)args;(void)argc; return value_int((int64_t)xgui_clock_ms(g_vm_xgui)); }
static Value vm_bi_xgui_sleep_ms(Value *args, int argc) {
    int ms = (argc >= 1 && VAL_TYPE(args[0]) == VAL_INT) ? (int)AS_INT(args[0]) : 0;
    xgui_sleep_ms(g_vm_xgui, ms);
    return value_null();
}

#else /* !HAVE_X11 — graceful stubs */

static Value vm_bi_xgui_no_x11(Value *args, int argc) {
    (void)args; (void)argc;
    fprintf(stderr, "xgui: X11 support was not compiled in. "
                    "Install libX11-dev / xorg-dev and recompile.\n");
    return value_null();
}

#endif /* HAVE_X11 */

/* ---- Additional builtins missing from the VM ---- */

/* indexOf: camelCase alias for index_of */
static Value vm_builtin_indexOf(Value *args, int argc) { return vm_builtin_index_of(args, argc); }

/* sort: global function that sorts array in-place */
static Value vm_builtin_sort(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_null();
    value_array_sort(args[0]); return value_null();
}

/* first / last / compact */
static Value vm_builtin_first(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY || AS_ARRAY(args[0]).len == 0) return value_null();
    return value_retain(AS_ARRAY(args[0]).items[0]);
}
static Value vm_builtin_last(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY || AS_ARRAY(args[0]).len == 0) return value_null();
    return value_retain(AS_ARRAY(args[0]).items[AS_ARRAY(args[0]).len - 1]);
}
static Value vm_builtin_compact(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_ARRAY) return value_array_new();
    Value r = value_array_new();
    for (int i = 0; i < AS_ARRAY(args[0]).len; i++)
        if (VAL_TYPE(AS_ARRAY(args[0]).items[i]) != VAL_NULL)
            value_array_push(r, AS_ARRAY(args[0]).items[i]);
    return r;
}

/* ltrim / rtrim */
static Value vm_builtin_ltrim(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_string("");
    const char *s = AS_STR(args[0]);
    while (isspace((unsigned char)*s)) s++;
    return value_string(s);
}
static Value vm_builtin_rtrim(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_string("");
    char *s = strdup(AS_STR(args[0]));
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) *--e = '\0';
    return value_string_take(s);
}

/* starts / ends (lowercase aliases) */
static Value vm_builtin_starts(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING || VAL_TYPE(args[1]) != VAL_STRING) return value_bool(0);
    return value_bool(strncmp(AS_STR(args[0]), AS_STR(args[1]), strlen(AS_STR(args[1]))) == 0 ? 1 : 0);
}
static Value vm_builtin_ends(Value *args, int argc) {
    if (argc < 2 || VAL_TYPE(args[0]) != VAL_STRING || VAL_TYPE(args[1]) != VAL_STRING) return value_bool(0);
    size_t sl = strlen(AS_STR(args[0])), ml = strlen(AS_STR(args[1]));
    return value_bool((sl >= ml && strcmp(AS_STR(args[0]) + sl - ml, AS_STR(args[1])) == 0) ? 1 : 0);
}

/* chars: return array of chars */
static Value vm_builtin_chars(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_array_new();
    const char *s = AS_STR(args[0]);
    Value arr = value_array_new();
    char buf[2] = {0, 0};
    for (int i = 0; s[i]; i++) {
        buf[0] = s[i];
        Value cv = value_string(buf);
        value_array_push(arr, cv); value_release(cv);
    }
    return arr;
}

/* parseInt / parseFloat */
static Value vm_builtin_parseInt(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_int(0);
    return value_int((long long)strtoll(AS_STR(args[0]), NULL, 10));
}
static Value vm_builtin_parseFloat(Value *args, int argc) {
    if (argc < 1 || VAL_TYPE(args[0]) != VAL_STRING) return value_float(0.0);
    return value_float(strtod(AS_STR(args[0]), NULL));
}

/* Math functions */
static Value vm_bi_sin(Value *a, int n)  { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(sin(v)); }
static Value vm_bi_cos(Value *a, int n)  { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(cos(v)); }
static Value vm_bi_tan(Value *a, int n)  { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(tan(v)); }
static Value vm_bi_asin(Value *a, int n) { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(asin(v)); }
static Value vm_bi_acos(Value *a, int n) { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(acos(v)); }
static Value vm_bi_atan(Value *a, int n) { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(atan(v)); }
static Value vm_bi_atan2(Value *a, int n){ if(n<2) return value_float(0.0); double y=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); double x=(VAL_TYPE(a[1])==VAL_INT)?AS_INT(a[1]):AS_FLOAT(a[1]); return value_float(atan2(y,x)); }
static Value vm_bi_log(Value *a, int n)  { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(log(v)); }
static Value vm_bi_log2(Value *a, int n) { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(log2(v)); }
static Value vm_bi_log10(Value *a, int n){ if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(log10(v)); }
static Value vm_bi_exp(Value *a, int n)  { if(n<1) return value_float(0.0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_float(exp(v)); }
static Value vm_bi_hypot(Value *a, int n){ if(n<2) return value_float(0.0); double x=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); double y=(VAL_TYPE(a[1])==VAL_INT)?AS_INT(a[1]):AS_FLOAT(a[1]); return value_float(hypot(x,y)); }
static Value vm_bi_isnan(Value *a, int n){ if(n<1) return value_bool(0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_bool(isnan(v)?1:0); }
static Value vm_bi_isinf(Value *a, int n){ if(n<1) return value_bool(0); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); return value_bool(isinf(v)?1:0); }
static Value vm_bi_clamp(Value *a, int n){ if(n<3) return n>=1?value_retain(a[0]):value_null(); double v=(VAL_TYPE(a[0])==VAL_INT)?AS_INT(a[0]):AS_FLOAT(a[0]); double lo=(VAL_TYPE(a[1])==VAL_INT)?AS_INT(a[1]):AS_FLOAT(a[1]); double hi=(VAL_TYPE(a[2])==VAL_INT)?AS_INT(a[2]):AS_FLOAT(a[2]); if(v<lo)v=lo; if(v>hi)v=hi; return (VAL_TYPE(a[0])==VAL_INT&&VAL_TYPE(a[1])==VAL_INT&&VAL_TYPE(a[2])==VAL_INT)?value_int((long long)v):value_float(v); }

/* Math constants */
static Value vm_bi_pi(Value *a, int n)  { (void)a;(void)n; return value_float(3.14159265358979323846); }
static Value vm_bi_e(Value *a, int n)   { (void)a;(void)n; return value_float(2.71828182845904523536); }
static Value vm_bi_tau(Value *a, int n) { (void)a;(void)n; return value_float(6.28318530717958647692); }
static Value vm_bi_inf(Value *a, int n) { (void)a;(void)n; return value_float(1.0/0.0); }
static Value vm_bi_nan(Value *a, int n) { (void)a;(void)n; return value_float(0.0/0.0); }

/* repr / id / error / exit / clock / time */
static Value vm_bi_repr(Value *a, int n)  { if(n<1) return value_string("null"); char *s=value_to_string(a[0]); return value_string_take(s); }
static Value vm_bi_id(Value *a, int n)    { if(n<1||!IS_PTR(a[0])) return value_int(0); return value_int((long long)(uintptr_t)AS_PTR(a[0])); }
static Value vm_bi_error(Value *a, int n) { if(n<1) { fprintf(stderr,"error\n"); } else { char *s=value_to_string(a[0]); fprintf(stderr,"%s\n",s); free(s); } return value_null(); }
static Value vm_bi_exit_fn(Value *a, int n){ int code=(n>=1&&VAL_TYPE(a[0])==VAL_INT)?(int)AS_INT(a[0]):0; exit(code); return value_null(); }
static Value vm_bi_clock_fn(Value *a, int n) { (void)a;(void)n; return value_float((double)clock()/CLOCKS_PER_SEC); }
static Value vm_bi_time_now(Value *a, int n){ (void)a;(void)n; return value_float((double)time(NULL)); }

/* slice: slice(arr, start, end) */
static Value vm_bi_slice(Value *a, int n) {
    if (n < 1) return value_array_new();
    if (VAL_TYPE(a[0]) == VAL_STRING) {
        const char *s = AS_STR(a[0]);
        int slen = (int)strlen(s);
        int start = (n >= 2 && VAL_TYPE(a[1]) == VAL_INT) ? (int)AS_INT(a[1]) : 0;
        int end   = (n >= 3 && VAL_TYPE(a[2]) == VAL_INT) ? (int)AS_INT(a[2]) : slen;
        if (start < 0) start += slen; if (start < 0) start = 0;
        if (end   < 0) end   += slen; if (end   < 0) end   = 0;
        if (start > slen) start = slen; if (end > slen) end = slen;
        if (end < start) end = start;
        char *buf = malloc(end - start + 1);
        memcpy(buf, s + start, end - start);
        buf[end - start] = '\0';
        return value_string_take(buf);
    }
    if (VAL_TYPE(a[0]) != VAL_ARRAY) return value_array_new();
    int len = AS_ARRAY(a[0]).len;
    int start = (n >= 2 && VAL_TYPE(a[1]) == VAL_INT) ? (int)AS_INT(a[1]) : 0;
    int end   = (n >= 3 && VAL_TYPE(a[2]) == VAL_INT) ? (int)AS_INT(a[2]) : len;
    if (start < 0) start += len; if (start < 0) start = 0;
    if (end   < 0) end   += len; if (end   < 0) end   = 0;
    if (start > len) start = len; if (end > len) end = len;
    Value r = value_array_new();
    for (int i = start; i < end; i++) value_array_push(r, AS_ARRAY(a[0]).items[i]);
    return r;
}

/* flatten: flatten(arr) */
static Value vm_bi_flatten(Value *a, int n) {
    if (n < 1 || VAL_TYPE(a[0]) != VAL_ARRAY) return value_array_new();
    Value r = value_array_new();
    for (int i = 0; i < AS_ARRAY(a[0]).len; i++) {
        Value item = AS_ARRAY(a[0]).items[i];
        if (VAL_TYPE(item) == VAL_ARRAY)
            for (int j = 0; j < AS_ARRAY(item).len; j++) value_array_push(r, AS_ARRAY(item).items[j]);
        else value_array_push(r, item);
    }
    return r;
}

/* File system */
static Value vm_bi_is_file(Value *a, int n) {
    if (n < 1 || VAL_TYPE(a[0]) != VAL_STRING) return value_bool(0);
    struct stat st; if (stat(AS_STR(a[0]), &st) != 0) return value_bool(0);
    return value_bool(S_ISREG(st.st_mode) ? 1 : 0);
}
static Value vm_bi_is_dir(Value *a, int n) {
    if (n < 1 || VAL_TYPE(a[0]) != VAL_STRING) return value_bool(0);
    struct stat st; if (stat(AS_STR(a[0]), &st) != 0) return value_bool(0);
    return value_bool(S_ISDIR(st.st_mode) ? 1 : 0);
}
static Value vm_bi_file_size(Value *a, int n) {
    if (n < 1 || VAL_TYPE(a[0]) != VAL_STRING) return value_int(-1);
    struct stat st; if (stat(AS_STR(a[0]), &st) != 0) return value_int(-1);
    return value_int((long long)st.st_size);
}
static Value vm_bi_append_file(Value *a, int n) {
    if (n < 2 || VAL_TYPE(a[0]) != VAL_STRING || VAL_TYPE(a[1]) != VAL_STRING) return value_bool(0);
    FILE *f = fopen(AS_STR(a[0]), "a"); if (!f) return value_bool(0);
    fputs(AS_STR(a[1]), f); fclose(f); return value_bool(1);
}
static Value vm_bi_delete_file(Value *a, int n) {
    if (n < 1 || VAL_TYPE(a[0]) != VAL_STRING) return value_bool(0);
    return value_bool(remove(AS_STR(a[0])) == 0 ? 1 : 0);
}
static Value vm_bi_listdir(Value *a, int n) {
    const char *path = (n >= 1 && VAL_TYPE(a[0]) == VAL_STRING) ? AS_STR(a[0]) : ".";
    Value arr = value_array_new();
    DIR *d = opendir(path); if (!d) return arr;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        Value sv = value_string(ent->d_name);
        value_array_push(arr, sv); value_release(sv);
    }
    closedir(d); return arr;
}
static Value vm_bi_getcwd(Value *a, int n) {
    (void)a;(void)n; char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return value_string("");
    return value_string(buf);
}
static Value vm_bi_getenv(Value *a, int n) {
    if (n < 1 || VAL_TYPE(a[0]) != VAL_STRING) return value_null();
    const char *val = getenv(AS_STR(a[0]));
    if (!val) return (n >= 2) ? value_retain(a[1]) : value_null();
    return value_string(val);
}

/* ================================================================== register */

void vm_register_builtins(VM *vm) {
    /* Register complete shared stdlib first (math, string, array, dict, etc.) */
    prism_register_stdlib(vm->globals);

    /* VM-specific overrides and additions */
    struct { const char *name; BuiltinFn fn; } bi[] = {
        {"output",     vm_builtin_output},
        {"input",      vm_builtin_input},
        {"len",        vm_builtin_len},
        {"bool",       vm_builtin_bool_fn},
        {"int",        vm_builtin_int_fn},
        {"float",      vm_builtin_float_fn},
        {"str",        vm_builtin_str_fn},
        {"set",        vm_builtin_set_fn},
        {"array",      vm_builtin_array_fn},
        {"tuple",      vm_builtin_tuple_fn},
        {"complex",    vm_builtin_complex_fn},
        {"type",       vm_builtin_type_fn},
        {"assert",     vm_builtin_assert},
        {"assert_eq",  vm_builtin_assert_eq},
        {"round",      vm_builtin_round},
        {"abs",        vm_builtin_abs},
        {"floor",      vm_builtin_floor},
        {"ceil",       vm_builtin_ceil},
        {"min",        vm_builtin_min},
        {"max",        vm_builtin_max},
        {"pow",        vm_builtin_pow},
        {"sqrt",       vm_builtin_sqrt},
        {"print",      vm_builtin_print},
        /* stdlib */
        {"push",       vm_builtin_push},
        {"append",     vm_builtin_append},
        {"pop",        vm_builtin_pop},
        {"reverse",    vm_builtin_reverse},
        {"delete",     vm_bi_delete},
        {"copy",       vm_builtin_copy},
        {"sorted",     vm_builtin_sorted},
        {"sum",        vm_builtin_sum},
        {"unique",     vm_builtin_unique},
        {"enumerate",  vm_builtin_enumerate},
        {"zip",        vm_builtin_zip2},
        {"contains",   vm_builtin_contains},
        {"has",        vm_builtin_has},
        {"all",        vm_builtin_all_arr},
        {"any",        vm_builtin_any_arr},
        {"keys",       vm_builtin_keys},
        {"values",     vm_builtin_values},
        {"merge",      vm_builtin_merge},
        {"split",      vm_builtin_split},
        {"join",       vm_builtin_join},
        {"trim",       vm_builtin_trim},
        {"upper",      vm_builtin_upper},
        {"lower",      vm_builtin_lower},
        {"replace",    vm_builtin_replace},
        {"startsWith", vm_builtin_startswith},
        {"endsWith",   vm_builtin_endswith},
        {"padLeft",    vm_builtin_padleft},
        {"padRight",   vm_builtin_padright},
        {"repeat",     vm_builtin_repeat_str},
        {"chr",        vm_builtin_chr},
        {"ord",        vm_builtin_ord},
        {"hex",        vm_builtin_hex},
        {"bin",        vm_builtin_bin},
        {"oct",        vm_builtin_oct},
        {"write_file", vm_builtin_write_file},
        {"read_file",  vm_builtin_read_file},
        {"file_exists",vm_builtin_file_exists},
        {"index_of",   vm_builtin_index_of},
        {"indexOf",    vm_builtin_indexOf},
        {"count",      vm_builtin_count_fn},
        /* additional array builtins */
        {"sort",       vm_builtin_sort},
        {"first",      vm_builtin_first},
        {"last",       vm_builtin_last},
        {"compact",    vm_builtin_compact},
        {"slice",      vm_bi_slice},
        {"flatten",    vm_bi_flatten},
        /* additional string builtins */
        {"ltrim",      vm_builtin_ltrim},
        {"rtrim",      vm_builtin_rtrim},
        {"starts",     vm_builtin_starts},
        {"ends",       vm_builtin_ends},
        {"chars",      vm_builtin_chars},
        {"parseInt",   vm_builtin_parseInt},
        {"parseFloat", vm_builtin_parseFloat},
        /* math functions */
        {"sin",        vm_bi_sin},
        {"cos",        vm_bi_cos},
        {"tan",        vm_bi_tan},
        {"asin",       vm_bi_asin},
        {"acos",       vm_bi_acos},
        {"atan",       vm_bi_atan},
        {"atan2",      vm_bi_atan2},
        {"log",        vm_bi_log},
        {"log2",       vm_bi_log2},
        {"log10",      vm_bi_log10},
        {"exp",        vm_bi_exp},
        {"hypot",      vm_bi_hypot},
        {"isnan",      vm_bi_isnan},
        {"isinf",      vm_bi_isinf},
        {"clamp",      vm_bi_clamp},
        /* (math constants PI/E/TAU/INF/NAN registered as float values below) */
        /* utility */
        {"repr",       vm_bi_repr},
        {"id",         vm_bi_id},
        {"error",      vm_bi_error},
        {"exit",       vm_bi_exit_fn},
        {"clock",      vm_bi_clock_fn},
        {"time",       vm_bi_time_now},
        /* file system */
        {"is_file",    vm_bi_is_file},
        {"is_dir",     vm_bi_is_dir},
        {"file_size",  vm_bi_file_size},
        {"append_file",vm_bi_append_file},
        {"delete_file",vm_bi_delete_file},
        {"listdir",    vm_bi_listdir},
        {"getcwd",     vm_bi_getcwd},
        {"getenv",     vm_bi_getenv},
        {"gui_window", vmbi_gui_window},
        {"gui_label",  vmbi_gui_label},
        {"gui_button", vmbi_gui_button},
        {"gui_input",  vmbi_gui_input},
        {"gui_run",    vmbi_gui_run},
        /* X11 native GUI */
#ifdef HAVE_X11
        {"xgui_init",      vm_bi_xgui_init},
        {"xgui_style",     vm_bi_xgui_style},
        {"xgui_running",   vm_bi_xgui_running},
        {"xgui_begin",     vm_bi_xgui_begin},
        {"xgui_end",       vm_bi_xgui_end},
        {"xgui_label",     vm_bi_xgui_label},
        {"xgui_button",    vm_bi_xgui_button},
        {"xgui_input",     vm_bi_xgui_input},
        {"xgui_spacer",    vm_bi_xgui_spacer},
        {"xgui_row_begin", vm_bi_xgui_row_begin},
        {"xgui_row_end",   vm_bi_xgui_row_end},
        {"xgui_close",     vm_bi_xgui_close},
        {"xgui_title",     vm_bi_xgui_title},
        {"xgui_subtitle",  vm_bi_xgui_subtitle},
        {"xgui_separator", vm_bi_xgui_separator},
        {"xgui_checkbox",  vm_bi_xgui_checkbox},
        {"xgui_progress",  vm_bi_xgui_progress},
        {"xgui_slider",    vm_bi_xgui_slider},
        {"xgui_textarea",  vm_bi_xgui_textarea},
        {"xgui_badge",       vm_bi_xgui_badge},
        {"xgui_set_dark",    vm_bi_xgui_set_dark},
        {"xgui_card_begin",  vm_bi_xgui_card_begin},
        {"xgui_card_end",    vm_bi_xgui_card_end},
        {"xgui_tooltip",     vm_bi_xgui_tooltip},
        /* v4 new widgets */
        {"xgui_toggle",              vm_bi_xgui_toggle},
        {"xgui_chip",                vm_bi_xgui_chip},
        {"xgui_tabs",                vm_bi_xgui_tabs},
        {"xgui_select",              vm_bi_xgui_select},
        {"xgui_spinner",             vm_bi_xgui_spinner},
        {"xgui_list_item",           vm_bi_xgui_list_item},
        {"xgui_show_toast",          vm_bi_xgui_show_toast},
        {"xgui_section",             vm_bi_xgui_section},
        {"xgui_icon_button",         vm_bi_xgui_icon_button},
        {"xgui_group_begin",         vm_bi_xgui_group_begin},
        {"xgui_group_end",           vm_bi_xgui_group_end},
        {"xgui_grid_begin",          vm_bi_xgui_grid_begin},
        {"xgui_grid_end",            vm_bi_xgui_grid_end},
        /* game-mode raw drawing */
        {"xgui_clear_bg",            vm_bi_xgui_clear_bg},
        {"xgui_fill_rect_at",        vm_bi_xgui_fill_rect_at},
        {"xgui_fill_circle_at",      vm_bi_xgui_fill_circle_at},
        {"xgui_draw_line_at",        vm_bi_xgui_draw_line_at},
        {"xgui_draw_text_at",        vm_bi_xgui_draw_text_at},
        {"xgui_draw_text_centered",  vm_bi_xgui_draw_text_centered},
        {"xgui_draw_text_bold_at",   vm_bi_xgui_draw_text_bold_at},
        {"xgui_draw_text_bold_centered", vm_bi_xgui_draw_text_bold_centered},
        /* key-hold queries */
        {"xgui_key_w",       vm_bi_xgui_key_w},
        {"xgui_key_s",       vm_bi_xgui_key_s},
        {"xgui_key_a",       vm_bi_xgui_key_a},
        {"xgui_key_d",       vm_bi_xgui_key_d},
        {"xgui_key_up",      vm_bi_xgui_key_up},
        {"xgui_key_down",    vm_bi_xgui_key_down},
        {"xgui_key_left",    vm_bi_xgui_key_left},
        {"xgui_key_right",   vm_bi_xgui_key_right},
        {"xgui_key_space",   vm_bi_xgui_key_space},
        {"xgui_key_escape",  vm_bi_xgui_key_escape},
        {"xgui_mouse_down",  vm_bi_xgui_mouse_down},
        {"xgui_mouse_x",     vm_bi_xgui_mouse_x},
        {"xgui_mouse_y",     vm_bi_xgui_mouse_y},
        {"xgui_win_w",       vm_bi_xgui_win_w},
        {"xgui_win_h",       vm_bi_xgui_win_h},
        {"xgui_delta_ms",    vm_bi_xgui_delta_ms},
        {"xgui_clock_ms",    vm_bi_xgui_clock_ms},
        {"xgui_sleep_ms",    vm_bi_xgui_sleep_ms},
#else
        {"xgui_init",        vm_bi_xgui_no_x11},
        {"xgui_style",       vm_bi_xgui_no_x11},
        {"xgui_running",     vm_bi_xgui_no_x11},
        {"xgui_begin",       vm_bi_xgui_no_x11},
        {"xgui_end",         vm_bi_xgui_no_x11},
        {"xgui_label",       vm_bi_xgui_no_x11},
        {"xgui_button",      vm_bi_xgui_no_x11},
        {"xgui_input",       vm_bi_xgui_no_x11},
        {"xgui_spacer",      vm_bi_xgui_no_x11},
        {"xgui_row_begin",   vm_bi_xgui_no_x11},
        {"xgui_row_end",     vm_bi_xgui_no_x11},
        {"xgui_close",       vm_bi_xgui_no_x11},
        {"xgui_title",       vm_bi_xgui_no_x11},
        {"xgui_subtitle",    vm_bi_xgui_no_x11},
        {"xgui_separator",   vm_bi_xgui_no_x11},
        {"xgui_checkbox",    vm_bi_xgui_no_x11},
        {"xgui_progress",    vm_bi_xgui_no_x11},
        {"xgui_slider",      vm_bi_xgui_no_x11},
        {"xgui_textarea",    vm_bi_xgui_no_x11},
        {"xgui_badge",       vm_bi_xgui_no_x11},
        {"xgui_set_dark",    vm_bi_xgui_no_x11},
        {"xgui_card_begin",  vm_bi_xgui_no_x11},
        {"xgui_card_end",    vm_bi_xgui_no_x11},
        {"xgui_tooltip",     vm_bi_xgui_no_x11},
#endif
        {NULL, NULL}
    };
    for (int i = 0; bi[i].name; i++) {
        Value v = value_builtin(bi[i].name, bi[i].fn);
        env_set(vm->globals, bi[i].name, v, false);
        value_release(v);
    }
    /* Register math constants as const float values (not function builtins) */
    {
        struct { const char *name; double val; } consts[] = {
            {"PI",  3.14159265358979323846},
            {"E",   2.71828182845904523536},
            {"TAU", 6.28318530717958647692},
            {NULL,  0.0}
        };
        for (int i = 0; consts[i].name; i++) {
            Value v = value_float(consts[i].val);
            env_set(vm->globals, consts[i].name, v, false);
            value_release(v);
        }
        Value inf_v = value_float(1.0/0.0);
        env_set(vm->globals, "INF", inf_v, false); value_release(inf_v);
        Value nan_v = value_float(0.0/0.0);
        env_set(vm->globals, "NAN", nan_v, false); value_release(nan_v);
    }
    Value memory = value_dict_new();
    Value key = value_string("__module");
    Value name = value_string("memory");
    value_dict_set(memory, key, name);
    value_release(key);
    value_release(name);
    env_set(vm->globals, "memory", memory, true);
    value_release(memory);
}

/* ================================================================== f-string processing */

static char *vm_process_fstring(VM *vm, const char *tmpl, Env *env);

/* Forward declarations for method dispatch */


static Value vm_dispatch_method_slow(VM *vm, Value obj, const char *method, Value *args, int argc, int line);
static Value vm_dispatch_method_cached(VM *vm, Value obj, const char *method, VmMethodId method_id, Value *args,
                                         int argc, int line);
static void method_table_init(void);

/* ================================================================== VM new/free */

VM *vm_new(void) {
    VM *vm = calloc(1, sizeof(VM));
    vm->gc  = gc_global();
    vm->jit = NULL;            /* JIT disabled by default; enable with vm->jit = jit_new() */
    vm->jit_verbose = false;
    vm->globals = env_new(NULL);
    method_table_init();  /* build O(1) method dispatch table */
    vm_register_builtins(vm);
    return vm;
}

/* Defer a module chunk for cleanup at vm_free time. This prevents use-after-free
 * crashes when functions created via OP_MAKE_FUNCTION borrow chunk pointers from
 * the module's chunk constants pool. */
static void vm_defer_module_chunk(VM *vm, Chunk *c) {
    if (!c) return;
    if (vm->mod_chunks_count >= vm->mod_chunks_cap) {
        int new_cap = vm->mod_chunks_cap < 8 ? 8 : vm->mod_chunks_cap * 2;
        vm->mod_chunks = realloc(vm->mod_chunks, new_cap * sizeof(Chunk *));
        vm->mod_chunks_cap = new_cap;
    }
    vm->mod_chunks[vm->mod_chunks_count++] = c;
}

void vm_free(VM *vm) {
    if (vm->jit) {
        if (vm->jit_verbose) jit_print_stats(vm->jit);
        jit_free(vm->jit);
        vm->jit = NULL;
    }
    /* Release globals first (this releases function objects whose chunk pointers
     * come from the prelude_chunk constants pool). */
    env_free_root(vm->globals);
    /* Now safe to free the prelude chunk and its constants (function protos). */
    if (vm->prelude_chunk) {
        chunk_free(vm->prelude_chunk);
        free(vm->prelude_chunk);
        vm->prelude_chunk = NULL;
    }
    /* Free deferred module chunks (kept alive so borrowed chunk pointers remain
     * valid during the entire execution lifetime). */
    for (int i = 0; i < vm->mod_chunks_count; i++) {
        chunk_free(vm->mod_chunks[i]);
        free(vm->mod_chunks[i]);
    }
    free(vm->mod_chunks);
    vm->mod_chunks = NULL;
    vm->mod_chunks_count = vm->mod_chunks_cap = 0;
    /* Item 5: free the static HTML-GUI body buffer allocated by vmgui_append */
    if (g_vmgui.body) {
        free(g_vmgui.body);
        g_vmgui.body     = NULL;
        g_vmgui.body_len = 0;
        g_vmgui.body_cap = 0;
    }
#ifdef HAVE_X11
    /* Item 5: destroy the X11 GUI handle if the program exited without xgui_close */
    if (g_vm_xgui) { xgui_destroy(g_vm_xgui); g_vm_xgui = NULL; }
#endif
    free(vm);
}

/* ================================================================== method dispatch hash table
 *
 * Built once at VM startup after the GC intern table is ready.
 * Keys are (ValueType, interned const char*) pairs stored in an open-address
 * hash table.  Lookup is a pointer-equality comparison — O(1) per call.
 * ================================================================== */

#define METHOD_TABLE_CAP 128  /* power of 2; must be > 2× the number of methods */

typedef struct {
    const char *key;  /* interned method name pointer; NULL = empty slot */
    ValueType   type;
    VmMethodId  id;
} MethodEntry;

static MethodEntry s_method_table[METHOD_TABLE_CAP];
static bool        s_method_table_ready = false;

static inline unsigned method_slot(ValueType type, const char *key) {
    uintptr_t v = (uintptr_t)key ^ ((uintptr_t)(unsigned)type * 2654435761u);
    v ^= v >> 16;
    return (unsigned)v & (METHOD_TABLE_CAP - 1);
}

static void method_table_put(ValueType type, const char *name, VmMethodId id) {
    const char *key = gc_intern_cstr(gc_global(), name);
    unsigned    h   = method_slot(type, key);
    for (int i = 0; i < METHOD_TABLE_CAP; i++) {
        unsigned idx = (h + (unsigned)i) & (METHOD_TABLE_CAP - 1);
        if (!s_method_table[idx].key) {
            s_method_table[idx].key  = key;
            s_method_table[idx].type = type;
            s_method_table[idx].id   = id;
            return;
        }
    }
}

static void method_table_init(void) {
    if (s_method_table_ready) return;
    memset(s_method_table, 0, sizeof(s_method_table));
    /* STRING */
    method_table_put(VAL_STRING, "upper",      VM_METHOD_STRING_UPPER);
    method_table_put(VAL_STRING, "lower",      VM_METHOD_STRING_LOWER);
    method_table_put(VAL_STRING, "strip",      VM_METHOD_STRING_STRIP);
    method_table_put(VAL_STRING, "lstrip",     VM_METHOD_STRING_LSTRIP);
    method_table_put(VAL_STRING, "rstrip",     VM_METHOD_STRING_RSTRIP);
    method_table_put(VAL_STRING, "len",        VM_METHOD_STRING_LEN);
    method_table_put(VAL_STRING, "capitalize", VM_METHOD_STRING_CAPITALIZE);
    method_table_put(VAL_STRING, "find",       VM_METHOD_STRING_FIND);
    method_table_put(VAL_STRING, "index",      VM_METHOD_STRING_FIND);
    method_table_put(VAL_STRING, "replace",    VM_METHOD_STRING_REPLACE);
    method_table_put(VAL_STRING, "startswith", VM_METHOD_STRING_STARTSWITH);
    method_table_put(VAL_STRING, "endswith",   VM_METHOD_STRING_ENDSWITH);
    method_table_put(VAL_STRING, "split",      VM_METHOD_STRING_SPLIT);
    method_table_put(VAL_STRING, "join",       VM_METHOD_STRING_JOIN);
    method_table_put(VAL_STRING, "isdigit",    VM_METHOD_STRING_ISDIGIT);
    method_table_put(VAL_STRING, "isalpha",    VM_METHOD_STRING_ISALPHA);
    /* ARRAY */
    method_table_put(VAL_ARRAY, "add",    VM_METHOD_ARRAY_ADD);
    method_table_put(VAL_ARRAY, "pop",    VM_METHOD_ARRAY_POP);
    method_table_put(VAL_ARRAY, "sort",   VM_METHOD_ARRAY_SORT);
    method_table_put(VAL_ARRAY, "insert", VM_METHOD_ARRAY_INSERT);
    method_table_put(VAL_ARRAY, "remove", VM_METHOD_ARRAY_REMOVE);
    method_table_put(VAL_ARRAY, "extend", VM_METHOD_ARRAY_EXTEND);
    method_table_put(VAL_ARRAY, "len",    VM_METHOD_ARRAY_LEN);
    /* DICT */
    method_table_put(VAL_DICT, "keys",   VM_METHOD_DICT_KEYS);
    method_table_put(VAL_DICT, "values", VM_METHOD_DICT_VALUES);
    method_table_put(VAL_DICT, "items",  VM_METHOD_DICT_ITEMS);
    method_table_put(VAL_DICT, "erase",  VM_METHOD_DICT_ERASE);
    method_table_put(VAL_DICT, "get",    VM_METHOD_DICT_GET);
    /* SET */
    method_table_put(VAL_SET, "add",     VM_METHOD_SET_ADD);
    method_table_put(VAL_SET, "remove",  VM_METHOD_SET_REMOVE);
    method_table_put(VAL_SET, "discard", VM_METHOD_SET_DISCARD);
    method_table_put(VAL_SET, "update",  VM_METHOD_SET_UPDATE);
    /* TUPLE */
    method_table_put(VAL_TUPLE, "count", VM_METHOD_TUPLE_COUNT);
    method_table_put(VAL_TUPLE, "index", VM_METHOD_TUPLE_INDEX);
    s_method_table_ready = true;
}

/* ================================================================== method dispatch */

static VmMethodId vm_resolve_method_id(ValueType type, const char *method) {
    /* Intern the method name and look up in the O(1) hash table. */
    const char *key = gc_intern_cstr(gc_global(), method);
    unsigned    h   = method_slot(type, key);
    for (int i = 0; i < METHOD_TABLE_CAP; i++) {
        unsigned idx = (h + (unsigned)i) & (METHOD_TABLE_CAP - 1);
        if (!s_method_table[idx].key) return VM_METHOD_UNKNOWN; /* empty slot = not found */
        if (s_method_table[idx].type == type && s_method_table[idx].key == key)
            return s_method_table[idx].id;
    }
    return VM_METHOD_UNKNOWN;
}

static void vm_no_method_error(VM *vm, Value obj, const char *method, int line) {
    char msg[256];
    snprintf(msg, sizeof(msg), "type '%s' has no method '%s'",
             value_type_name(VAL_TYPE(obj)), method);
    vm_error(vm, msg, line);
}

static Value vm_dispatch_method_slow(VM *vm, Value obj, const char *method, Value *args, int argc, int line) {
    /* Re-use the interpreter's method dispatch by creating a temporary interpreter. */
    /* Actually we implement the methods directly here to avoid coupling. */

    if (VAL_TYPE(obj) == VAL_STRING) {
        const char *s = AS_STR(obj);
        if (strcmp(method, "upper") == 0) {
            char *r = strdup(s);
            for (int i = 0; r[i]; i++) if (r[i]>='a'&&r[i]<='z') r[i]-=32;
            return value_string_take(r);
        }
        if (strcmp(method, "lower") == 0) {
            char *r = strdup(s);
            for (int i = 0; r[i]; i++) if (r[i]>='A'&&r[i]<='Z') r[i]+=32;
            return value_string_take(r);
        }
        if (strcmp(method, "strip") == 0) {
            const char *start = s;
            while (*start == ' ' || *start == '\t' || *start == '\n') start++;
            const char *end = s + strlen(s);
            while (end > start && (*(end-1)==' '||*(end-1)=='\t'||*(end-1)=='\n')) end--;
            size_t len = (size_t)(end - start);
            char *r = malloc(len + 1); memcpy(r, start, len); r[len] = '\0';
            return value_string_take(r);
        }
        if (strcmp(method, "lstrip") == 0) {
            const char *p = s;
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            return value_string(p);
        }
        if (strcmp(method, "rstrip") == 0) {
            char *r = strdup(s);
            int len = (int)strlen(r);
            while (len > 0 && (r[len-1]==' '||r[len-1]=='\t'||r[len-1]=='\n')) r[--len]='\0';
            return value_string_take(r);
        }
        if (strcmp(method, "len") == 0) return value_int((long long)strlen(s));
        if (strcmp(method, "capitalize") == 0) {
            char *r = strdup(s);
            if (r[0]>='a'&&r[0]<='z') r[0]-=32;
            for (int i=1;r[i];i++) if(r[i]>='A'&&r[i]<='Z') r[i]+=32;
            return value_string_take(r);
        }
        if (strcmp(method, "find") == 0 && argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING) {
            const char *p = strstr(s, AS_STR(args[0]));
            return value_int(p ? (long long)(p - s) : -1);
        }
        if (strcmp(method, "replace") == 0 && argc >= 2
            && VAL_TYPE(args[0])==VAL_STRING && VAL_TYPE(args[1])==VAL_STRING) {
            const char *from = AS_STR(args[0]), *to = AS_STR(args[1]);
            size_t flen = strlen(from), tlen = strlen(to);
            size_t rlen = 0, slen = strlen(s);
            const char *p = s;
            while ((p = strstr(p, from)) != NULL) { rlen += tlen - flen; p += flen; }
            char *r = malloc(slen + rlen + 1);
            char *out = r; p = s;
            const char *q;
            while ((q = strstr(p, from)) != NULL) {
                size_t pre = (size_t)(q - p);
                memcpy(out, p, pre); out += pre;
                memcpy(out, to, tlen); out += tlen;
                p = q + flen;
            }
            strcpy(out, p);
            return value_string_take(r);
        }
        if (strcmp(method, "startswith") == 0 && argc >= 1 && VAL_TYPE(args[0])==VAL_STRING)
            return value_bool(strncmp(s, AS_STR(args[0]), strlen(AS_STR(args[0])))==0 ? 1 : 0);
        if (strcmp(method, "endswith") == 0 && argc >= 1 && VAL_TYPE(args[0])==VAL_STRING) {
            size_t sl=strlen(s), pl=strlen(AS_STR(args[0]));
            return value_bool(sl>=pl && strcmp(s+sl-pl, AS_STR(args[0]))==0 ? 1 : 0);
        }
        if (strcmp(method, "split") == 0) {
            const char *delim = (argc>=1&&VAL_TYPE(args[0])==VAL_STRING) ? AS_STR(args[0]) : " ";
            Value arr = value_array_new();
            char *copy = strdup(s), *tok, *save = NULL;
            size_t dl = strlen(delim);
            if (dl == 0) { free(copy); return arr; }
            char *p2 = copy;
            while (1) {
                tok = strstr(p2, delim);
                if (!tok) { value_array_push(arr, value_string(p2)); break; }
                *tok = '\0'; value_array_push(arr, value_string(p2)); p2 = tok + dl;
            }
            (void)save; free(copy);
            return arr;
        }
        if (strcmp(method, "join") == 0 && argc >= 1 &&
            (VAL_TYPE(args[0])==VAL_ARRAY||VAL_TYPE(args[0])==VAL_TUPLE)) {
            ValueArray *arr2 = VAL_TYPE(args[0])==VAL_ARRAY ? &AS_ARRAY(args[0]) : &AS_TUPLE(args[0]);
            size_t dlen = strlen(s), total = 0;
            for (int i=0;i<arr2->len;i++) {
                char *tmp = value_to_string(arr2->items[i]);
                total += strlen(tmp); free(tmp);
                if (i < arr2->len-1) total += dlen;
            }
            char *r = malloc(total+1); r[0]='\0';
            for (int i=0;i<arr2->len;i++) {
                char *tmp = value_to_string(arr2->items[i]);
                strcat(r, tmp); free(tmp);
                if (i < arr2->len-1) strcat(r, s);
            }
            return value_string_take(r);
        }
        if (strcmp(method, "isdigit")==0) {
            bool ok = *s!='\0';
            for (const char *p=s;*p;p++) if(*p<'0'||*p>'9'){ok=false;break;}
            return value_bool(ok?1:0);
        }
        if (strcmp(method, "isalpha")==0) {
            bool ok = *s!='\0';
            for (const char *p=s;*p;p++) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))){ok=false;break;}
            return value_bool(ok?1:0);
        }
    }

    if (VAL_TYPE(obj) == VAL_ARRAY) {
        if (strcmp(method, "add") == 0 && argc >= 1) {
            value_array_push(obj, args[0]); return value_null();
        }
        if (strcmp(method, "pop") == 0) {
            long long idx = (argc>=1&&VAL_TYPE(args[0])==VAL_INT) ? AS_INT(args[0]) : -1;
            return value_array_pop(obj, idx);
        }
        if (strcmp(method, "sort") == 0) { value_array_sort(obj); return value_null(); }
        if (strcmp(method, "insert") == 0 && argc >= 2 && VAL_TYPE(args[0])==VAL_INT) {
            value_array_insert(obj, AS_INT(args[0]), args[1]); return value_null();
        }
        if (strcmp(method, "remove") == 0 && argc >= 1) {
            value_array_remove(obj, args[0]); return value_null();
        }
        if (strcmp(method, "extend") == 0 && argc >= 1) {
            value_array_extend(obj, args[0]); return value_null();
        }
        if (strcmp(method, "len") == 0) return value_int(AS_ARRAY(obj).len);
    }

    if (VAL_TYPE(obj) == VAL_DICT) {
        if (strcmp(method, "keys") == 0) {
            Value arr2 = value_array_new();
            for (int i=0;i<AS_DICT(obj).len;i++)
                value_array_push(arr2, value_retain(AS_DICT(obj).entries[i].key));
            return arr2;
        }
        if (strcmp(method, "values") == 0) {
            Value arr2 = value_array_new();
            for (int i=0;i<AS_DICT(obj).len;i++)
                value_array_push(arr2, value_retain(AS_DICT(obj).entries[i].val));
            return arr2;
        }
        if (strcmp(method, "items") == 0) {
            Value arr2 = value_array_new();
            for (int i=0;i<AS_DICT(obj).len;i++) {
                Value items[2] = {AS_DICT(obj).entries[i].key, AS_DICT(obj).entries[i].val};
                value_array_push(arr2, value_tuple_new(items, 2));
            }
            return arr2;
        }
        if (strcmp(method, "erase") == 0) {
            value_dict_clear(obj);
            return value_null();
        }
        if (strcmp(method, "get") == 0 && argc >= 1) {
            Value v = value_dict_get(obj, args[0]);
            if (!v) v = (argc>=2) ? value_retain(args[1]) : value_null();
            return v;
        }
    }

    if (VAL_TYPE(obj) == VAL_SET) {
        if (strcmp(method,"add")==0&&argc>=1) { value_set_add(obj,args[0]); return value_null(); }
        if (strcmp(method,"remove")==0&&argc>=1) { value_set_remove(obj,args[0]); return value_null(); }
        if (strcmp(method,"discard")==0&&argc>=1) { value_set_remove(obj,args[0]); return value_null(); }
        if (strcmp(method,"update")==0&&argc>=1&&(VAL_TYPE(args[0])==VAL_SET||VAL_TYPE(args[0])==VAL_ARRAY)) {
            ValueArray *src = VAL_TYPE(args[0])==VAL_SET ? &AS_SET(args[0]) : &AS_ARRAY(args[0]);
            for (int i=0;i<src->len;i++) value_set_add(obj,src->items[i]);
            return value_null();
        }
    }

    if (VAL_TYPE(obj) == VAL_TUPLE) {
        if (strcmp(method,"count")==0&&argc>=1) {
            int cnt=0;
            for (int i=0;i<AS_TUPLE(obj).len;i++)
                if(value_equals(AS_TUPLE(obj).items[i],args[0])) cnt++;
            return value_int(cnt);
        }
        if (strcmp(method,"index")==0&&argc>=1) {
            for (int i=0;i<AS_TUPLE(obj).len;i++)
                if(value_equals(AS_TUPLE(obj).items[i],args[0])) return value_int(i);
            return value_int(-1);
        }
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "type '%s' has no method '%s'",
             value_type_name(VAL_TYPE(obj)), method);
    vm_error(vm, msg, line);
    return value_null();
}

static Value vm_dispatch_method_cached(VM *vm, Value obj, const char *method, VmMethodId method_id, Value *args,
                                         int argc, int line) {
    if (vm_is_memory_module(obj))
        return vm_memory_method(vm, obj, method, args, argc, line);
    if (method_id == VM_METHOD_UNKNOWN)
        return vm_dispatch_method_slow(vm, obj, method, args, argc, line);

    const char *s = VAL_TYPE(obj) == VAL_STRING ? AS_STR(obj) : NULL;
    switch (method_id) {
        case VM_METHOD_STRING_UPPER: {
            char *r = strdup(s);
            for (int i = 0; r[i]; i++) if (r[i]>='a'&&r[i]<='z') r[i]-=32;
            return value_string_take(r);
        }
        case VM_METHOD_STRING_LOWER: {
            char *r = strdup(s);
            for (int i = 0; r[i]; i++) if (r[i]>='A'&&r[i]<='Z') r[i]+=32;
            return value_string_take(r);
        }
        case VM_METHOD_STRING_STRIP: {
            const char *start = s;
            while (*start == ' ' || *start == '\t' || *start == '\n') start++;
            const char *end = s + strlen(s);
            while (end > start && (*(end-1)==' '||*(end-1)=='\t'||*(end-1)=='\n')) end--;
            size_t len = (size_t)(end - start);
            char *r = malloc(len + 1); memcpy(r, start, len); r[len] = '\0';
            return value_string_take(r);
        }
        case VM_METHOD_STRING_LSTRIP: {
            const char *p = s;
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            return value_string(p);
        }
        case VM_METHOD_STRING_RSTRIP: {
            char *r = strdup(s);
            int len = (int)strlen(r);
            while (len > 0 && (r[len-1]==' '||r[len-1]=='\t'||r[len-1]=='\n')) r[--len]='\0';
            return value_string_take(r);
        }
        case VM_METHOD_STRING_LEN:
            return value_int((long long)strlen(s));
        case VM_METHOD_STRING_CAPITALIZE: {
            char *r = strdup(s);
            if (r[0]>='a'&&r[0]<='z') r[0]-=32;
            for (int i=1;r[i];i++) if(r[i]>='A'&&r[i]<='Z') r[i]+=32;
            return value_string_take(r);
        }
        case VM_METHOD_STRING_FIND:
            if (argc >= 1 && VAL_TYPE(args[0]) == VAL_STRING) {
                const char *p = strstr(s, AS_STR(args[0]));
                return value_int(p ? (long long)(p - s) : -1);
            }
            break;
        case VM_METHOD_STRING_REPLACE:
            if (argc >= 2 && VAL_TYPE(args[0])==VAL_STRING && VAL_TYPE(args[1])==VAL_STRING) {
                const char *from = AS_STR(args[0]), *to = AS_STR(args[1]);
                size_t flen = strlen(from), tlen = strlen(to);
                size_t rlen = 0, slen = strlen(s);
                const char *p = s;
                while ((p = strstr(p, from)) != NULL) { rlen += tlen - flen; p += flen; }
                char *r = malloc(slen + rlen + 1);
                char *out = r; p = s;
                const char *q;
                while ((q = strstr(p, from)) != NULL) {
                    size_t pre = (size_t)(q - p);
                    memcpy(out, p, pre); out += pre;
                    memcpy(out, to, tlen); out += tlen;
                    p = q + flen;
                }
                strcpy(out, p);
                return value_string_take(r);
            }
            break;
        case VM_METHOD_STRING_STARTSWITH:
            if (argc >= 1 && VAL_TYPE(args[0])==VAL_STRING)
                return value_bool(strncmp(s, AS_STR(args[0]), strlen(AS_STR(args[0])))==0 ? 1 : 0);
            break;
        case VM_METHOD_STRING_ENDSWITH:
            if (argc >= 1 && VAL_TYPE(args[0])==VAL_STRING) {
                size_t sl=strlen(s), pl=strlen(AS_STR(args[0]));
                return value_bool(sl>=pl && strcmp(s+sl-pl, AS_STR(args[0]))==0 ? 1 : 0);
            }
            break;
        case VM_METHOD_STRING_SPLIT: {
            const char *delim = (argc>=1&&VAL_TYPE(args[0])==VAL_STRING) ? AS_STR(args[0]) : " ";
            Value arr = value_array_new();
            char *copy = strdup(s);
            size_t dl = strlen(delim);
            if (dl == 0) { free(copy); return arr; }
            char *p2 = copy;
            while (1) {
                char *tok = strstr(p2, delim);
                if (!tok) { value_array_push(arr, value_string(p2)); break; }
                *tok = '\0'; value_array_push(arr, value_string(p2)); p2 = tok + dl;
            }
            free(copy);
            return arr;
        }
        case VM_METHOD_STRING_JOIN:
            if (argc >= 1 && (VAL_TYPE(args[0])==VAL_ARRAY||VAL_TYPE(args[0])==VAL_TUPLE)) {
                ValueArray *arr2 = VAL_TYPE(args[0])==VAL_ARRAY ? &AS_ARRAY(args[0]) : &AS_TUPLE(args[0]);
                size_t dlen = strlen(s), total = 0;
                for (int i=0;i<arr2->len;i++) {
                    char *tmp = value_to_string(arr2->items[i]);
                    total += strlen(tmp); free(tmp);
                    if (i < arr2->len-1) total += dlen;
                }
                char *r = malloc(total+1); r[0]='\0';
                for (int i=0;i<arr2->len;i++) {
                    char *tmp = value_to_string(arr2->items[i]);
                    strcat(r, tmp); free(tmp);
                    if (i < arr2->len-1) strcat(r, s);
                }
                return value_string_take(r);
            }
            break;
        case VM_METHOD_STRING_ISDIGIT: {
            bool ok = *s!='\0';
            for (const char *p=s;*p;p++) if(*p<'0'||*p>'9'){ok=false;break;}
            return value_bool(ok?1:0);
        }
        case VM_METHOD_STRING_ISALPHA: {
            bool ok = *s!='\0';
            for (const char *p=s;*p;p++) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))){ok=false;break;}
            return value_bool(ok?1:0);
        }
        case VM_METHOD_ARRAY_ADD:
            if (argc >= 1) { value_array_push(obj, args[0]); return value_null(); }
            break;
        case VM_METHOD_ARRAY_POP: {
            long long idx = (argc>=1&&VAL_TYPE(args[0])==VAL_INT) ? AS_INT(args[0]) : -1;
            return value_array_pop(obj, idx);
        }
        case VM_METHOD_ARRAY_SORT:
            value_array_sort(obj); return value_null();
        case VM_METHOD_ARRAY_INSERT:
            if (argc >= 2 && VAL_TYPE(args[0])==VAL_INT) { value_array_insert(obj, AS_INT(args[0]), args[1]); return value_null(); }
            break;
        case VM_METHOD_ARRAY_REMOVE:
            if (argc >= 1) { value_array_remove(obj, args[0]); return value_null(); }
            break;
        case VM_METHOD_ARRAY_EXTEND:
            if (argc >= 1) { value_array_extend(obj, args[0]); return value_null(); }
            break;
        case VM_METHOD_ARRAY_LEN:
            return value_int(AS_ARRAY(obj).len);
        case VM_METHOD_DICT_KEYS: {
            Value arr2 = value_array_new();
            for (int i=0;i<AS_DICT(obj).len;i++) value_array_push(arr2, value_retain(AS_DICT(obj).entries[i].key));
            return arr2;
        }
        case VM_METHOD_DICT_VALUES: {
            Value arr2 = value_array_new();
            for (int i=0;i<AS_DICT(obj).len;i++) value_array_push(arr2, value_retain(AS_DICT(obj).entries[i].val));
            return arr2;
        }
        case VM_METHOD_DICT_ITEMS: {
            Value arr2 = value_array_new();
            for (int i=0;i<AS_DICT(obj).len;i++) {
                Value items[2] = {AS_DICT(obj).entries[i].key, AS_DICT(obj).entries[i].val};
                value_array_push(arr2, value_tuple_new(items, 2));
            }
            return arr2;
        }
        case VM_METHOD_DICT_ERASE:
            value_dict_clear(obj); return value_null();
        case VM_METHOD_DICT_GET:
            if (argc >= 1) {
                Value v = value_dict_get(obj, args[0]);
                return v ? value_retain(v) : (argc>=2 ? value_retain(args[1]) : value_null());
            }
            break;
        case VM_METHOD_SET_ADD:
            if (argc>=1) { value_set_add(obj,args[0]); return value_null(); }
            break;
        case VM_METHOD_SET_REMOVE:
        case VM_METHOD_SET_DISCARD:
            if (argc>=1) { value_set_remove(obj,args[0]); return value_null(); }
            break;
        case VM_METHOD_SET_UPDATE:
            if (argc>=1&&(VAL_TYPE(args[0])==VAL_SET||VAL_TYPE(args[0])==VAL_ARRAY)) {
                ValueArray *src = VAL_TYPE(args[0])==VAL_SET ? &AS_SET(args[0]) : &AS_ARRAY(args[0]);
                for (int i=0;i<src->len;i++) value_set_add(obj,src->items[i]);
                return value_null();
            }
            break;
        case VM_METHOD_TUPLE_COUNT:
            if (argc>=1) {
                int cnt=0;
                for (int i=0;i<AS_TUPLE(obj).len;i++) if(value_equals(AS_TUPLE(obj).items[i],args[0])) cnt++;
                return value_int(cnt);
            }
            break;
        case VM_METHOD_TUPLE_INDEX:
            if (argc>=1) {
                for (int i=0;i<AS_TUPLE(obj).len;i++)
                    if(value_equals(AS_TUPLE(obj).items[i],args[0])) return value_int(i);
                return value_int(-1);
            }
            break;
        default:
            break;
    }

    vm_no_method_error(vm, obj, method, line);
    return value_null();
}

/* ================================================================== call function */

/* ================================================================== f-string */

static char *vm_process_fstring(VM *vm, const char *tmpl, Env *env) {
    /* Use the interpreter's public f-string processor. */
    Interpreter tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.globals = vm->globals;
    return interpreter_process_fstring(&tmp, tmpl, env);
}

/* ================================================================== get_iter */

/* Convert a value to an array of items for for-in iteration. */
static Value vm_get_iter(VM *vm, Value v, int line) {
    Value arr = value_array_new();
    switch (VAL_TYPE(v)) {
        case VAL_ARRAY:
            for (int i = 0; i < AS_ARRAY(v).len; i++)
                value_array_push(arr, value_retain(AS_ARRAY(v).items[i]));
            break;
        case VAL_TUPLE:
            for (int i = 0; i < AS_TUPLE(v).len; i++)
                value_array_push(arr, value_retain(AS_TUPLE(v).items[i]));
            break;
        case VAL_SET:
            for (int i = 0; i < AS_SET(v).len; i++)
                value_array_push(arr, value_retain(AS_SET(v).items[i]));
            break;
        case VAL_STRING: {
            const char *s = AS_STR(v);
            while (*s) {
                char ch[2] = {*s++, '\0'};
                value_array_push(arr, value_string(ch));
            }
            break;
        }
        case VAL_DICT:
            for (int i = 0; i < AS_DICT(v).len; i++)
                value_array_push(arr, value_retain(AS_DICT(v).entries[i].key));
            break;
        default:
            vm_error(vm, "value is not iterable", line);
            break;
    }
    return arr;
}

/* ================================================================== value_do_slice */

static Value vm_do_slice(VM *vm, Value obj, Value start_v, Value stop_v, Value step_v, int line) {
    long long step  = (step_v  && VAL_TYPE(step_v)  == VAL_INT) ? AS_INT(step_v)  : 1;
    if (step == 0) { vm_error(vm, "slice step cannot be zero", line); return value_null(); }

    long long len = 0;
    if      (VAL_TYPE(obj) == VAL_STRING) len = (long long)strlen(AS_STR(obj));
    else if (VAL_TYPE(obj) == VAL_ARRAY)  len = AS_ARRAY(obj).len;
    else if (VAL_TYPE(obj) == VAL_TUPLE)  len = AS_TUPLE(obj).len;
    else { vm_error(vm, "type is not sliceable", line); return value_null(); }

    long long start = (start_v && VAL_TYPE(start_v) == VAL_INT) ? AS_INT(start_v)
                      : (step > 0 ? 0 : len - 1);
    long long stop  = (stop_v  && VAL_TYPE(stop_v)  == VAL_INT) ? AS_INT(stop_v)
                      : (step > 0 ? len : -len - 1);

    if (start < 0) start += len;
    if (stop  < 0) stop  += len;
    if (step > 0) {
        if (start < 0) start = 0;
        if (stop  > len) stop = len;
    } else {
        if (start >= len) start = len - 1;
    }

    if (VAL_TYPE(obj) == VAL_STRING) {
        const char *s = AS_STR(obj);
        int cap2 = 64, sz2 = 0;
        char *r = malloc(cap2);
        for (long long i = start; step > 0 ? i < stop : i > stop; i += step) {
            if (i >= 0 && i < len) {
                if (sz2 + 2 >= cap2) { cap2 *= 2; r = realloc(r, cap2); }
                r[sz2++] = s[i];
            }
        }
        r[sz2] = '\0';
        return value_string_take(r);
    }

    ValueArray *src = (VAL_TYPE(obj) == VAL_ARRAY) ? &AS_ARRAY(obj) : &AS_TUPLE(obj);
    Value res = (VAL_TYPE(obj) == VAL_ARRAY) ? value_array_new() : value_null();
    Value *items_buf = NULL;
    int items_count = 0, items_cap = 8;
    if (VAL_TYPE(obj) == VAL_TUPLE) items_buf = malloc(items_cap * sizeof(Value));

    for (long long i = start; step > 0 ? i < stop : i > stop; i += step) {
        if (i >= 0 && i < len) {
            if (VAL_TYPE(obj) == VAL_ARRAY) {
                value_array_push(res, value_retain(src->items[i]));
            } else {
                if (items_count >= items_cap) { items_cap*=2; items_buf=realloc(items_buf,items_cap*sizeof(Value)); }
                items_buf[items_count++] = value_retain(src->items[i]);
            }
        }
    }
    if (VAL_TYPE(obj) == VAL_TUPLE) {
        res = value_tuple_new(items_buf, items_count);
        for (int i=0;i<items_count;i++) value_release(items_buf[i]);
        free(items_buf);
    }
    return res;
}

/* ================================================================== prelude */

static const char *PRISM_PRELUDE =
    "func filter(arr, f) {\n"
    "    let out = []\n"
    "    for x in arr { if f(x) { push(out, x) } }\n"
    "    return out\n"
    "}\n"
    /* map(arr, f) OR map(f, arr) — detect by first arg type */
    "func map(a, b) {\n"
    "    let out = []\n"
    "    if type(a) == \"array\" {\n"
    "        for x in a { push(out, b(x)) }\n"
    "    } else {\n"
    "        for x in b { push(out, a(x)) }\n"
    "    }\n"
    "    return out\n"
    "}\n"
    "func reduce(arr, f, init) {\n"
    "    let acc = init\n"
    "    for x in arr { acc = f(acc, x) }\n"
    "    return acc\n"
    "}\n"
    "func forEach(arr, f) {\n"
    "    for x in arr { f(x) }\n"
    "}\n"
    "func flatMap(arr, f) {\n"
    "    let out = []\n"
    "    for x in arr {\n"
    "        let r = f(x)\n"
    "        if type(r) == \"array\" { for y in r { push(out, y) } }\n"
    "        else { push(out, r) }\n"
    "    }\n"
    "    return out\n"
    "}\n"
    "func find(arr, f) {\n"
    "    for x in arr { if f(x) { return x } }\n"
    "    return null\n"
    "}\n"
    "func sortBy(arr, f) {\n"
    "    let cp = copy(arr)\n"
    "    let n = len(cp)\n"
    "    let i = 0\n"
    "    while i < n {\n"
    "        let j = i + 1\n"
    "        while j < n {\n"
    "            if f(cp[i]) > f(cp[j]) {\n"
    "                let tmp = cp[i]\n"
    "                cp[i] = cp[j]\n"
    "                cp[j] = tmp\n"
    "            }\n"
    "            j += 1\n"
    "        }\n"
    "        i += 1\n"
    "    }\n"
    "    return cp\n"
    "}\n"
    "func groupBy(arr, f) {\n"
    "    let out = {}\n"
    "    for x in arr {\n"
    "        let k = str(f(x))\n"
    "        if !has(out, k) { out[k] = [] }\n"
    "        push(out[k], x)\n"
    "    }\n"
    "    return out\n"
    "}\n";

int vm_run_prelude(VM *vm) {
    Parser  *p   = parser_new(PRISM_PRELUDE);
    ASTNode *ast = parser_parse(p);
    if (p->had_error) { parser_free(p); if (ast) ast_node_free(ast); return 1; }

    /* Allocate the prelude chunk on the heap and keep it alive in vm->prelude_chunk
     * for the full lifetime of the VM.  The function objects created from the
     * prelude (filter, map, reduce, …) hold non-owning pointers into this
     * chunk's sub-chunks; freeing it early would leave those pointers dangling. */
    Chunk *prelude_chunk = malloc(sizeof(Chunk));
    if (!prelude_chunk) { ast_node_free(ast); parser_free(p); return 1; }
    char  errbuf[256] = {0};
    if (compile(ast, prelude_chunk, errbuf, sizeof(errbuf))) {
        free(prelude_chunk);
        ast_node_free(ast); parser_free(p); return 1;
    }
    ast_node_free(ast);
    parser_free(p);

    int rc = vm_run(vm, prelude_chunk);
    /* Store chunk; freed in vm_free() after the VM has finished. */
    vm->prelude_chunk = prelude_chunk;
    return rc;
}

/* ================================================================== main run loop */

/* ================================================================== VM performance helpers */

/* Compiler branch-prediction hints */
#ifdef __GNUC__
#  define PRISM_LIKELY(x)    __builtin_expect(!!(x), 1)
#  define PRISM_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#else
#  define PRISM_LIKELY(x)    (x)
#  define PRISM_UNLIKELY(x)  (x)
#endif

/* Small-arg call stack buffer: avoids malloc for the common case of ≤16 args.
 * Saves one malloc + free per function call, which is ~80% of all calls. */
#define VM_CALL_STACK_BUF 16

/* ================================================================== vm_run (hot path) */

#ifdef __GNUC__
__attribute__((hot))
#endif
int vm_run(VM *vm, Chunk *chunk) {
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk      = chunk;
    frame->ip         = 0;
    frame->stack_base = 0;
    frame->env        = vm->globals;
    frame->root_env   = vm->globals;
    frame->owns_env   = 0;
    frame->owns_chunk = 0;
    frame->local_count = 0;
    memset(frame->locals, 0, sizeof(frame->locals));

/* ---- Computed-goto dispatch table (GCC / Clang only) ---- *
 * Replaces the central switch+dispatch with direct label jumps,
 * eliminating the shared branch at the top of every iteration.
 * Expected gain: 15–25 % on instruction-dense workloads.       */
#ifdef __GNUC__
    static const void *s_dt[OP_COUNT_];
    if (PRISM_UNLIKELY(!s_dt[OP_HALT])) {
        s_dt[OP_HALT]              = &&lbl_OP_HALT;
        s_dt[OP_PUSH_NULL]         = &&lbl_OP_PUSH_NULL;
        s_dt[OP_PUSH_TRUE]         = &&lbl_OP_PUSH_TRUE;
        s_dt[OP_PUSH_FALSE]        = &&lbl_OP_PUSH_FALSE;
        s_dt[OP_PUSH_UNKNOWN]      = &&lbl_OP_PUSH_UNKNOWN;
        s_dt[OP_PUSH_CONST]        = &&lbl_OP_PUSH_CONST;
        s_dt[OP_PUSH_INT_IMM]      = &&lbl_OP_PUSH_INT_IMM;
        s_dt[OP_POP]               = &&lbl_OP_POP;
        s_dt[OP_DUP]               = &&lbl_OP_DUP;
        s_dt[OP_POP_N]             = &&lbl_OP_POP_N;
        s_dt[OP_LOAD_NAME]         = &&lbl_OP_LOAD_NAME;
        s_dt[OP_STORE_NAME]        = &&lbl_OP_STORE_NAME;
        s_dt[OP_DEFINE_NAME]       = &&lbl_OP_DEFINE_NAME;
        s_dt[OP_DEFINE_CONST]      = &&lbl_OP_DEFINE_CONST;
        s_dt[OP_LOAD_LOCAL]        = &&lbl_OP_LOAD_LOCAL;
        s_dt[OP_STORE_LOCAL]       = &&lbl_OP_STORE_LOCAL;
        s_dt[OP_DEFINE_LOCAL]      = &&lbl_OP_DEFINE_LOCAL;
        s_dt[OP_INC_LOCAL]         = &&lbl_OP_INC_LOCAL;
        s_dt[OP_DEC_LOCAL]         = &&lbl_OP_DEC_LOCAL;
        s_dt[OP_ADD]               = &&lbl_OP_ADD;
        s_dt[OP_SUB]               = &&lbl_OP_SUB;
        s_dt[OP_MUL]               = &&lbl_OP_MUL;
        s_dt[OP_DIV]               = &&lbl_OP_DIV;
        s_dt[OP_MOD]               = &&lbl_OP_MOD;
        s_dt[OP_POW]               = &&lbl_OP_POW;
        s_dt[OP_NEG]               = &&lbl_OP_NEG;
        s_dt[OP_POS]               = &&lbl_OP_POS;
        s_dt[OP_IDIV]              = &&lbl_OP_IDIV;
        s_dt[OP_ADD_INT]           = &&lbl_OP_ADD_INT;
        s_dt[OP_SUB_INT]           = &&lbl_OP_SUB_INT;
        s_dt[OP_MUL_INT]           = &&lbl_OP_MUL_INT;
        s_dt[OP_DIV_INT]           = &&lbl_OP_DIV_INT;
        s_dt[OP_MOD_INT]           = &&lbl_OP_MOD_INT;
        s_dt[OP_NEG_INT]           = &&lbl_OP_NEG_INT;
        s_dt[OP_BIT_AND]           = &&lbl_OP_BIT_AND;
        s_dt[OP_BIT_OR]            = &&lbl_OP_BIT_OR;
        s_dt[OP_BIT_XOR]           = &&lbl_OP_BIT_XOR;
        s_dt[OP_BIT_NOT]           = &&lbl_OP_BIT_NOT;
        s_dt[OP_LSHIFT]            = &&lbl_OP_LSHIFT;
        s_dt[OP_RSHIFT]            = &&lbl_OP_RSHIFT;
        s_dt[OP_EQ]                = &&lbl_OP_EQ;
        s_dt[OP_NE]                = &&lbl_OP_NE;
        s_dt[OP_LT]                = &&lbl_OP_LT;
        s_dt[OP_LE]                = &&lbl_OP_LE;
        s_dt[OP_GT]                = &&lbl_OP_GT;
        s_dt[OP_GE]                = &&lbl_OP_GE;
        s_dt[OP_LT_INT]            = &&lbl_OP_LT_INT;
        s_dt[OP_LE_INT]            = &&lbl_OP_LE_INT;
        s_dt[OP_GT_INT]            = &&lbl_OP_GT_INT;
        s_dt[OP_GE_INT]            = &&lbl_OP_GE_INT;
        s_dt[OP_EQ_INT]            = &&lbl_OP_EQ_INT;
        s_dt[OP_NE_INT]            = &&lbl_OP_NE_INT;
        s_dt[OP_IN]                = &&lbl_OP_IN;
        s_dt[OP_NOT_IN]            = &&lbl_OP_NOT_IN;
        s_dt[OP_NOT]               = &&lbl_OP_NOT;
        s_dt[OP_JUMP]              = &&lbl_OP_JUMP;
        s_dt[OP_JUMP_IF_FALSE]     = &&lbl_OP_JUMP_IF_FALSE;
        s_dt[OP_JUMP_IF_TRUE]      = &&lbl_OP_JUMP_IF_TRUE;
        s_dt[OP_JUMP_IF_FALSE_PEEK]= &&lbl_OP_JUMP_IF_FALSE_PEEK;
        s_dt[OP_JUMP_IF_TRUE_PEEK] = &&lbl_OP_JUMP_IF_TRUE_PEEK;
        s_dt[OP_JUMP_WIDE]         = &&lbl_OP_JUMP_WIDE;
        s_dt[OP_JUMP_IF_FALSE_WIDE]= &&lbl_OP_JUMP_IF_FALSE_WIDE;
        s_dt[OP_JUMP_IF_TRUE_WIDE] = &&lbl_OP_JUMP_IF_TRUE_WIDE;
        s_dt[OP_PUSH_SCOPE]        = &&lbl_OP_PUSH_SCOPE;
        s_dt[OP_POP_SCOPE]         = &&lbl_OP_POP_SCOPE;
        s_dt[OP_MAKE_ARRAY]        = &&lbl_OP_MAKE_ARRAY;
        s_dt[OP_MAKE_DICT]         = &&lbl_OP_MAKE_DICT;
        s_dt[OP_MAKE_SET]          = &&lbl_OP_MAKE_SET;
        s_dt[OP_MAKE_TUPLE]        = &&lbl_OP_MAKE_TUPLE;
        s_dt[OP_MAKE_RANGE]        = &&lbl_OP_MAKE_RANGE;
        s_dt[OP_GET_INDEX]         = &&lbl_OP_GET_INDEX;
        s_dt[OP_SET_INDEX]         = &&lbl_OP_SET_INDEX;
        s_dt[OP_GET_ATTR]          = &&lbl_OP_GET_ATTR;
        s_dt[OP_SET_ATTR]          = &&lbl_OP_SET_ATTR;
        s_dt[OP_SAFE_GET_ATTR]     = &&lbl_OP_SAFE_GET_ATTR;
        s_dt[OP_SAFE_GET_INDEX]    = &&lbl_OP_SAFE_GET_INDEX;
        s_dt[OP_SLICE]             = &&lbl_OP_SLICE;
        s_dt[OP_MAKE_FUNCTION]     = &&lbl_OP_MAKE_FUNCTION;
        s_dt[OP_CALL]              = &&lbl_OP_CALL;
        s_dt[OP_CALL_METHOD]       = &&lbl_OP_CALL_METHOD;
        s_dt[OP_RETURN]            = &&lbl_OP_RETURN;
        s_dt[OP_RETURN_NULL]       = &&lbl_OP_RETURN_NULL;
        s_dt[OP_TAIL_CALL]         = &&lbl_OP_TAIL_CALL;
        s_dt[OP_GET_ITER]          = &&lbl_OP_GET_ITER;
        s_dt[OP_FOR_ITER]          = &&lbl_OP_FOR_ITER;
        s_dt[OP_BUILD_FSTRING]     = &&lbl_OP_BUILD_FSTRING;
        s_dt[OP_IMPORT]            = &&lbl_OP_IMPORT;
        s_dt[OP_LINK_STYLE]        = &&lbl_OP_LINK_STYLE;
        s_dt[OP_IS_TYPE]           = &&lbl_OP_IS_TYPE;
        s_dt[OP_MATCH_TYPE]        = &&lbl_OP_MATCH_TYPE;
        s_dt[OP_NULL_COAL]         = &&lbl_OP_NULL_COAL;
        s_dt[OP_PIPE]              = &&lbl_OP_PIPE;
        s_dt[OP_EXPECT]            = &&lbl_OP_EXPECT;
        s_dt[OP_TRY_BEGIN]         = &&lbl_OP_TRY_BEGIN;
        s_dt[OP_TRY_END]           = &&lbl_OP_TRY_END;
        s_dt[OP_THROW]             = &&lbl_OP_THROW;
    }
#  define DISPATCH() do { if (PRISM_UNLIKELY(vm->had_error)) goto done;            \
                          if (PRISM_UNLIKELY(vm->exception_handled)) {             \
                              vm->exception_handled = 0;                           \
                              frame = &vm->frames[vm->frame_count - 1]; }          \
                          if (PRISM_UNLIKELY((++vm->instructions_executed & 65535) == 0)) \
                              gc_collect_minor(vm->gc, frame->env, vm, frame->chunk);     \
                          uint8_t _op = frame->chunk->code[frame->ip++];           \
                          line = frame->chunk->lines[frame->ip - 1];               \
                          goto *s_dt[_op]; } while(0)
#else
#  define DISPATCH() break  /* fallback for non-GCC compilers */
#endif

#define READ_BYTE()    (frame->chunk->code[frame->ip++])
#define READ_U16()     (frame->ip += 2, \
                        (uint16_t)(frame->chunk->code[frame->ip-2] | \
                                  (frame->chunk->code[frame->ip-1] << 8)))
#define CURR_LINE()    (frame->chunk->lines[frame->ip - 1])
#define PUSH(v)        vm_push(vm, (v))
#define POP()          vm_pop(vm)
#define PEEK(n)        vm_peek(vm, (n))
#define CONST(i)       (frame->chunk->constants[i])

    int line = 0;   /* hoisted so DISPATCH() macro can assign it */
    /* Hoist call-arg stack buffers to function scope.
     * Declaring them inside case blocks lets GCC -O2 overlap their storage
     * with other case-block locals via lifetime merging — a computed-goto
     * aliasing hazard that causes intermittent stack corruption.  At function
     * scope their lifetimes are conservatively the whole function body. */
    Value _arg_buf[VM_CALL_STACK_BUF];
    Value _marg_buf[VM_CALL_STACK_BUF];
#ifdef __GNUC__
    /* Prime the computed-goto pump: read the very first opcode and jump
     * directly to its handler label.  From here on, DISPATCH() at the end
     * of each handler takes over — the while-loop overhead is never paid. */
    DISPATCH();
    /* NOTE: the while loop below is still needed for non-GCC compilers and
     * for code-analysis tools.  With GCC the pump above means we only reach
     * the while condition if DISPATCH() is undefined (shouldn't happen). */
#endif
    while (!vm->had_error) {
        uint8_t op = READ_BYTE();
        line = CURR_LINE();
        gc_set_alloc_site(frame->chunk->source_file, line);

        switch ((Opcode)op) {

        lbl_OP_HALT:
        case OP_HALT: goto done;

        lbl_OP_PUSH_NULL:
        case OP_PUSH_NULL:    PUSH(value_null());       DISPATCH();
        lbl_OP_PUSH_TRUE:
        case OP_PUSH_TRUE:    PUSH(value_bool(1));      DISPATCH();
        lbl_OP_PUSH_FALSE:
        case OP_PUSH_FALSE:   PUSH(value_bool(0));      DISPATCH();
        lbl_OP_PUSH_UNKNOWN:
        case OP_PUSH_UNKNOWN: PUSH(value_bool(-1));     DISPATCH();
        lbl_OP_PUSH_CONST:
        case OP_PUSH_CONST: {
            uint16_t idx = READ_U16();
            PUSH(value_retain(CONST(idx)));
            DISPATCH();
        }

        lbl_OP_POP:
        case OP_POP: {
            Value v = POP(); value_release(v); DISPATCH();
        }
        lbl_OP_DUP:
        case OP_DUP:
            PUSH(value_retain(PEEK(0)));
            DISPATCH();
        lbl_OP_POP_N:
        case OP_POP_N: {
            uint16_t n = READ_U16();
            for (int i = 0; i < n; i++) { Value v = POP(); value_release(v); }
            DISPATCH();
        }

        lbl_OP_LOAD_NAME:
        case OP_LOAD_NAME: {
            uint16_t idx = READ_U16();
            const char *name = AS_STR(CONST(idx));
            Value v = env_get(frame->env, name);
            if (PRISM_UNLIKELY(!v)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "name '%s' is not defined", name);
                vm_error(vm, msg, line);
                PUSH(value_null());
            } else {
                PUSH(v); /* env_get already retained; no additional retain needed */
            }
            DISPATCH();
        }

        lbl_OP_STORE_NAME:
        case OP_STORE_NAME: {
            uint16_t idx = READ_U16();
            const char *name = AS_STR(CONST(idx));
            Value top = POP();
            if (PRISM_UNLIKELY(!env_assign(frame->env, name, top))) {
                char msg[256];
                if (env_is_const(frame->env, name))
                    snprintf(msg, sizeof(msg),
                        "cannot assign to '%s': it was declared as a constant", name);
                else
                    snprintf(msg, sizeof(msg),
                        "variable '%s' is not declared; use 'let %s = ...' to declare it",
                        name, name);
                vm_error(vm, msg, line);
            }
            value_release(top);
            DISPATCH();
        }

        lbl_OP_DEFINE_NAME:
        case OP_DEFINE_NAME: {
            uint16_t idx = READ_U16();
            Value top = POP();
            env_set(frame->env, AS_STR(CONST(idx)), top, false);
            value_release(top);
            DISPATCH();
        }

        lbl_OP_DEFINE_CONST:
        case OP_DEFINE_CONST: {
            uint16_t idx = READ_U16();
            Value top = POP();
            env_set(frame->env, AS_STR(CONST(idx)), top, true);
            value_release(top);
            DISPATCH();
        }

        /* ---- arithmetic ---- */
        lbl_OP_ADD:
        case OP_ADD: { Value b = POP(), a = POP(); Value r = (PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT))?value_int(vm_fast_iadd(AS_INT(a),AS_INT(b))):value_add(a,b);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operands for +",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);DISPATCH(); }
        lbl_OP_SUB:
        case OP_SUB: { Value b = POP(), a = POP(); Value r = (PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT))?value_int(vm_fast_isub(AS_INT(a),AS_INT(b))):value_sub(a,b);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operands for -",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);DISPATCH(); }
        lbl_OP_MUL:
        case OP_MUL: { Value b = POP(), a = POP(); Value r = (PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT))?value_int(vm_fast_imul(AS_INT(a),AS_INT(b))):value_mul(a,b);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operands for *",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);DISPATCH(); }
        lbl_OP_DIV:
        case OP_DIV: { Value b = POP(), a = POP(); Value r = value_div(a,b);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operands for /",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);DISPATCH(); }
        lbl_OP_MOD:
        case OP_MOD: { Value b = POP(), a = POP(); Value r = value_mod(a,b);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operands for %",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);DISPATCH(); }
        lbl_OP_POW:
        case OP_POW: { Value b = POP(), a = POP(); Value r = value_pow(a,b);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operands for **",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);DISPATCH(); }
        lbl_OP_NEG:
        case OP_NEG: { Value a = POP(); Value r = value_neg(a);
            if(PRISM_UNLIKELY(IS_NULL(r))){vm_error(vm,"invalid operand for unary -",line);r=value_null();}
            value_release(a);PUSH(r);DISPATCH(); }
        lbl_OP_POS:
        case OP_POS: { /* unary + is a no-op for numbers */ DISPATCH(); }

        /* ---- bitwise ---- */
        lbl_OP_BIT_AND:
        case OP_BIT_AND: { Value b = POP(), a = POP();
            if(PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT)) { PUSH(value_int(vm_fast_iand(AS_INT(a),AS_INT(b)))); }
            else if(VAL_TYPE(a)==VAL_SET&&VAL_TYPE(b)==VAL_SET) {
                Value r=value_set_new();
                for(int i=0;i<AS_SET(a).len;i++) if(value_set_has(b,AS_SET(a).items[i])) value_set_add(r,AS_SET(a).items[i]);
                PUSH(r);
            } else { vm_error(vm,"invalid operands for &",line); PUSH(value_null()); }
            value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_BIT_OR:
        case OP_BIT_OR: { Value b = POP(), a = POP();
            if(PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT)) { PUSH(value_int(vm_fast_ior(AS_INT(a),AS_INT(b)))); }
            else if(VAL_TYPE(a)==VAL_SET&&VAL_TYPE(b)==VAL_SET) {
                Value r=value_set_new();
                for(int i=0;i<AS_SET(a).len;i++) value_set_add(r,AS_SET(a).items[i]);
                for(int i=0;i<AS_SET(b).len;i++) value_set_add(r,AS_SET(b).items[i]);
                PUSH(r);
            } else { vm_error(vm,"invalid operands for |",line); PUSH(value_null()); }
            value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_BIT_XOR:
        case OP_BIT_XOR: { Value b = POP(), a = POP();
            if(PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT)) { PUSH(value_int(vm_fast_ixor(AS_INT(a),AS_INT(b)))); }
            else if(VAL_TYPE(a)==VAL_SET&&VAL_TYPE(b)==VAL_SET) {
                Value r=value_set_new();
                for(int i=0;i<AS_SET(a).len;i++) if(!value_set_has(b,AS_SET(a).items[i])) value_set_add(r,AS_SET(a).items[i]);
                for(int i=0;i<AS_SET(b).len;i++) if(!value_set_has(a,AS_SET(b).items[i])) value_set_add(r,AS_SET(b).items[i]);
                PUSH(r);
            } else { vm_error(vm,"invalid operands for ^",line); PUSH(value_null()); }
            value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_BIT_NOT:
        case OP_BIT_NOT: { Value a = POP();
            if(PRISM_LIKELY(VAL_TYPE(a)==VAL_INT)) PUSH(value_int(~AS_INT(a)));
            else { vm_error(vm,"~ requires int",line); PUSH(value_null()); }
            value_release(a);DISPATCH(); }
        lbl_OP_LSHIFT:
        case OP_LSHIFT: { Value b = POP(), a = POP();
            if(PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT)) PUSH(value_int(AS_INT(a)<<AS_INT(b)));
            else { vm_error(vm,"<< requires int",line); PUSH(value_null()); }
            value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_RSHIFT:
        case OP_RSHIFT: { Value b = POP(), a = POP();
            if(PRISM_LIKELY(VAL_TYPE(a)==VAL_INT&&VAL_TYPE(b)==VAL_INT)) PUSH(value_int(AS_INT(a)>>AS_INT(b)));
            else { vm_error(vm,">> requires int",line); PUSH(value_null()); }
            value_release(a);value_release(b);DISPATCH(); }

        /* ---- comparison ---- */
        lbl_OP_EQ:
        case OP_EQ:  { Value b = POP(), a = POP(); PUSH(value_bool(value_equals(a,b)?1:0)); value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_NE:
        case OP_NE:  { Value b = POP(), a = POP(); PUSH(value_bool(value_equals(a,b)?0:1)); value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_LT:
        case OP_LT:  { Value b = POP(), a = POP(); PUSH(value_bool(value_compare(a,b)<0?1:0)); value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_LE:
        case OP_LE:  { Value b = POP(), a = POP(); PUSH(value_bool(value_compare(a,b)<=0?1:0)); value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_GT:
        case OP_GT:  { Value b = POP(), a = POP(); PUSH(value_bool(value_compare(a,b)>0?1:0)); value_release(a);value_release(b);DISPATCH(); }
        lbl_OP_GE:
        case OP_GE:  { Value b = POP(), a = POP(); PUSH(value_bool(value_compare(a,b)>=0?1:0)); value_release(a);value_release(b);DISPATCH(); }

        /* ---- membership ---- */
        lbl_OP_IN:
        case OP_IN: {
            Value container = POP(), item = POP();
            bool found = false;
            if(VAL_TYPE(container)==VAL_ARRAY||VAL_TYPE(container)==VAL_TUPLE) {
                ValueArray *arr2=(VAL_TYPE(container)==VAL_ARRAY)?&AS_ARRAY(container):&AS_TUPLE(container);
                for(int i=0;i<arr2->len;i++) if(value_equals(arr2->items[i],item)){found=true;break;}
            } else if(VAL_TYPE(container)==VAL_SET) {
                found = value_set_has(container,item);
            } else if(VAL_TYPE(container)==VAL_DICT) {
                for(int i=0;i<AS_DICT(container).len;i++) if(value_equals(AS_DICT(container).entries[i].key,item)){found=true;break;}
            } else if(VAL_TYPE(container)==VAL_STRING&&VAL_TYPE(item)==VAL_STRING) {
                found = strstr(AS_STR(container), AS_STR(item)) != NULL;
            }
            value_release(container);value_release(item);
            PUSH(value_bool(found?1:0));
            DISPATCH();
        }
        lbl_OP_NOT_IN:
        case OP_NOT_IN: {
            Value container = POP(), item = POP();
            bool found = false;
            if(VAL_TYPE(container)==VAL_ARRAY||VAL_TYPE(container)==VAL_TUPLE) {
                ValueArray *arr2=(VAL_TYPE(container)==VAL_ARRAY)?&AS_ARRAY(container):&AS_TUPLE(container);
                for(int i=0;i<arr2->len;i++) if(value_equals(arr2->items[i],item)){found=true;break;}
            } else if(VAL_TYPE(container)==VAL_SET) {
                found = value_set_has(container,item);
            } else if(VAL_TYPE(container)==VAL_STRING&&VAL_TYPE(item)==VAL_STRING) {
                found = strstr(AS_STR(container), AS_STR(item)) != NULL;
            }
            value_release(container);value_release(item);
            PUSH(value_bool(found?0:1));
            DISPATCH();
        }

        /* ---- logical not ---- */
        lbl_OP_NOT:
        case OP_NOT: { Value a = POP(); PUSH(value_bool(value_truthy(a)?0:1)); value_release(a); DISPATCH(); }

        /* ---- jumps ---- */
        lbl_OP_JUMP:
        case OP_JUMP: {
            int16_t off = (int16_t)READ_U16();
            /* frame->ip is now 3 bytes past the OP_JUMP byte. */
            if (off < 0 && vm->jit) {
                /* Backward jump = potential loop back-edge. */
                int jump_ip   = frame->ip - 3;
                int header_ip = frame->ip + (int)off;
                JitTrace *trace = jit_on_backward_jump(
                    vm->jit, vm, frame->env, jump_ip, header_ip, frame->chunk);
                if (trace) {
                    if (vm->jit_verbose) jit_dump_ir(trace);
                    int result = jit_execute(trace, vm, frame->env);
                    vm->jit->traces_executed++;
                    if (result == JIT_EXIT_LOOP_DONE) {
                        if (trace->exit_ip > 0) {
                            frame->ip = trace->exit_ip;
                        } else {
                            frame->ip += (int)off;
                        }
                        DISPATCH();
                    }
                    vm->jit->guard_exits++;
                }
            }
            frame->ip += (int)off;
            DISPATCH();
        }
        lbl_OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_FALSE: {
            int16_t off = (int16_t)READ_U16();
            Value v = POP();
            if (!value_truthy(v)) frame->ip += off;
            value_release(v);
            DISPATCH();
        }
        lbl_OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_TRUE: {
            int16_t off = (int16_t)READ_U16();
            Value v = POP();
            if (value_truthy(v)) frame->ip += off;
            value_release(v);
            DISPATCH();
        }
        lbl_OP_JUMP_IF_FALSE_PEEK:
        case OP_JUMP_IF_FALSE_PEEK: {
            int16_t off = (int16_t)READ_U16();
            if (!value_truthy(PEEK(0))) frame->ip += off;
            DISPATCH();
        }
        lbl_OP_JUMP_IF_TRUE_PEEK:
        case OP_JUMP_IF_TRUE_PEEK: {
            int16_t off = (int16_t)READ_U16();
            if (value_truthy(PEEK(0))) frame->ip += off;
            DISPATCH();
        }

        /* ---- scope ---- */
        lbl_OP_PUSH_SCOPE:
        case OP_PUSH_SCOPE:
            frame->env = env_new(frame->env);
            DISPATCH();
        lbl_OP_POP_SCOPE:
        case OP_POP_SCOPE: {
            Env *old = frame->env;
            frame->env = old->parent;
            env_free(old);
            DISPATCH();
        }

        /* ---- collections ---- */
        lbl_OP_MAKE_ARRAY:
        case OP_MAKE_ARRAY: {
            uint16_t n = READ_U16();
            Value arr2 = value_array_new();
            /* Use stack buffer for small arrays to avoid malloc */
            Value *_stk_buf[VM_CALL_STACK_BUF];
            Value *tmp = (n <= VM_CALL_STACK_BUF) ? _stk_buf : malloc(n * sizeof(Value));
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            for (int i = 0; i < n; i++) { value_array_push(arr2, tmp[i]); value_release(tmp[i]); }
            if (n > VM_CALL_STACK_BUF) free(tmp);
            PUSH(arr2);
            DISPATCH();
        }
        lbl_OP_MAKE_TUPLE:
        case OP_MAKE_TUPLE: {
            uint16_t n = READ_U16();
            Value *_stk_buf2[VM_CALL_STACK_BUF];
            Value *tmp = (n <= VM_CALL_STACK_BUF) ? _stk_buf2 : malloc(n * sizeof(Value));
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            Value t = value_tuple_new(tmp, n);
            for (int i=0;i<n;i++) value_release(tmp[i]);
            if (n > VM_CALL_STACK_BUF) free(tmp);
            PUSH(t);
            DISPATCH();
        }
        lbl_OP_MAKE_SET:
        case OP_MAKE_SET: {
            uint16_t n = READ_U16();
            Value *_stk_buf3[VM_CALL_STACK_BUF];
            Value *tmp = (n <= VM_CALL_STACK_BUF) ? _stk_buf3 : malloc(n * sizeof(Value));
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            Value s = value_set_new();
            for (int i = 0; i < n; i++) { value_set_add(s, tmp[i]); value_release(tmp[i]); }
            if (n > VM_CALL_STACK_BUF) free(tmp);
            PUSH(s);
            DISPATCH();
        }
        lbl_OP_MAKE_DICT:
        case OP_MAKE_DICT: {
            uint16_t n = READ_U16();
            Value *_stk_buf4[VM_CALL_STACK_BUF * 2];
            Value *tmp = (n*2 <= VM_CALL_STACK_BUF*2) ? _stk_buf4 : malloc(n * 2 * sizeof(Value));
            for (int i = n*2-1; i >= 0; i--) tmp[i] = POP();
            Value d = value_dict_new();
            for (int i = 0; i < n; i++) {
                value_dict_set(d, tmp[i*2], tmp[i*2+1]);
                value_release(tmp[i*2]); value_release(tmp[i*2+1]);
            }
            if (n*2 > VM_CALL_STACK_BUF*2) free(tmp);
            PUSH(d);
            DISPATCH();
        }

        /* ---- indexing ---- */
        lbl_OP_GET_INDEX:
        case OP_GET_INDEX: {
            Value idx = POP(), obj = POP();
            Value result = value_null();
            if (PRISM_LIKELY(VAL_TYPE(obj) == VAL_ARRAY)) {
                long long i = (VAL_TYPE(idx)==VAL_INT) ? AS_INT(idx) : 0;
                if (i < 0) i += AS_ARRAY(obj).len;
                if (PRISM_LIKELY(i >= 0 && i < AS_ARRAY(obj).len)) result = value_retain(AS_ARRAY(obj).items[i]);
                else { vm_error(vm,"array index out of range",line); }
            } else if (VAL_TYPE(obj) == VAL_TUPLE) {
                long long i = (VAL_TYPE(idx)==VAL_INT) ? AS_INT(idx) : 0;
                if (i < 0) i += AS_TUPLE(obj).len;
                if (i >= 0 && i < AS_TUPLE(obj).len) result = value_retain(AS_TUPLE(obj).items[i]);
                else { vm_error(vm,"tuple index out of range",line); }
            } else if (VAL_TYPE(obj) == VAL_STRING) {
                long long i = (VAL_TYPE(idx)==VAL_INT) ? AS_INT(idx) : 0;
                long long slen = (long long)strlen(AS_STR(obj));
                if (i < 0) i += slen;
                if (i >= 0 && i < slen) {
                    char ch[2] = {AS_STR(obj)[i], '\0'};
                    result = value_string(ch);
                } else { vm_error(vm,"string index out of range",line); }
            } else if (VAL_TYPE(obj) == VAL_DICT) {
                Value v = value_dict_get(obj, idx);
                result = v ? value_retain(v) : value_null();
            }
            value_release(obj); value_release(idx);
            PUSH(result);
            DISPATCH();
        }

        lbl_OP_SET_INDEX:
        case OP_SET_INDEX: {
            Value val = POP(), idx = POP(), obj = POP();
            if (PRISM_LIKELY(VAL_TYPE(obj) == VAL_ARRAY)) {
                long long i = (VAL_TYPE(idx)==VAL_INT) ? AS_INT(idx) : 0;
                if (i < 0) i += AS_ARRAY(obj).len;
                if (PRISM_LIKELY(i >= 0 && i < AS_ARRAY(obj).len)) {
                    value_release(AS_ARRAY(obj).items[i]);
                    AS_ARRAY(obj).items[i] = value_retain(val);
                } else vm_error(vm,"array index out of range",line);
            } else if (VAL_TYPE(obj) == VAL_DICT) {
                value_dict_set(obj, idx, val);
            } else vm_error(vm,"cannot index-assign this type",line);
            value_release(val); value_release(idx); value_release(obj);
            DISPATCH();
        }

        lbl_OP_GET_ATTR:
        case OP_GET_ATTR: {
            int instruction_ip = frame->ip - 1;
            uint16_t name_idx = READ_U16();
            const char *name = AS_STR(CONST(name_idx));
            Value obj = POP();
            if (PRISM_LIKELY(VAL_TYPE(obj) == VAL_DICT)) {
                InlineCache *cache = chunk_inline_cache(frame->chunk, instruction_ip);
                if (cache && (cache->opcode != OP_GET_ATTR || cache->name_idx != name_idx)) {
                    cache->opcode = OP_GET_ATTR;
                    cache->name_idx = name_idx;
                    cache->receiver_type = VAL_DICT;
                    cache->dict_index = -1;
                    cache->dict_version = 0;
                    cache->method_id = VM_METHOD_UNKNOWN;
                }
                Value v = cache
                    ? value_dict_get_cached(obj, CONST(name_idx), &cache->dict_index, &cache->dict_version)
                    : value_dict_get(obj, CONST(name_idx));
                PUSH(!IS_NULL(v) ? value_retain(v) : value_null());
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "cannot get attribute '%s' on %s",
                         name, value_type_name(VAL_TYPE(obj)));
                vm_error(vm, msg, line);
                PUSH(value_null());
            }
            value_release(obj);
            DISPATCH();
        }

        lbl_OP_SET_ATTR:
        case OP_SET_ATTR: {
            int instruction_ip = frame->ip - 1;
            uint16_t name_idx = READ_U16();
            Value val = POP(), obj = POP();
            if (VAL_TYPE(obj) == VAL_DICT) {
                InlineCache *cache = chunk_inline_cache(frame->chunk, instruction_ip);
                value_dict_set(obj, CONST(name_idx), val);
                if (cache) {
                    cache->opcode = OP_SET_ATTR;
                    cache->name_idx = name_idx;
                    cache->receiver_type = VAL_DICT;
                    cache->dict_index = value_dict_find_index(obj, CONST(name_idx));
                    cache->dict_version = AS_DICT(obj).version;
                    cache->method_id = VM_METHOD_UNKNOWN;
                }
            } else {
                vm_error(vm, "cannot set attribute on non-dict", line);
            }
            value_release(val); value_release(obj);
            DISPATCH();
        }

        /* ---- slicing ---- */
        lbl_OP_SLICE:
        case OP_SLICE: {
            Value step = POP();
            Value stop = POP();
            Value start = POP();
            Value obj = POP();
            Value result = vm_do_slice(vm, obj, start, stop, step, line);
            value_release(obj); value_release(start);
            value_release(stop); value_release(step);
            PUSH(result);
            DISPATCH();
        }

        /* ---- functions ---- */
        lbl_OP_MAKE_FUNCTION:
        case OP_MAKE_FUNCTION: {
            uint16_t idx = READ_U16();
            Value proto = CONST(idx);
            Value fn = value_function_copy(proto, frame->env);
            PUSH(fn);
            DISPATCH();
        }

        lbl_OP_CALL:
        case OP_CALL: {
            uint16_t argc = READ_U16();
            Value *args = (argc <= VM_CALL_STACK_BUF) ? _arg_buf : malloc(argc * sizeof(Value));
            for (int i = argc-1; i >= 0; i--) args[i] = POP();
            Value callee = POP();
            if (PRISM_LIKELY(VAL_TYPE(callee) == VAL_BUILTIN)) {
                Value result = AS_BUILTIN(callee).fn(args, argc);
                for (int i = 0; i < argc; i++) value_release(args[i]);
                if (argc > VM_CALL_STACK_BUF) free(args);
                value_release(callee);
                PUSH(result ? result : value_null());
            } else if (VAL_TYPE(callee) == VAL_FUNCTION && AS_FUNC(callee).chunk) {
                if (PRISM_UNLIKELY(vm->frame_count >= VM_FRAME_MAX)) {
                    vm_error(vm, "call frame overflow", line);
                    for (int i = 0; i < argc; i++) value_release(args[i]);
                    if (argc > VM_CALL_STACK_BUF) free(args);
                    value_release(callee); PUSH(value_null()); DISPATCH();
                }
                Env *fn_env = env_new(AS_FUNC(callee).closure ? AS_FUNC(callee).closure : vm->globals);
                for (int i = 0; i < AS_FUNC(callee).param_count; i++) {
                    Value arg = (i < argc) ? args[i] : value_null();
                    env_set(fn_env, AS_FUNC(callee).params[i].name, arg, false);
                }
                for (int i = 0; i < argc; i++) value_release(args[i]);
                if (argc > VM_CALL_STACK_BUF) free(args);
                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->func = value_retain(callee);
                new_frame->chunk = AS_FUNC(callee).chunk;
                new_frame->ip = 0; new_frame->stack_base = vm->stack_top;
                new_frame->env = fn_env; new_frame->root_env = fn_env;
                new_frame->owns_env = 1; new_frame->owns_chunk = 0;
                /* Pre-fill local slots for params so OP_LOAD_LOCAL/OP_STORE_LOCAL
                 * work without a hash-map lookup (the compiler emits locals for params). */
                {
                    int pc = AS_FUNC(callee).param_count;
                    int lc = pc < VM_LOCALS_MAX ? pc : VM_LOCALS_MAX;
                    for (int _pi = 0; _pi < lc; _pi++) {
                        const char *pname = AS_FUNC(callee).params[_pi].name;
                        new_frame->locals[_pi] = env_get(fn_env, pname);
                    }
                    if (lc < VM_LOCALS_MAX)
                        memset(&new_frame->locals[lc], 0,
                               (VM_LOCALS_MAX - lc) * sizeof(Value));
                    new_frame->local_count = lc;
                }
                value_release(callee);
                frame = new_frame; DISPATCH();
            } else {
                vm_error(vm, "value is not callable", line);
                for (int i = 0; i < argc; i++) value_release(args[i]);
                if (argc > VM_CALL_STACK_BUF) free(args);
                value_release(callee); PUSH(value_null());
            }
            DISPATCH();
        }

        lbl_OP_CALL_METHOD:
        case OP_CALL_METHOD: {
            int instruction_ip = frame->ip - 1;
            uint16_t name_idx = READ_U16();
            uint16_t argc     = READ_U16();
            const char *method_name = AS_STR(CONST(name_idx));
            Value *args = (argc <= VM_CALL_STACK_BUF) ? _marg_buf : malloc(argc * sizeof(Value));
            for (int i = argc-1; i >= 0; i--) args[i] = POP();
            Value obj = POP();
            InlineCache *cache = chunk_inline_cache(frame->chunk, instruction_ip);
            VmMethodId method_id = VM_METHOD_UNKNOWN;
            if (PRISM_LIKELY(cache && cache->opcode == OP_CALL_METHOD && cache->name_idx == name_idx && cache->receiver_type == VAL_TYPE(obj))) {
                method_id = cache->method_id;
            } else {
                method_id = vm_resolve_method_id(VAL_TYPE(obj), method_name);
                if (cache) { cache->opcode = OP_CALL_METHOD; cache->name_idx = name_idx; cache->receiver_type = VAL_TYPE(obj); cache->method_id = method_id; }
            }
            Value result = vm_dispatch_method_cached(vm, obj, method_name, method_id, args, argc, line);
            for (int i = 0; i < argc; i++) value_release(args[i]);
            if (argc > VM_CALL_STACK_BUF) free(args);
            value_release(obj);
            PUSH(result ? result : value_null());
            DISPATCH();
        }

        lbl_OP_RETURN:
        case OP_RETURN: {
            Value ret = value_retain(POP());
            while (vm->stack_top > frame->stack_base) { value_release(POP()); }
            for (int _li = 0; _li < frame->local_count; _li++) { value_release(frame->locals[_li]); frame->locals[_li] = 0; }
            frame->local_count = 0;
            vm_close_frame_env(frame);
            value_release(frame->func);
            vm->frame_count--;
            if (vm->frame_count == 0) { value_release(ret); goto done; }
            frame = &vm->frames[vm->frame_count - 1];
            PUSH(ret); value_release(ret);
            DISPATCH();
        }

        lbl_OP_RETURN_NULL:
        case OP_RETURN_NULL: {
            while (vm->stack_top > frame->stack_base) { value_release(POP()); }
            for (int _li = 0; _li < frame->local_count; _li++) { value_release(frame->locals[_li]); frame->locals[_li] = 0; }
            frame->local_count = 0;
            vm_close_frame_env(frame);
            value_release(frame->func);
            /* Defer module chunk cleanup instead of freeing immediately.
             * Functions created via OP_MAKE_FUNCTION borrow chunk pointers
             * from the module's constants pool; freeing here would leave
             * dangling pointers in any live function stored in globals. */
            if (frame->owns_chunk && frame->chunk) {
                vm_defer_module_chunk(vm, frame->chunk);
                frame->chunk = NULL;
            }
            vm->frame_count--;
            if (vm->frame_count == 0) goto done;
            frame = &vm->frames[vm->frame_count - 1];
            PUSH(value_null());
            DISPATCH();
        }

        lbl_OP_TAIL_CALL:
        case OP_TAIL_CALL: {
             /* Fallback to CALL for now */
             goto lbl_OP_CALL;
        }
        lbl_OP_GET_ITER:
        case OP_GET_ITER: {
            Value v = POP();
            Value iter = vm_get_iter(vm, v, line);
            value_release(v);
            PUSH(iter);
            DISPATCH();
        }

        lbl_OP_FOR_ITER:
        case OP_FOR_ITER: {
            int16_t jump_off = (int16_t)READ_U16();
            Value idx_v    = PEEK(0);
            Value iter_arr = PEEK(1);
            long long idx  = AS_INT(idx_v);
            long long len  = AS_ARRAY(iter_arr).len;
            if (PRISM_UNLIKELY(idx >= len)) {
                value_release(POP());
                value_release(POP());
                frame->ip += jump_off;
            } else {
                Value item = value_retain(AS_ARRAY(iter_arr).items[idx]);
                value_release(POP());
                PUSH(value_int(idx + 1));
                PUSH(item);
            }
            DISPATCH();
        }

        /* ---- f-string ---- */
        lbl_OP_BUILD_FSTRING:
        case OP_BUILD_FSTRING: {
            READ_U16(); /* count (currently always 1) */
            Value tmpl = POP();
            char *result = vm_process_fstring(vm, AS_STR(tmpl), frame->env);
            value_release(tmpl);
            PUSH(value_string_take(result));
            DISPATCH();
        }

        /* ---- import ---- */
        lbl_OP_IMPORT:
        case OP_IMPORT: {
            uint16_t idx = READ_U16();
            const char *path = AS_STR(CONST(idx));
            /* Resolve import: try as-is, +.pr, lib/<path>, lib/<path>.pr */
            FILE *f = NULL;
            char probe[512];
            if ((f = fopen(path, "r")) != NULL) {
                /* as-is */
            } else {
                snprintf(probe, sizeof(probe), "%s.pr", path);
                if ((f = fopen(probe, "r")) != NULL) {
                    /* with .pr */
                } else {
                    snprintf(probe, sizeof(probe), "lib/%s", path);
                    if ((f = fopen(probe, "r")) != NULL) {
                        /* lib/<path> */
                    } else {
                        snprintf(probe, sizeof(probe), "lib/%s.pr", path);
                        f = fopen(probe, "r");
                    }
                }
            }
            if (!f) {
                char msg[256];
                snprintf(msg, sizeof(msg), "cannot import '%s': file not found", path);
                vm_error(vm, msg, line);
                DISPATCH();
            }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f); rewind(f);
            char *src = malloc(sz + 1);
            { size_t _nr = fread(src, 1, (size_t)sz, f); (void)_nr; }
            src[sz] = '\0'; fclose(f);

            /* Parse the module source. */
            extern ASTNode *parser_parse_source(const char *src,
                                                char *errbuf, int errlen);
            char errbuf[512] = {0};
            ASTNode *prog = parser_parse_source(src, errbuf, sizeof(errbuf));
            free(src);
            if (!prog) {
                vm_error(vm, errbuf[0] ? errbuf : "import parse error", line);
                DISPATCH();
            }

            Chunk *mod_chunk = calloc(1, sizeof(Chunk));
            char comp_err[512] = {0};
            if (compile_module(prog, mod_chunk, comp_err, sizeof(comp_err)) != 0) {
                ast_node_free(prog);
                chunk_free(mod_chunk); free(mod_chunk);
                vm_error(vm, comp_err[0] ? comp_err : "import compile error", line);
                DISPATCH();
            }
            ast_node_free(prog);
            mod_chunk->source_file = path;

            if (PRISM_UNLIKELY(vm->frame_count >= VM_FRAME_MAX)) {
                chunk_free(mod_chunk); free(mod_chunk);
                vm_error(vm, "import: frame stack overflow", line);
                DISPATCH();
            }
            CallFrame *mod_frame = &vm->frames[vm->frame_count++];
            mod_frame->chunk       = mod_chunk;
            mod_frame->ip          = 0;
            mod_frame->stack_base  = vm->stack_top;
            mod_frame->env         = frame->env;
            mod_frame->root_env    = frame->env;
            mod_frame->owns_env    = 0;
            mod_frame->owns_chunk  = 1;
            mod_frame->local_count = 0;
            memset(mod_frame->locals, 0, sizeof(mod_frame->locals));
            frame = mod_frame;
            DISPATCH();
        }

        lbl_OP_IS_TYPE:
        case OP_IS_TYPE: {
            uint16_t idx = READ_U16();
            const char *tname = AS_STR(CONST(idx));
            Value val = vm_pop(vm);
            bool match_ok = false;
            if      (strcmp(tname, "int")    == 0) match_ok = (VAL_TYPE(val) == VAL_INT);
            else if (strcmp(tname, "float")  == 0) match_ok = (VAL_TYPE(val) == VAL_FLOAT);
            else if (strcmp(tname, "str")    == 0) match_ok = (VAL_TYPE(val) == VAL_STRING);
            else if (strcmp(tname, "bool")   == 0) match_ok = (VAL_TYPE(val) == VAL_BOOL);
            else if (strcmp(tname, "null")   == 0) match_ok = (VAL_TYPE(val) == VAL_NULL);
            else if (strcmp(tname, "array")  == 0) match_ok = (VAL_TYPE(val) == VAL_ARRAY);
            else if (strcmp(tname, "dict")   == 0) match_ok = (VAL_TYPE(val) == VAL_DICT);
            else if (strcmp(tname, "set")    == 0) match_ok = (VAL_TYPE(val) == VAL_SET);
            else if (strcmp(tname, "tuple")  == 0) match_ok = (VAL_TYPE(val) == VAL_TUPLE);
            else if (strcmp(tname, "func")   == 0) match_ok = (VAL_TYPE(val) == VAL_FUNCTION || VAL_TYPE(val) == VAL_BUILTIN);
            else if (strcmp(tname, "unknown") == 0) match_ok = (VAL_TYPE(val) == VAL_BOOL && AS_BOOL(val) == -1);
            else {
                match_ok = (strcmp(value_type_name(VAL_TYPE(val)), tname) == 0);
            }
            value_release(val);
            vm_push(vm, value_bool(match_ok ? 1 : 0));
            DISPATCH();
        }

        /* ── local variable slots (O(1), no hash) ──────────────── */
        lbl_OP_DEFINE_LOCAL:
        case OP_DEFINE_LOCAL: {
            uint16_t slot = READ_U16();
            Value v = POP();
            if (PRISM_LIKELY(slot < VM_LOCALS_MAX)) {
                value_release(frame->locals[slot]);
                frame->locals[slot] = v;
                if (slot >= (uint16_t)frame->local_count)
                    frame->local_count = (int)slot + 1;
            } else { value_release(v); }
            DISPATCH();
        }
        lbl_OP_STORE_LOCAL:
        case OP_STORE_LOCAL: {
            uint16_t slot = READ_U16();
            Value v = POP();
            if (PRISM_LIKELY(slot < VM_LOCALS_MAX)) {
                value_release(frame->locals[slot]);
                frame->locals[slot] = v;
            } else { value_release(v); }
            DISPATCH();
        }
        lbl_OP_LOAD_LOCAL:
        case OP_LOAD_LOCAL: {
            uint16_t slot = READ_U16();
            Value v = (PRISM_LIKELY(slot < VM_LOCALS_MAX) && !IS_NULL(frame->locals[slot]))
                       ? value_retain(frame->locals[slot])
                       : value_null();
            PUSH(v);
            DISPATCH();
        }
        lbl_OP_INC_LOCAL:
        case OP_INC_LOCAL: {
            /* Tagged-int: v=(n<<1)|1  →  v+2=((n+1)<<1)|1.  No untagging, no release. */
            uint16_t slot = READ_U16();
            Value v = frame->locals[slot];
            if (PRISM_LIKELY(slot < VM_LOCALS_MAX && IS_INT(v)))
                frame->locals[slot] = v + 2;
            DISPATCH();
        }
        lbl_OP_DEC_LOCAL:
        case OP_DEC_LOCAL: {
            uint16_t slot = READ_U16();
            Value v = frame->locals[slot];
            if (PRISM_LIKELY(slot < VM_LOCALS_MAX && IS_INT(v)))
                frame->locals[slot] = v - 2;
            DISPATCH();
        }

        /* ── small immediate integer ──────────────────────────── */
        lbl_OP_PUSH_INT_IMM:
        case OP_PUSH_INT_IMM: {
            int16_t imm = (int16_t)READ_U16();
            PUSH(value_int((long long)imm));
            DISPATCH();
        }

        /* ── specialized integer arithmetic (no type check) ──── */
        lbl_OP_ADD_INT:
        case OP_ADD_INT: {
            /* Tagged-int bit trick: a=(n<<1)|1, b=(m<<1)|1 → a+b-1=((n+m)<<1)|1
             * No untagging, no value_int(), no release needed (immediates). */
            Value b = POP(), a = POP();
            PUSH(a + b - 1);
            DISPATCH();
        }
        lbl_OP_SUB_INT:
        case OP_SUB_INT: {
            /* a-b+1 = ((n-m)<<1)|1 */
            Value b = POP(), a = POP();
            PUSH(a - b + 1);
            DISPATCH();
        }
        lbl_OP_MUL_INT:
        case OP_MUL_INT: {
            /* Multiplication cannot use the additive trick; must untag. */
            Value b = POP(), a = POP();
            PUSH(value_int(vm_fast_imul(AS_INT(a), AS_INT(b))));
            DISPATCH();
        }
        lbl_OP_DIV_INT:
        case OP_DIV_INT: {
            Value b = POP(), a = POP();
            if (PRISM_UNLIKELY(AS_INT(b) == 0)) { vm_error(vm, "division by zero", line); PUSH(value_null()); }
            else PUSH(value_int(AS_INT(a) / AS_INT(b)));
            value_release(a); value_release(b);
            DISPATCH();
        }
        lbl_OP_MOD_INT:
        case OP_MOD_INT: {
            Value b = POP(), a = POP();
            if (PRISM_UNLIKELY(AS_INT(b) == 0)) { vm_error(vm, "modulo by zero", line); PUSH(value_null()); }
            else PUSH(value_int(AS_INT(a) % AS_INT(b)));
            value_release(a); value_release(b);
            DISPATCH();
        }
        lbl_OP_NEG_INT:
        case OP_NEG_INT: {
            Value a = POP();
            PUSH(value_int(-AS_INT(a)));
            value_release(a);
            DISPATCH();
        }
        lbl_OP_IDIV:
        case OP_IDIV: {
            Value b = POP(), a = POP();
            if ((VAL_TYPE(a) == VAL_INT) && (VAL_TYPE(b) == VAL_INT)) {
                if (PRISM_UNLIKELY(AS_INT(b) == 0)) { vm_error(vm,"division by zero",line); PUSH(value_null()); }
                else {
                    long long q = AS_INT(a) / AS_INT(b);
                    if ((AS_INT(a) ^ AS_INT(b)) < 0 && q * AS_INT(b) != AS_INT(a)) q--;
                    PUSH(value_int(q));
                }
            } else if (VAL_TYPE(a) == VAL_FLOAT || VAL_TYPE(b) == VAL_FLOAT) {
                double fa = (VAL_TYPE(a)==VAL_INT)?(double)AS_INT(a):AS_FLOAT(a);
                double fb = (VAL_TYPE(b)==VAL_INT)?(double)AS_INT(b):AS_FLOAT(b);
                if (fb == 0.0) { vm_error(vm,"division by zero",line); PUSH(value_null()); }
                else PUSH(value_float(floor(fa/fb)));
            } else { vm_error(vm,"// requires numeric operands",line); PUSH(value_null()); }
            value_release(a); value_release(b);
            DISPATCH();
        }

        /* ── specialized integer comparisons ─────────────────── */
        lbl_OP_LT_INT:
        case OP_LT_INT: {
            /* Direct tagged-value comparison: (n<<1)|1 < (m<<1)|1 iff n < m. */
            Value b = POP(), a = POP();
            PUSH((intptr_t)a < (intptr_t)b ? VAL_SPEC_TRUE : VAL_SPEC_FALSE);
            DISPATCH();
        }
        lbl_OP_LE_INT:
        case OP_LE_INT: {
            Value b = POP(), a = POP();
            PUSH((intptr_t)a <= (intptr_t)b ? VAL_SPEC_TRUE : VAL_SPEC_FALSE);
            DISPATCH();
        }
        lbl_OP_GT_INT:
        case OP_GT_INT: {
            Value b = POP(), a = POP();
            PUSH((intptr_t)a > (intptr_t)b ? VAL_SPEC_TRUE : VAL_SPEC_FALSE);
            DISPATCH();
        }
        lbl_OP_GE_INT:
        case OP_GE_INT: {
            Value b = POP(), a = POP();
            PUSH((intptr_t)a >= (intptr_t)b ? VAL_SPEC_TRUE : VAL_SPEC_FALSE);
            DISPATCH();
        }
        lbl_OP_EQ_INT:
        case OP_EQ_INT: {
            /* Equal tagged ints have identical bit patterns. */
            Value b = POP(), a = POP();
            PUSH(a == b ? VAL_SPEC_TRUE : VAL_SPEC_FALSE);
            DISPATCH();
        }
        lbl_OP_NE_INT:
        case OP_NE_INT: {
            Value b = POP(), a = POP();
            PUSH(a != b ? VAL_SPEC_TRUE : VAL_SPEC_FALSE);
            DISPATCH();
        }

        /* ── wide jumps (32-bit offset) ───────────────────────── */
        lbl_OP_JUMP_WIDE:
        case OP_JUMP_WIDE: {
            uint16_t lo = READ_U16(), hi = READ_U16();
            int32_t off = (int32_t)((uint32_t)lo | ((uint32_t)hi << 16));
            frame->ip += (int)off;
            DISPATCH();
        }
        lbl_OP_JUMP_IF_FALSE_WIDE:
        case OP_JUMP_IF_FALSE_WIDE: {
            uint16_t lo = READ_U16(), hi = READ_U16();
            int32_t off = (int32_t)((uint32_t)lo | ((uint32_t)hi << 16));
            Value v = POP();
            if (!value_truthy(v)) frame->ip += (int)off;
            value_release(v); DISPATCH();
        }
        lbl_OP_JUMP_IF_TRUE_WIDE:
        case OP_JUMP_IF_TRUE_WIDE: {
            uint16_t lo = READ_U16(), hi = READ_U16();
            int32_t off = (int32_t)((uint32_t)lo | ((uint32_t)hi << 16));
            Value v = POP();
            if (value_truthy(v)) frame->ip += (int)off;
            value_release(v); DISPATCH();
        }

        /* ── range literal [start..stop step N] ──────────────── */
        lbl_OP_MAKE_RANGE:
        case OP_MAKE_RANGE: {
            uint16_t flags = READ_U16();
            Value step_v  = (flags & 1) ? POP() : value_null();
            Value stop_v = POP();
            Value start_v = POP();
            long long start = (VAL_TYPE(start_v) == VAL_INT) ? AS_INT(start_v) : 0;
            long long stop  = (VAL_TYPE(stop_v)  == VAL_INT) ? AS_INT(stop_v)  : 0;
            long long step2 = step_v ? (VAL_TYPE(step_v)==VAL_INT?AS_INT(step_v):1) : 1;
            if (step2 == 0) step2 = 1;
            if (flags & 2) {
                stop += (step2 > 0) ? 1 : -1;
            }
            Value arr2 = value_array_new();
            if (step2 > 0) {
                for (long long i = start; i < stop; i += step2)
                    value_array_push(arr2, value_int(i));
            } else {
                for (long long i = start; i > stop; i += step2)
                    value_array_push(arr2, value_int(i));
            }
            value_release(start_v); value_release(stop_v);
            if (step_v) value_release(step_v);
            PUSH(arr2);
            DISPATCH();
        }

        /* ── null coalescing ?? ───────────────────────────────── */
        lbl_OP_NULL_COAL:
        case OP_NULL_COAL: {
            Value rhs = POP(), lhs = POP();
            if (VAL_TYPE(lhs) == VAL_NULL) {
                value_release(lhs); PUSH(rhs);
            } else {
                value_release(rhs); PUSH(lhs);
            }
            DISPATCH();
        }

        /* ── safe attribute ?. ───────────────────────────────── */
        lbl_OP_SAFE_GET_ATTR:
        case OP_SAFE_GET_ATTR: {
            uint16_t name_idx = READ_U16();
            Value obj = POP();
            if (VAL_TYPE(obj) == VAL_NULL) {
                PUSH(value_null());
            } else if (VAL_TYPE(obj) == VAL_DICT) {
                Value v = value_dict_get(obj, CONST(name_idx));
                PUSH(!IS_NULL(v) ? value_retain(v) : value_null());
            } else {
                char msg2[256];
                snprintf(msg2,sizeof(msg2),"cannot safe-access '%s' on %s",
                         AS_STR(CONST(name_idx)), value_type_name(VAL_TYPE(obj)));
                vm_error(vm, msg2, line); PUSH(value_null());
            }
            value_release(obj); DISPATCH();
        }

        /* ── safe index ?[] ──────────────────────────────────── */
        lbl_OP_SAFE_GET_INDEX:
        case OP_SAFE_GET_INDEX: {
            Value idx = POP(), obj = POP();
            if (VAL_TYPE(obj) == VAL_NULL) {
                PUSH(value_null());
            } else if (VAL_TYPE(obj) == VAL_ARRAY) {
                long long i = (VAL_TYPE(idx)==VAL_INT)?AS_INT(idx):0;
                if (i < 0) i += AS_ARRAY(obj).len;
                PUSH((i>=0&&i<AS_ARRAY(obj).len)?value_retain(AS_ARRAY(obj).items[i]):value_null());
            } else if (VAL_TYPE(obj) == VAL_DICT) {
                Value v = value_dict_get(obj, idx);
                PUSH(!IS_NULL(v) ? value_retain(v) : value_null());
            } else {
                PUSH(value_null());
            }
            value_release(obj); value_release(idx); DISPATCH();
        }

        /* ── pipe |> operator ────────────────────────────────── */
        lbl_OP_PIPE:
        case OP_PIPE: {
            Value fn = POP();
            Value arg = POP();
            Value result = value_null();
            if (VAL_TYPE(fn) == VAL_BUILTIN) {
                result = AS_BUILTIN(fn).fn(&arg, 1);
            } else if (VAL_TYPE(fn) == VAL_FUNCTION && AS_FUNC(fn).chunk) {
                if (vm->frame_count < VM_FRAME_MAX) {
                    Env *fn_env = env_new(AS_FUNC(fn).closure ? AS_FUNC(fn).closure : vm->globals);
                    if (AS_FUNC(fn).param_count >= 1)
                        env_set(fn_env, AS_FUNC(fn).params[0].name, arg, false);
                    CallFrame *new_frame = &vm->frames[vm->frame_count++];
                    new_frame->chunk = AS_FUNC(fn).chunk;
                    new_frame->ip = 0;
                    new_frame->stack_base = vm->stack_top;
                    new_frame->env = fn_env;
                    new_frame->root_env = fn_env;
                    new_frame->owns_env   = 1;
                    new_frame->owns_chunk = 0;
                    new_frame->local_count = 0;
                    frame = new_frame;
                    value_release(arg); value_release(fn);
                    DISPATCH();
                }
            } else {
                vm_error(vm, "pipe: right-hand side is not callable", line);
            }
            value_release(arg); value_release(fn);
            PUSH(result ? result : value_null());
            DISPATCH();
        }

        /* ── PSS link stylesheet ─────────────────────────────── */
        lbl_OP_LINK_STYLE:
        case OP_LINK_STYLE: {
            uint16_t idx = READ_U16();
            const char *path = AS_STR(CONST(idx));
#ifdef HAVE_X11
            if (g_vm_xgui) {
                xgui_load_style(g_vm_xgui, path);
            } else {
                fprintf(stderr, "[pss] link '%s' (no active xgui window yet)\n", path);
            }
#else
            fprintf(stderr, "[pss] link '%s' (X11 not compiled in)\n", path);
#endif
            DISPATCH();
        }

        /* ── expect / assert ─────────────────────────────────── */
        lbl_OP_EXPECT:
        case OP_EXPECT: {
            uint16_t msg_idx = READ_U16();
            Value cond = POP();
            if (PRISM_UNLIKELY(!value_truthy(cond))) {
                const char *msg2 = (msg_idx < (uint16_t)frame->chunk->const_count &&
                                   VAL_TYPE(frame->chunk->constants[msg_idx]) == VAL_STRING)
                                   ? AS_STR(frame->chunk->constants[msg_idx])
                                   : "expectation failed";
                fprintf(stderr, "[prism] expect failed: %s (line %d)\n", msg2, line);
                value_release(cond);
                vm->had_error = 1;
                snprintf(vm->error_msg, sizeof(vm->error_msg),
                         "line %d: expect failed: %s", line, msg2);
                goto done;
            }
            value_release(cond);
            DISPATCH();
        }

        /* ── match type check ─────────────────────────────────── */
        lbl_OP_MATCH_TYPE:

        case OP_MATCH_TYPE: {
            uint16_t idx = READ_U16();
            const char *tname = AS_STR(CONST(idx));
            Value val = PEEK(0);
            bool match_ok2 = false;
            ValueType vt = VAL_TYPE(val);
            if      (strcmp(tname,"int")==0)    match_ok2 = (vt == VAL_INT);
            else if (strcmp(tname,"float")==0)  match_ok2 = (vt == VAL_FLOAT);
            else if (strcmp(tname,"str")==0)    match_ok2 = (vt == VAL_STRING);
            else if (strcmp(tname,"bool")==0)   match_ok2 = (vt == VAL_BOOL);
            else if (strcmp(tname,"null")==0)   match_ok2 = (vt == VAL_NULL);
            else if (strcmp(tname,"array")==0)  match_ok2 = (vt == VAL_ARRAY);
            else if (strcmp(tname,"dict")==0)   match_ok2 = (vt == VAL_DICT);
            else if (strcmp(tname,"set")==0)    match_ok2 = (vt == VAL_SET);
            else if (strcmp(tname,"tuple")==0)  match_ok2 = (vt == VAL_TUPLE);
            else match_ok2 = false;
            PUSH(value_bool(match_ok2 ? 1 : 0));
            DISPATCH();
        }

        /* ── try/catch/throw ─────────────────────────────────── */
        lbl_OP_TRY_BEGIN:
        case OP_TRY_BEGIN: {
            /* operand: signed 16-bit offset from current ip to catch handler */
            int16_t catch_off = (int16_t)READ_U16();
            if (PRISM_LIKELY(vm->try_depth < VM_TRY_MAX)) {
                VMTryFrame *tf = &vm->try_frames[vm->try_depth++];
                tf->handler_ip  = frame->ip + (int)catch_off;
                tf->frame_count = vm->frame_count;
                tf->stack_top   = vm->stack_top;
            }
            DISPATCH();
        }

        lbl_OP_TRY_END:
        case OP_TRY_END: {
            if (vm->try_depth > 0) vm->try_depth--;
            DISPATCH();
        }

        lbl_OP_THROW:
        case OP_THROW: {
            Value exc = POP();
            char *exc_str = value_to_string(exc);
            value_release(exc);
            vm_error(vm, exc_str ? exc_str : "unknown error", line);
            if (exc_str) free(exc_str);
            DISPATCH();
        }

        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unknown opcode %d at ip %d", op, frame->ip - 1);
            vm_error(vm, msg, line);
            DISPATCH();
        }
        }
    }
done:
    while (vm->frame_count > 0) {
        CallFrame *open = &vm->frames[vm->frame_count - 1];
        for (int _li = 0; _li < open->local_count; _li++) {
            if (open->locals[_li]) {
                value_release(open->locals[_li]);
                open->locals[_li] = 0;
            }
        }
        open->local_count = 0;
        vm_close_frame_env(open);
        value_release(open->func);
        vm->frame_count--;
    }
    return vm->had_error ? 1 : 0;

#undef READ_BYTE
#undef READ_U16
#undef CURR_LINE
#undef PUSH
#undef POP
#undef PEEK
#undef CONST
}
