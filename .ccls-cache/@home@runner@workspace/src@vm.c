#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vm.h"
#include "opcode.h"
#include "chunk.h"
#include "value.h"
#include "interpreter.h"

/* ================================================================== helpers */

static void vm_error(VM *vm, const char *msg, int line) {
    if (vm->had_error) return;
    vm->had_error = 1;
    snprintf(vm->error_msg, sizeof(vm->error_msg), "line %d: %s", line, msg);
}

static void vm_close_frame_env(CallFrame *frame) {
    while (frame->env && frame->env != frame->root_env) {
        Env *old = frame->env;
        frame->env = old->parent;
        old->parent = NULL;
        env_free(old);
    }
    if (frame->owns_env && frame->root_env) {
        Env *old = frame->root_env;
        old->parent = NULL;
        env_free(old);
    }
    frame->env = NULL;
    frame->root_env = NULL;
    frame->owns_env = 0;
}

/* Push / pop helpers (no bounds check in release, add in debug). */
static inline void vm_push(VM *vm, Value *v) {
    if (vm->stack_top >= VM_STACK_MAX) {
        vm->had_error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "stack overflow");
        return;
    }
    vm->stack[vm->stack_top++] = v;
}

static inline Value *vm_pop(VM *vm) {
    if (vm->stack_top <= 0) {
        vm->had_error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "stack underflow");
        return value_null();
    }
    return vm->stack[--vm->stack_top];
}

static inline Value *vm_peek(VM *vm, int offset) {
    return vm->stack[vm->stack_top - 1 - offset];
}

static inline long long vm_fast_iadd(long long a, long long b) {
#if defined(__x86_64__) || defined(__amd64__)
    __asm__("addq %1, %0" : "+r"(a) : "r"(b));
    return a;
#else
    return a + b;
#endif
}

static inline long long vm_fast_isub(long long a, long long b) {
#if defined(__x86_64__) || defined(__amd64__)
    __asm__("subq %1, %0" : "+r"(a) : "r"(b));
    return a;
#else
    return a - b;
#endif
}

static inline long long vm_fast_imul(long long a, long long b) {
#if defined(__x86_64__) || defined(__amd64__)
    __asm__("imulq %1, %0" : "+r"(a) : "r"(b));
    return a;
#else
    return a * b;
#endif
}

static inline long long vm_fast_iand(long long a, long long b) {
#if defined(__x86_64__) || defined(__amd64__)
    __asm__("andq %1, %0" : "+r"(a) : "r"(b));
    return a;
#else
    return a & b;
#endif
}

static inline long long vm_fast_ior(long long a, long long b) {
#if defined(__x86_64__) || defined(__amd64__)
    __asm__("orq %1, %0" : "+r"(a) : "r"(b));
    return a;
#else
    return a | b;
#endif
}

static inline long long vm_fast_ixor(long long a, long long b) {
#if defined(__x86_64__) || defined(__amd64__)
    __asm__("xorq %1, %0" : "+r"(a) : "r"(b));
    return a;
#else
    return a ^ b;
#endif
}

/* Read a uint16_t from bytecode (little-endian) and advance ip by 2. */
static inline uint16_t read16(CallFrame *f) {
    uint8_t lo = f->chunk->code[f->ip++];
    uint8_t hi = f->chunk->code[f->ip++];
    return (uint16_t)(lo | (hi << 8));
}

/* ================================================================== built-in output via VM */

static Value *vm_builtin_output(Value **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (args[i]->type == VAL_STRING) printf("%s", args[i]->str_val);
        else value_print(args[i]);
    }
    printf("\n");
    return value_null();
}

static Value *vm_builtin_input(Value **args, int argc) {
    if (argc > 0 && args[0]->type == VAL_STRING)
        printf("%s", args[0]->str_val);
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return value_string("");
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return value_string(buf);
}

static Value *vm_builtin_len(Value **args, int argc) {
    if (argc < 1) return value_int(0);
    Value *v = args[0];
    switch (v->type) {
        case VAL_STRING: return value_int((long long)strlen(v->str_val));
        case VAL_ARRAY:  return value_int(v->array.len);
        case VAL_TUPLE:  return value_int(v->tuple.len);
        case VAL_DICT:   return value_int(v->dict.len);
        case VAL_SET:    return value_int(v->set.len);
        default: return value_int(0);
    }
}

static Value *vm_builtin_bool_fn(Value **args, int argc) {
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

static Value *vm_builtin_int_fn(Value **args, int argc) {
    if (argc < 1) return value_int(0);
    Value *v = args[0];
    if (v->type == VAL_INT)    return value_retain(v);
    if (v->type == VAL_FLOAT)  return value_int((long long)v->float_val);
    if (v->type == VAL_BOOL)   return value_int(v->bool_val == 1 ? 1 : 0);
    if (v->type == VAL_STRING) return value_int(strtoll(v->str_val, NULL, 10));
    return value_int(0);
}

static Value *vm_builtin_float_fn(Value **args, int argc) {
    if (argc < 1) return value_float(0.0);
    Value *v = args[0];
    if (v->type == VAL_FLOAT)  return value_retain(v);
    if (v->type == VAL_INT)    return value_float((double)v->int_val);
    if (v->type == VAL_STRING) return value_float(strtod(v->str_val, NULL));
    return value_float(0.0);
}

static Value *vm_builtin_str_fn(Value **args, int argc) {
    if (argc < 1) return value_string("");
    return value_string_take(value_to_string(args[0]));
}

static Value *vm_builtin_set_fn(Value **args, int argc) {
    Value *s = value_set_new();
    if (argc >= 1) {
        Value *src = args[0];
        if (src->type == VAL_ARRAY || src->type == VAL_TUPLE) {
            ValueArray *arr = (src->type == VAL_ARRAY) ? &src->array : &src->tuple;
            for (int i = 0; i < arr->len; i++) value_set_add(s, arr->items[i]);
        } else if (src->type == VAL_SET) {
            for (int i = 0; i < src->set.len; i++) value_set_add(s, src->set.items[i]);
        }
    }
    return s;
}

static Value *vm_builtin_type_fn(Value **args, int argc) {
    if (argc < 1) return value_string("null");
    return value_string(value_type_name(args[0]->type));
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

static Value *vmbi_gui_window(Value **args, int argc) {
    if (argc >= 1 && args[0]->type == VAL_STRING)
        snprintf(g_vmgui.title, sizeof(g_vmgui.title), "%s", args[0]->str_val);
    if (argc >= 2 && args[1]->type == VAL_INT) g_vmgui.width  = (int)args[1]->int_val;
    if (argc >= 3 && args[2]->type == VAL_INT) g_vmgui.height = (int)args[2]->int_val;
    g_vmgui.active = 1;
    return value_null();
}

static Value *vmbi_gui_label(Value **args, int argc) {
    char buf[2048];
    const char *t = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "";
    snprintf(buf, sizeof(buf), "  <p class=\"gui-label\">%s</p>\n", t);
    vmgui_append(buf); return value_null();
}

static Value *vmbi_gui_button(Value **args, int argc) {
    char buf[2048];
    const char *t = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "Button";
    snprintf(buf, sizeof(buf), "  <button class=\"gui-btn\">%s</button>\n", t);
    vmgui_append(buf); return value_null();
}

static Value *vmbi_gui_input(Value **args, int argc) {
    char buf[2048];
    const char *ph = (argc >= 1 && args[0]->type == VAL_STRING) ? args[0]->str_val : "";
    snprintf(buf, sizeof(buf), "  <input class=\"gui-input\" type=\"text\" placeholder=\"%s\" />\n", ph);
    vmgui_append(buf); return value_null();
}

static Value *vmbi_gui_run(Value **args, int argc) {
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

/* ================================================================== register */

void vm_register_builtins(VM *vm) {
    struct { const char *name; BuiltinFn fn; } bi[] = {
        {"output",     vm_builtin_output},
        {"input",      vm_builtin_input},
        {"len",        vm_builtin_len},
        {"bool",       vm_builtin_bool_fn},
        {"int",        vm_builtin_int_fn},
        {"float",      vm_builtin_float_fn},
        {"str",        vm_builtin_str_fn},
        {"set",        vm_builtin_set_fn},
        {"type",       vm_builtin_type_fn},
        {"gui_window", vmbi_gui_window},
        {"gui_label",  vmbi_gui_label},
        {"gui_button", vmbi_gui_button},
        {"gui_input",  vmbi_gui_input},
        {"gui_run",    vmbi_gui_run},
        {NULL, NULL}
    };
    for (int i = 0; bi[i].name; i++) {
        Value *v = value_builtin(bi[i].name, bi[i].fn);
        env_set(vm->globals, bi[i].name, v, false);
        value_release(v);
    }
}

/* ================================================================== f-string processing */

static char *vm_process_fstring(VM *vm, const char *tmpl, Env *env);

/* Forward declarations for method dispatch */
static Value *vm_dispatch_method(VM *vm, Value *obj, const char *method,
                                  Value **args, int argc, int line);

/* ================================================================== VM new/free */

VM *vm_new(void) {
    VM *vm = calloc(1, sizeof(VM));
    vm->gc = gc_global();
    vm->globals = env_new(NULL);
    vm_register_builtins(vm);
    return vm;
}

void vm_free(VM *vm) {
    gc_collect_audit(vm->gc, vm->globals, vm, NULL);
    env_free(vm->globals);
    free(vm);
}

/* ================================================================== method dispatch */

static Value *vm_dispatch_method(VM *vm, Value *obj, const char *method,
                                  Value **args, int argc, int line) {
    /* Re-use the interpreter's method dispatch by creating a temporary interpreter. */
    /* Actually we implement the methods directly here to avoid coupling. */

    if (obj->type == VAL_STRING) {
        const char *s = obj->str_val;
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
        if (strcmp(method, "find") == 0 && argc >= 1 && args[0]->type == VAL_STRING) {
            const char *p = strstr(s, args[0]->str_val);
            return value_int(p ? (long long)(p - s) : -1);
        }
        if (strcmp(method, "replace") == 0 && argc >= 2
            && args[0]->type==VAL_STRING && args[1]->type==VAL_STRING) {
            const char *from = args[0]->str_val, *to = args[1]->str_val;
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
        if (strcmp(method, "startswith") == 0 && argc >= 1 && args[0]->type==VAL_STRING)
            return value_bool(strncmp(s, args[0]->str_val, strlen(args[0]->str_val))==0 ? 1 : 0);
        if (strcmp(method, "endswith") == 0 && argc >= 1 && args[0]->type==VAL_STRING) {
            size_t sl=strlen(s), pl=strlen(args[0]->str_val);
            return value_bool(sl>=pl && strcmp(s+sl-pl, args[0]->str_val)==0 ? 1 : 0);
        }
        if (strcmp(method, "split") == 0) {
            const char *delim = (argc>=1&&args[0]->type==VAL_STRING) ? args[0]->str_val : " ";
            Value *arr = value_array_new();
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
            (args[0]->type==VAL_ARRAY||args[0]->type==VAL_TUPLE)) {
            ValueArray *arr2 = args[0]->type==VAL_ARRAY ? &args[0]->array : &args[0]->tuple;
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

    if (obj->type == VAL_ARRAY) {
        if (strcmp(method, "add") == 0 && argc >= 1) {
            value_array_push(obj, args[0]); return value_null();
        }
        if (strcmp(method, "pop") == 0) {
            long long idx = (argc>=1&&args[0]->type==VAL_INT) ? args[0]->int_val : -1;
            return value_array_pop(obj, idx);
        }
        if (strcmp(method, "sort") == 0) { value_array_sort(obj); return value_null(); }
        if (strcmp(method, "insert") == 0 && argc >= 2 && args[0]->type==VAL_INT) {
            value_array_insert(obj, args[0]->int_val, args[1]); return value_null();
        }
        if (strcmp(method, "remove") == 0 && argc >= 1) {
            value_array_remove(obj, args[0]); return value_null();
        }
        if (strcmp(method, "extend") == 0 && argc >= 1) {
            value_array_extend(obj, args[0]); return value_null();
        }
        if (strcmp(method, "len") == 0) return value_int(obj->array.len);
    }

    if (obj->type == VAL_DICT) {
        if (strcmp(method, "keys") == 0) {
            Value *arr2 = value_array_new();
            for (int i=0;i<obj->dict.len;i++)
                value_array_push(arr2, value_retain(obj->dict.entries[i].key));
            return arr2;
        }
        if (strcmp(method, "values") == 0) {
            Value *arr2 = value_array_new();
            for (int i=0;i<obj->dict.len;i++)
                value_array_push(arr2, value_retain(obj->dict.entries[i].val));
            return arr2;
        }
        if (strcmp(method, "items") == 0) {
            Value *arr2 = value_array_new();
            for (int i=0;i<obj->dict.len;i++) {
                Value *items[2] = {obj->dict.entries[i].key, obj->dict.entries[i].val};
                value_array_push(arr2, value_tuple_new(items, 2));
            }
            return arr2;
        }
        if (strcmp(method, "erase") == 0) {
            for (int i=0;i<obj->dict.len;i++) {
                value_release(obj->dict.entries[i].key);
                value_release(obj->dict.entries[i].val);
            }
            obj->dict.len = 0;
            return value_null();
        }
        if (strcmp(method, "get") == 0 && argc >= 1) {
            Value *v = value_dict_get(obj, args[0]);
            if (!v) v = (argc>=2) ? value_retain(args[1]) : value_null();
            return v;
        }
    }

    if (obj->type == VAL_SET) {
        if (strcmp(method,"add")==0&&argc>=1) { value_set_add(obj,args[0]); return value_null(); }
        if (strcmp(method,"remove")==0&&argc>=1) { value_set_remove(obj,args[0]); return value_null(); }
        if (strcmp(method,"discard")==0&&argc>=1) { value_set_remove(obj,args[0]); return value_null(); }
        if (strcmp(method,"update")==0&&argc>=1&&(args[0]->type==VAL_SET||args[0]->type==VAL_ARRAY)) {
            ValueArray *src = args[0]->type==VAL_SET ? &args[0]->set : &args[0]->array;
            for (int i=0;i<src->len;i++) value_set_add(obj,src->items[i]);
            return value_null();
        }
    }

    if (obj->type == VAL_TUPLE) {
        if (strcmp(method,"count")==0&&argc>=1) {
            int cnt=0;
            for (int i=0;i<obj->tuple.len;i++)
                if(value_equals(obj->tuple.items[i],args[0])) cnt++;
            return value_int(cnt);
        }
        if (strcmp(method,"index")==0&&argc>=1) {
            for (int i=0;i<obj->tuple.len;i++)
                if(value_equals(obj->tuple.items[i],args[0])) return value_int(i);
            return value_int(-1);
        }
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "type '%s' has no method '%s'",
             value_type_name(obj->type), method);
    vm_error(vm, msg, line);
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
static Value *vm_get_iter(VM *vm, Value *v, int line) {
    Value *arr = value_array_new();
    switch (v->type) {
        case VAL_ARRAY:
            for (int i = 0; i < v->array.len; i++)
                value_array_push(arr, value_retain(v->array.items[i]));
            break;
        case VAL_TUPLE:
            for (int i = 0; i < v->tuple.len; i++)
                value_array_push(arr, value_retain(v->tuple.items[i]));
            break;
        case VAL_SET:
            for (int i = 0; i < v->set.len; i++)
                value_array_push(arr, value_retain(v->set.items[i]));
            break;
        case VAL_STRING: {
            const char *s = v->str_val;
            while (*s) {
                char ch[2] = {*s++, '\0'};
                value_array_push(arr, value_string(ch));
            }
            break;
        }
        case VAL_DICT:
            for (int i = 0; i < v->dict.len; i++)
                value_array_push(arr, value_retain(v->dict.entries[i].key));
            break;
        default:
            vm_error(vm, "value is not iterable", line);
            break;
    }
    return arr;
}

/* ================================================================== value_do_slice */

static Value *vm_do_slice(VM *vm, Value *obj, Value *start_v, Value *stop_v,
                           Value *step_v, int line) {
    long long step  = (step_v  && step_v->type  == VAL_INT) ? step_v->int_val  : 1;
    if (step == 0) { vm_error(vm, "slice step cannot be zero", line); return value_null(); }

    long long len = 0;
    if      (obj->type == VAL_STRING) len = (long long)strlen(obj->str_val);
    else if (obj->type == VAL_ARRAY)  len = obj->array.len;
    else if (obj->type == VAL_TUPLE)  len = obj->tuple.len;
    else { vm_error(vm, "type is not sliceable", line); return value_null(); }

    long long start = (start_v && start_v->type == VAL_INT) ? start_v->int_val
                      : (step > 0 ? 0 : len - 1);
    long long stop  = (stop_v  && stop_v->type  == VAL_INT) ? stop_v->int_val
                      : (step > 0 ? len : -len - 1);

    if (start < 0) start += len;
    if (stop  < 0) stop  += len;
    if (step > 0) {
        if (start < 0) start = 0;
        if (stop  > len) stop = len;
    } else {
        if (start >= len) start = len - 1;
    }

    if (obj->type == VAL_STRING) {
        const char *s = obj->str_val;
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

    ValueArray *src = (obj->type == VAL_ARRAY) ? &obj->array : &obj->tuple;
    Value *res = (obj->type == VAL_ARRAY) ? value_array_new() : NULL;
    Value **items_buf = NULL;
    int items_count = 0, items_cap = 8;
    if (obj->type == VAL_TUPLE) items_buf = malloc(items_cap * sizeof(Value*));

    for (long long i = start; step > 0 ? i < stop : i > stop; i += step) {
        if (i >= 0 && i < len) {
            if (obj->type == VAL_ARRAY) {
                value_array_push(res, value_retain(src->items[i]));
            } else {
                if (items_count >= items_cap) { items_cap*=2; items_buf=realloc(items_buf,items_cap*sizeof(Value*)); }
                items_buf[items_count++] = value_retain(src->items[i]);
            }
        }
    }
    if (obj->type == VAL_TUPLE) {
        res = value_tuple_new(items_buf, items_count);
        for (int i=0;i<items_count;i++) value_release(items_buf[i]);
        free(items_buf);
    }
    return res;
}

/* ================================================================== main run loop */

int vm_run(VM *vm, Chunk *chunk) {
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk      = chunk;
    frame->ip         = 0;
    frame->stack_base = 0;
    frame->env        = vm->globals;
    frame->root_env   = vm->globals;
    frame->owns_env   = 0;

#define READ_BYTE()    (frame->chunk->code[frame->ip++])
#define READ_U16()     (frame->ip += 2, \
                        (uint16_t)(frame->chunk->code[frame->ip-2] | \
                                  (frame->chunk->code[frame->ip-1] << 8)))
#define CURR_LINE()    (frame->chunk->lines[frame->ip - 1])
#define PUSH(v)        vm_push(vm, (v))
#define POP()          vm_pop(vm)
#define PEEK(n)        vm_peek(vm, (n))
#define CONST(i)       (frame->chunk->constants[i])

    while (!vm->had_error) {
        uint8_t op = READ_BYTE();
        int line   = CURR_LINE();

        switch ((Opcode)op) {

        case OP_HALT: goto done;

        case OP_PUSH_NULL:    PUSH(value_null());       break;
        case OP_PUSH_TRUE:    PUSH(value_bool(1));      break;
        case OP_PUSH_FALSE:   PUSH(value_bool(0));      break;
        case OP_PUSH_UNKNOWN: PUSH(value_bool(-1));     break;
        case OP_PUSH_CONST: {
            uint16_t idx = READ_U16();
            PUSH(value_retain(CONST(idx)));
            break;
        }

        case OP_POP: {
            Value *v = POP(); value_release(v); break;
        }
        case OP_DUP:
            PUSH(value_retain(PEEK(0)));
            break;
        case OP_POP_N: {
            uint16_t n = READ_U16();
            for (int i = 0; i < n; i++) { Value *v = POP(); value_release(v); }
            break;
        }

        case OP_LOAD_NAME: {
            uint16_t idx = READ_U16();
            const char *name = CONST(idx)->str_val;
            Value *v = env_get(frame->env, name);
            if (!v) {
                char msg[256];
                snprintf(msg, sizeof(msg), "name '%s' is not defined", name);
                vm_error(vm, msg, line);
                PUSH(value_null());
            } else {
                PUSH(value_retain(v));
            }
            break;
        }

        case OP_STORE_NAME: {
            uint16_t idx = READ_U16();
            const char *name = CONST(idx)->str_val;
            Value *top = POP();
            if (!env_assign(frame->env, name, top)) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "variable '%s' is not declared; use 'let %s = ...' to declare it",
                    name, name);
                vm_error(vm, msg, line);
            }
            value_release(top);
            break;
        }

        case OP_DEFINE_NAME: {
            uint16_t idx = READ_U16();
            Value *top = POP();
            env_set(frame->env, CONST(idx)->str_val, top, false);
            value_release(top);
            break;
        }

        case OP_DEFINE_CONST: {
            uint16_t idx = READ_U16();
            Value *top = POP();
            env_set(frame->env, CONST(idx)->str_val, top, true);
            value_release(top);
            break;
        }

        /* ---- arithmetic ---- */
        case OP_ADD: { Value *b=POP(), *a=POP(); Value *r=(a->type==VAL_INT&&b->type==VAL_INT)?value_int(vm_fast_iadd(a->int_val,b->int_val)):value_add(a,b);
            if(!r){vm_error(vm,"invalid operands for +",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);break; }
        case OP_SUB: { Value *b=POP(), *a=POP(); Value *r=(a->type==VAL_INT&&b->type==VAL_INT)?value_int(vm_fast_isub(a->int_val,b->int_val)):value_sub(a,b);
            if(!r){vm_error(vm,"invalid operands for -",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);break; }
        case OP_MUL: { Value *b=POP(), *a=POP(); Value *r=(a->type==VAL_INT&&b->type==VAL_INT)?value_int(vm_fast_imul(a->int_val,b->int_val)):value_mul(a,b);
            if(!r){vm_error(vm,"invalid operands for *",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);break; }
        case OP_DIV: { Value *b=POP(), *a=POP(); Value *r=value_div(a,b);
            if(!r){vm_error(vm,"invalid operands for /",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);break; }
        case OP_MOD: { Value *b=POP(), *a=POP(); Value *r=value_mod(a,b);
            if(!r){vm_error(vm,"invalid operands for %",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);break; }
        case OP_POW: { Value *b=POP(), *a=POP(); Value *r=value_pow(a,b);
            if(!r){vm_error(vm,"invalid operands for **",line);r=value_null();}
            value_release(a);value_release(b);PUSH(r);break; }
        case OP_NEG: { Value *a=POP(); Value *r=value_neg(a);
            if(!r){vm_error(vm,"invalid operand for unary -",line);r=value_null();}
            value_release(a);PUSH(r);break; }
        case OP_POS: { /* unary + is a no-op for numbers */ break; }

        /* ---- bitwise ---- */
        case OP_BIT_AND: { Value *b=POP(),*a=POP();
            if(a->type==VAL_INT&&b->type==VAL_INT) { PUSH(value_int(vm_fast_iand(a->int_val,b->int_val))); }
            else if(a->type==VAL_SET&&b->type==VAL_SET) {
                Value *r=value_set_new();
                for(int i=0;i<a->set.len;i++) if(value_set_has(b,a->set.items[i])) value_set_add(r,a->set.items[i]);
                PUSH(r);
            } else { vm_error(vm,"invalid operands for &",line); PUSH(value_null()); }
            value_release(a);value_release(b);break; }
        case OP_BIT_OR: { Value *b=POP(),*a=POP();
            if(a->type==VAL_INT&&b->type==VAL_INT) { PUSH(value_int(vm_fast_ior(a->int_val,b->int_val))); }
            else if(a->type==VAL_SET&&b->type==VAL_SET) {
                Value *r=value_set_new();
                for(int i=0;i<a->set.len;i++) value_set_add(r,a->set.items[i]);
                for(int i=0;i<b->set.len;i++) value_set_add(r,b->set.items[i]);
                PUSH(r);
            } else { vm_error(vm,"invalid operands for |",line); PUSH(value_null()); }
            value_release(a);value_release(b);break; }
        case OP_BIT_XOR: { Value *b=POP(),*a=POP();
            if(a->type==VAL_INT&&b->type==VAL_INT) { PUSH(value_int(vm_fast_ixor(a->int_val,b->int_val))); }
            else if(a->type==VAL_SET&&b->type==VAL_SET) {
                Value *r=value_set_new();
                for(int i=0;i<a->set.len;i++) if(!value_set_has(b,a->set.items[i])) value_set_add(r,a->set.items[i]);
                for(int i=0;i<b->set.len;i++) if(!value_set_has(a,b->set.items[i])) value_set_add(r,b->set.items[i]);
                PUSH(r);
            } else { vm_error(vm,"invalid operands for ^",line); PUSH(value_null()); }
            value_release(a);value_release(b);break; }
        case OP_BIT_NOT: { Value *a=POP();
            if(a->type==VAL_INT) PUSH(value_int(~a->int_val));
            else { vm_error(vm,"~ requires int",line); PUSH(value_null()); }
            value_release(a);break; }
        case OP_LSHIFT: { Value *b=POP(),*a=POP();
            if(a->type==VAL_INT&&b->type==VAL_INT) PUSH(value_int(a->int_val<<b->int_val));
            else { vm_error(vm,"<< requires int",line); PUSH(value_null()); }
            value_release(a);value_release(b);break; }
        case OP_RSHIFT: { Value *b=POP(),*a=POP();
            if(a->type==VAL_INT&&b->type==VAL_INT) PUSH(value_int(a->int_val>>b->int_val));
            else { vm_error(vm,">> requires int",line); PUSH(value_null()); }
            value_release(a);value_release(b);break; }

        /* ---- comparison ---- */
        case OP_EQ:  { Value *b=POP(),*a=POP(); PUSH(value_bool(value_equals(a,b)?1:0)); value_release(a);value_release(b);break; }
        case OP_NE:  { Value *b=POP(),*a=POP(); PUSH(value_bool(value_equals(a,b)?0:1)); value_release(a);value_release(b);break; }
        case OP_LT:  { Value *b=POP(),*a=POP(); PUSH(value_bool(value_compare(a,b)<0?1:0)); value_release(a);value_release(b);break; }
        case OP_LE:  { Value *b=POP(),*a=POP(); PUSH(value_bool(value_compare(a,b)<=0?1:0)); value_release(a);value_release(b);break; }
        case OP_GT:  { Value *b=POP(),*a=POP(); PUSH(value_bool(value_compare(a,b)>0?1:0)); value_release(a);value_release(b);break; }
        case OP_GE:  { Value *b=POP(),*a=POP(); PUSH(value_bool(value_compare(a,b)>=0?1:0)); value_release(a);value_release(b);break; }

        /* ---- membership ---- */
        case OP_IN: {
            Value *container=POP(), *item=POP();
            bool found = false;
            if(container->type==VAL_ARRAY||container->type==VAL_TUPLE) {
                ValueArray *arr2=(container->type==VAL_ARRAY)?&container->array:&container->tuple;
                for(int i=0;i<arr2->len;i++) if(value_equals(arr2->items[i],item)){found=true;break;}
            } else if(container->type==VAL_SET) {
                found = value_set_has(container,item);
            } else if(container->type==VAL_DICT) {
                for(int i=0;i<container->dict.len;i++) if(value_equals(container->dict.entries[i].key,item)){found=true;break;}
            } else if(container->type==VAL_STRING&&item->type==VAL_STRING) {
                found = strstr(container->str_val, item->str_val) != NULL;
            }
            value_release(container);value_release(item);
            PUSH(value_bool(found?1:0));
            break;
        }
        case OP_NOT_IN: {
            Value *container=POP(), *item=POP();
            bool found = false;
            if(container->type==VAL_ARRAY||container->type==VAL_TUPLE) {
                ValueArray *arr2=(container->type==VAL_ARRAY)?&container->array:&container->tuple;
                for(int i=0;i<arr2->len;i++) if(value_equals(arr2->items[i],item)){found=true;break;}
            } else if(container->type==VAL_SET) {
                found = value_set_has(container,item);
            } else if(container->type==VAL_STRING&&item->type==VAL_STRING) {
                found = strstr(container->str_val, item->str_val) != NULL;
            }
            value_release(container);value_release(item);
            PUSH(value_bool(found?0:1));
            break;
        }

        /* ---- logical not ---- */
        case OP_NOT: { Value *a=POP(); PUSH(value_bool(value_truthy(a)?0:1)); value_release(a); break; }

        /* ---- jumps ---- */
        case OP_JUMP: {
            int16_t off = (int16_t)READ_U16();
            frame->ip += off;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            int16_t off = (int16_t)READ_U16();
            Value *v = POP();
            if (!value_truthy(v)) frame->ip += off;
            value_release(v);
            break;
        }
        case OP_JUMP_IF_TRUE: {
            int16_t off = (int16_t)READ_U16();
            Value *v = POP();
            if (value_truthy(v)) frame->ip += off;
            value_release(v);
            break;
        }
        case OP_JUMP_IF_FALSE_PEEK: {
            int16_t off = (int16_t)READ_U16();
            if (!value_truthy(PEEK(0))) frame->ip += off;
            break;
        }
        case OP_JUMP_IF_TRUE_PEEK: {
            int16_t off = (int16_t)READ_U16();
            if (value_truthy(PEEK(0))) frame->ip += off;
            break;
        }

        /* ---- scope ---- */
        case OP_PUSH_SCOPE:
            frame->env = env_new(frame->env);
            break;
        case OP_POP_SCOPE: {
            Env *old = frame->env;
            frame->env = old->parent;
            old->parent = NULL; /* detach so free doesn't cascade */
            env_free(old);
            break;
        }

        /* ---- collections ---- */
        case OP_MAKE_ARRAY: {
            uint16_t n = READ_U16();
            Value *arr2 = value_array_new();
            /* Items are on stack in order [0] at bottom; pop in reverse. */
            Value **tmp = malloc(n * sizeof(Value*));
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            for (int i = 0; i < n; i++) { value_array_push(arr2, tmp[i]); value_release(tmp[i]); }
            free(tmp);
            PUSH(arr2);
            break;
        }
        case OP_MAKE_TUPLE: {
            uint16_t n = READ_U16();
            Value **tmp = malloc(n * sizeof(Value*));
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            Value *t = value_tuple_new(tmp, n);
            for (int i=0;i<n;i++) value_release(tmp[i]);
            free(tmp);
            PUSH(t);
            break;
        }
        case OP_MAKE_SET: {
            uint16_t n = READ_U16();
            Value **tmp = malloc(n * sizeof(Value*));
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            Value *s = value_set_new();
            for (int i = 0; i < n; i++) { value_set_add(s, tmp[i]); value_release(tmp[i]); }
            free(tmp);
            PUSH(s);
            break;
        }
        case OP_MAKE_DICT: {
            uint16_t n = READ_U16(); /* n = number of key-value pairs */
            Value **tmp = malloc(n * 2 * sizeof(Value*));
            for (int i = n*2-1; i >= 0; i--) tmp[i] = POP();
            Value *d = value_dict_new();
            for (int i = 0; i < n; i++) {
                value_dict_set(d, tmp[i*2], tmp[i*2+1]);
                value_release(tmp[i*2]); value_release(tmp[i*2+1]);
            }
            free(tmp);
            PUSH(d);
            break;
        }

        /* ---- indexing ---- */
        case OP_GET_INDEX: {
            Value *idx = POP(), *obj = POP();
            Value *result = value_null();
            if (obj->type == VAL_ARRAY) {
                long long i = (idx->type==VAL_INT) ? idx->int_val : 0;
                if (i < 0) i += obj->array.len;
                if (i >= 0 && i < obj->array.len) result = value_retain(obj->array.items[i]);
                else { vm_error(vm,"array index out of range",line); }
            } else if (obj->type == VAL_TUPLE) {
                long long i = (idx->type==VAL_INT) ? idx->int_val : 0;
                if (i < 0) i += obj->tuple.len;
                if (i >= 0 && i < obj->tuple.len) result = value_retain(obj->tuple.items[i]);
                else { vm_error(vm,"tuple index out of range",line); }
            } else if (obj->type == VAL_STRING) {
                long long i = (idx->type==VAL_INT) ? idx->int_val : 0;
                long long slen = (long long)strlen(obj->str_val);
                if (i < 0) i += slen;
                if (i >= 0 && i < slen) {
                    char ch[2] = {obj->str_val[i], '\0'};
                    result = value_string(ch);
                } else { vm_error(vm,"string index out of range",line); }
            } else if (obj->type == VAL_DICT) {
                Value *v = value_dict_get(obj, idx);
                result = v ? value_retain(v) : value_null();
            }
            value_release(obj); value_release(idx);
            PUSH(result);
            break;
        }

        case OP_SET_INDEX: {
            Value *val = POP(), *idx = POP(), *obj = POP();
            if (obj->type == VAL_ARRAY) {
                long long i = (idx->type==VAL_INT) ? idx->int_val : 0;
                if (i < 0) i += obj->array.len;
                if (i >= 0 && i < obj->array.len) {
                    value_release(obj->array.items[i]);
                    obj->array.items[i] = value_retain(val);
                } else vm_error(vm,"array index out of range",line);
            } else if (obj->type == VAL_DICT) {
                value_dict_set(obj, idx, val);
            } else vm_error(vm,"cannot index-assign this type",line);
            value_release(val); value_release(idx); value_release(obj);
            break;
        }

        case OP_GET_ATTR: {
            uint16_t name_idx = READ_U16();
            const char *name = CONST(name_idx)->str_val;
            Value *obj = POP();
            /* Dict member access: obj["key"] */
            if (obj->type == VAL_DICT) {
                Value *k = value_string(name);
                Value *v = value_dict_get(obj, k);
                value_release(k);
                PUSH(v ? value_retain(v) : value_null());
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "cannot get attribute '%s' on %s",
                         name, value_type_name(obj->type));
                vm_error(vm, msg, line);
                PUSH(value_null());
            }
            value_release(obj);
            break;
        }

        case OP_SET_ATTR: {
            uint16_t name_idx = READ_U16();
            const char *name = CONST(name_idx)->str_val;
            Value *val = POP(), *obj = POP();
            if (obj->type == VAL_DICT) {
                Value *k = value_string(name);
                value_dict_set(obj, k, val);
                value_release(k);
            } else {
                vm_error(vm, "cannot set attribute on non-dict", line);
            }
            value_release(val); value_release(obj);
            break;
        }

        /* ---- slicing ---- */
        case OP_SLICE: {
            Value *step  = POP();
            Value *stop  = POP();
            Value *start = POP();
            Value *obj   = POP();
            Value *result = vm_do_slice(vm, obj, start, stop, step, line);
            value_release(obj); value_release(start);
            value_release(stop); value_release(step);
            PUSH(result);
            break;
        }

        /* ---- functions ---- */
        case OP_MAKE_FUNCTION: {
            uint16_t idx = READ_U16();
            Value *proto = CONST(idx);
            /* Create a copy with the current env as closure. */
            Value *fn = value_function(
                proto->func.name,
                proto->func.params,
                proto->func.param_count,
                proto->func.body,
                frame->env
            );
            fn->func.chunk = proto->func.chunk;
            fn->func.owns_chunk = false;
            PUSH(fn);
            break;
        }

        case OP_CALL: {
            uint16_t argc = READ_U16();
            Value **args = malloc(argc * sizeof(Value*));
            for (int i = argc-1; i >= 0; i--) args[i] = POP();
            Value *callee = POP();
            if (callee->type == VAL_BUILTIN) {
                Value *result = callee->builtin.fn(args, argc);
                for (int i = 0; i < argc; i++) value_release(args[i]);
                free(args);
                value_release(callee);
                PUSH(result ? result : value_null());
            } else if (callee->type == VAL_FUNCTION && callee->func.chunk) {
                if (vm->frame_count >= VM_FRAME_MAX) {
                    vm_error(vm, "call frame overflow", line);
                    for (int i = 0; i < argc; i++) value_release(args[i]);
                    free(args);
                    value_release(callee);
                    PUSH(value_null());
                    break;
                }
                Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);
                for (int i = 0; i < callee->func.param_count; i++) {
                    Value *arg = (i < argc) ? args[i] : value_null();
                    env_set(fn_env, callee->func.params[i].name, arg, false);
                    if (i >= argc) value_release(arg);
                }
                for (int i = 0; i < argc; i++) value_release(args[i]);
                free(args);

                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->chunk = callee->func.chunk;
                new_frame->ip = 0;
                new_frame->stack_base = vm->stack_top;
                new_frame->env = fn_env;
                new_frame->root_env = fn_env;
                new_frame->owns_env = 1;
                value_release(callee);
                frame = new_frame;
            } else {
                vm_error(vm, "value is not callable", line);
                for (int i = 0; i < argc; i++) value_release(args[i]);
                free(args);
                value_release(callee);
                PUSH(value_null());
            }
            break;
        }

        case OP_CALL_METHOD: {
            uint16_t name_idx = READ_U16();
            uint16_t argc     = READ_U16();
            const char *method_name = CONST(name_idx)->str_val;
            Value **args = malloc(argc * sizeof(Value*));
            for (int i = argc-1; i >= 0; i--) args[i] = POP();
            Value *obj = POP();
            Value *result = vm_dispatch_method(vm, obj, method_name, args, argc, line);
            for (int i = 0; i < argc; i++) value_release(args[i]);
            free(args);
            value_release(obj);
            PUSH(result ? result : value_null());
            break;
        }

        case OP_RETURN: {
            Value *ret = POP();
            vm_close_frame_env(frame);
            vm->frame_count--;
            if (vm->frame_count == 0) {
                value_release(ret);
                goto done;
            }
            frame = &vm->frames[vm->frame_count - 1];
            PUSH(ret);
            break;
        }

        case OP_RETURN_NULL: {
            vm_close_frame_env(frame);
            vm->frame_count--;
            if (vm->frame_count == 0) goto done;
            frame = &vm->frames[vm->frame_count - 1];
            PUSH(value_null());
            break;
        }

        /* ---- iteration ---- */
        case OP_GET_ITER: {
            Value *v = POP();
            Value *iter = vm_get_iter(vm, v, line);
            value_release(v);
            PUSH(iter);
            break;
        }

        case OP_FOR_ITER: {
            int16_t jump_off = (int16_t)READ_U16();
            /* stack top: [iter_array, index] */
            Value *idx_v   = PEEK(0);
            Value *iter_arr = PEEK(1);
            long long idx  = idx_v->int_val;
            long long len  = iter_arr->array.len;
            if (idx >= len) {
                /* exhausted: pop iter_arr and index, jump forward */
                value_release(POP()); /* index */
                value_release(POP()); /* iter_arr */
                frame->ip += jump_off;
            } else {
                /* advance: update index, push current item */
                Value *item = value_retain(iter_arr->array.items[idx]);
                /* Replace index on stack with idx+1 */
                value_release(POP());
                PUSH(value_int(idx + 1));
                PUSH(item);
            }
            break;
        }

        /* ---- f-string ---- */
        case OP_BUILD_FSTRING: {
            READ_U16(); /* count (currently always 1) */
            Value *tmpl = POP();
            char *result = vm_process_fstring(vm, tmpl->str_val, frame->env);
            value_release(tmpl);
            PUSH(value_string_take(result));
            break;
        }

        /* ---- import ---- */
        case OP_IMPORT: {
            uint16_t idx = READ_U16();
            const char *path = CONST(idx)->str_val;
            FILE *f = fopen(path, "r");
            if (!f) {
                char msg[256];
                snprintf(msg, sizeof(msg), "cannot import '%s': file not found", path);
                vm_error(vm, msg, line);
                break;
            }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f); rewind(f);
            char *src = malloc(sz + 1);
            fread(src, 1, sz, f); src[sz] = '\0'; fclose(f);

            /* Parse via the lexer+parser and run with the tree-walking interpreter
             * in the current scope so imported names become available. */
            extern ASTNode *parser_parse_source(const char *src,
                                                char *errbuf, int errlen);
            char errbuf[512] = {0};
            ASTNode *prog = parser_parse_source(src, errbuf, sizeof(errbuf));
            free(src);
            if (!prog) {
                vm_error(vm, errbuf[0] ? errbuf : "import parse error", line);
                break;
            }
            Interpreter *imp = calloc(1, sizeof(Interpreter));
            imp->gc = vm->gc;
            imp->globals = frame->env;
            interpreter_eval(imp, prog, frame->env);
            if (imp->had_error && !vm->had_error)
                vm_error(vm, imp->error_msg, line);
            imp->globals = NULL; /* don't free shared env */
            free(imp);
            ast_node_free(prog);
            break;
        }

        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unknown opcode %d", op);
            vm_error(vm, msg, line);
            break;
        }
        }
    }

done:
    while (vm->frame_count > 0) {
        CallFrame *open = &vm->frames[vm->frame_count - 1];
        vm_close_frame_env(open);
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
