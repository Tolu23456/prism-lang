#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "interpreter.h"
#include "parser.h"
#include "value.h"

/* ================================================================== Environment */

Env *env_new(Env *parent) {
    Env *e     = calloc(1, sizeof(Env));
    e->cap     = 16;
    e->keys    = malloc(e->cap * sizeof(char *));
    e->values  = malloc(e->cap * sizeof(Value *));
    e->is_const= calloc(e->cap, sizeof(bool));
    e->size    = 0;
    e->parent  = parent;
    return e;
}

void env_free(Env *env) {
    if (!env) return;
    for (int i = 0; i < env->size; i++) {
        free(env->keys[i]);
        value_release(env->values[i]);
    }
    free(env->keys);
    free(env->values);
    free(env->is_const);
    free(env);
}

Value *env_get(Env *env, const char *name) {
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->size; i++)
            if (strcmp(e->keys[i], name) == 0)
                return e->values[i];
    }
    return NULL;
}

bool env_set(Env *env, const char *name, Value *val, bool is_const) {
    /* define in current scope */
    for (int i = 0; i < env->size; i++) {
        if (strcmp(env->keys[i], name) == 0) {
            if (env->is_const[i]) return false; /* const violation */
            value_release(env->values[i]);
            env->values[i]  = value_retain(val);
            env->is_const[i]= is_const;
            return true;
        }
    }
    if (env->size >= env->cap) {
        env->cap   *= 2;
        env->keys    = realloc(env->keys,    env->cap * sizeof(char *));
        env->values  = realloc(env->values,  env->cap * sizeof(Value *));
        env->is_const= realloc(env->is_const,env->cap * sizeof(bool));
    }
    env->keys[env->size]     = strdup(name);
    env->values[env->size]   = value_retain(val);
    env->is_const[env->size] = is_const;
    env->size++;
    return true;
}

bool env_assign(Env *env, const char *name, Value *val) {
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->size; i++) {
            if (strcmp(e->keys[i], name) == 0) {
                if (e->is_const[i]) return false;
                value_release(e->values[i]);
                e->values[i] = value_retain(val);
                return true;
            }
        }
    }
    return false; /* not found */
}

bool env_is_const(Env *env, const char *name) {
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->size; i++)
            if (strcmp(e->keys[i], name) == 0)
                return e->is_const[i];
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
    if (v->type == VAL_INT)    return value_retain(v);
    if (v->type == VAL_FLOAT)  return value_int((long long)v->float_val);
    if (v->type == VAL_BOOL)   return value_int(v->bool_val == 1 ? 1 : 0);
    if (v->type == VAL_STRING) return value_int(strtoll(v->str_val, NULL, 10));
    return value_int(0);
}

static Value *builtin_float_fn(Value **args, int argc) {
    if (argc < 1) return value_float(0.0);
    Value *v = args[0];
    if (v->type == VAL_FLOAT)  return value_retain(v);
    if (v->type == VAL_INT)    return value_float((double)v->int_val);
    if (v->type == VAL_STRING) return value_float(strtod(v->str_val, NULL));
    return value_float(0.0);
}

static Value *builtin_str_fn(Value **args, int argc) {
    if (argc < 1) return value_string("");
    char *s = value_to_string(args[0]);
    Value *v = value_string_take(s);
    return v;
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
        }
    }
    return s;
}

static Value *builtin_type_fn(Value **args, int argc) {
    if (argc < 1) return value_string("null");
    return value_string(value_type_name(args[0]->type));
}

static void register_builtins(Interpreter *interp) {
    struct { const char *name; BuiltinFn fn; } builtins[] = {
        {"output",  builtin_output},
        {"input",   builtin_input},
        {"len",     builtin_len},
        {"bool",    builtin_bool_fn},
        {"int",     builtin_int_fn},
        {"float",   builtin_float_fn},
        {"str",     builtin_str_fn},
        {"set",     builtin_set_fn},
        {"type",    builtin_type_fn},
        {NULL, NULL}
    };
    for (int i = 0; builtins[i].name; i++) {
        Value *v = value_builtin(builtins[i].name, builtins[i].fn);
        env_set(interp->globals, builtins[i].name, v, false);
        value_release(v);
    }
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
        return value_int(found ? (long long)(found - s) : -1LL);
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
                /* not found: define in current scope */
                env_set(env, target->ident.name, val, false);
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
            if (!env_assign(env, target->ident.name, result))
                env_set(env, target->ident.name, result, false);
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

        Value **args = malloc(node->func_call.arg_count * sizeof(Value *));
        for (int i = 0; i < node->func_call.arg_count; i++) {
            args[i] = eval_node(interp, node->func_call.args[i], env);
            if (interp->had_error) {
                for (int j = 0; j < i; j++) value_release(args[j]);
                free(args); value_release(callee); return value_null();
            }
        }

        Value *result = value_null();

        if (callee->type == VAL_BUILTIN) {
            value_release(result);
            result = callee->builtin.fn(args, node->func_call.arg_count);
        } else if (callee->type == VAL_FUNCTION) {
            Env *fn_env = env_new(callee->func.closure);
            for (int i = 0; i < callee->func.param_count; i++) {
                Value *arg = (i < node->func_call.arg_count) ? args[i] : value_null();
                env_set(fn_env, callee->func.params[i].name, arg, false);
                if (i >= node->func_call.arg_count) value_release(arg);
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

        for (int i = 0; i < node->func_call.arg_count; i++) value_release(args[i]);
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
        switch (obj->type) {
            case VAL_STRING: result = string_method(interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
            case VAL_ARRAY:  result = array_method (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
            case VAL_DICT:   result = dict_method  (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
            case VAL_SET:    result = set_method   (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
            case VAL_TUPLE:  result = tuple_method (interp, obj, node->method_call.method, args, node->method_call.arg_count, node->line); break;
            default: {
                char emsg[64]; snprintf(emsg, sizeof(emsg), "type '%s' has no methods", value_type_name(obj->type));
                runtime_error(interp, emsg, node->line);
                result = value_null();
            }
        }

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
            if (found) { value_release(result); result = value_retain(found); }
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
        else {
            char emsg[64]; snprintf(emsg, sizeof(emsg), "unknown operator '%s'", op);
            runtime_error(interp, emsg, node->line);
        }

        value_release(left); value_release(right);
        return result;
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
    i->globals     = env_new(NULL);
    register_builtins(i);
    return i;
}

void interpreter_free(Interpreter *interp) {
    if (!interp) return;
    if (interp->return_val) value_release(interp->return_val);
    env_free(interp->globals);
    free(interp);
}

void interpreter_run(Interpreter *interp, ASTNode *program) {
    Value *result = eval_node(interp, program, interp->globals);
    value_release(result);
}

Value *interpreter_eval(Interpreter *interp, ASTNode *node, Env *env) {
    return eval_node(interp, node, env);
}
