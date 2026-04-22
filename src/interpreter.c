#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interpreter.h"
#include "parser.h"
#include "value.h"
#include "builtins.h"
#include "gc.h"

#define NAMESET_CAP 256
typedef struct { const char *names[NAMESET_CAP]; int count; } NameSet;
static bool nameset_has(NameSet *s, const char *n) {
    for (int i = 0; i < s->count; i++) if (strcmp(s->names[i], n) == 0) return true;
    return false;
}
static void nameset_add(NameSet *s, const char *n) {
    if (nameset_has(s, n) || s->count >= NAMESET_CAP) return;
    s->names[s->count++] = n;
}

static void collect_used_names(ASTNode *n, NameSet *set, const char *alias) {
    if (!n) return;
    switch (n->type) {
        case NODE_IDENT:
            if (!alias) nameset_add(set, n->ident.name);
            break;
        case NODE_MEMBER:
            if (alias && n->member.obj && n->member.obj->type == NODE_IDENT
                && strcmp(n->member.obj->ident.name, alias) == 0) {
                nameset_add(set, n->member.name);
            } else collect_used_names(n->member.obj, set, alias);
            break;
        case NODE_PROGRAM: case NODE_BLOCK:
            for (int i = 0; i < n->block.count; i++) collect_used_names(n->block.stmts[i], set, alias);
            break;
        case NODE_VAR_DECL: collect_used_names(n->var_decl.init, set, alias); break;
        case NODE_FUNC_CALL:
            collect_used_names(n->func_call.callee, set, alias);
            for (int i = 0; i < n->func_call.arg_count; i++) collect_used_names(n->func_call.args[i], set, alias);
            break;
        case NODE_FUNC_DECL: collect_used_names(n->func_decl.body, set, alias); break;
        case NODE_RETURN: collect_used_names(n->ret.value, set, alias); break;
        case NODE_EXPR_STMT: collect_used_names(n->expr_stmt.expr, set, alias); break;
        case NODE_BINOP: collect_used_names(n->binop.left, set, alias); collect_used_names(n->binop.right, set, alias); break;
        case NODE_UNOP: collect_used_names(n->unop.operand, set, alias); break;
        case NODE_FOR_IN: collect_used_names(n->for_in.iter, set, alias); collect_used_names(n->for_in.body, set, alias); break;
        case NODE_RANGE: case NODE_OPTIMIZED_RANGE:
            collect_used_names(n->range_lit.start, set, alias);
            collect_used_names(n->range_lit.end, set, alias);
            collect_used_names(n->range_lit.step, set, alias);
            break;
        default: break;
    }
}

static inline const char *env_intern(const char *name) { return gc_intern_cstr(gc_global(), name); }
static inline unsigned env_ptr_slot(const char *key, int cap) {
    uintptr_t v = (uintptr_t)key; v = (v ^ (v >> 16)) * 0x45d9f3b5u;
    v = v ^ (v >> 16); return (unsigned)v & (unsigned)(cap - 1);
}

void env_rehash(Env *env, int new_cap) {
    EnvSlot *old = env->slots; int old_c = env->cap;
    env->slots = calloc((size_t)new_cap, sizeof(EnvSlot)); env->cap = new_cap; env->size = 0;
    for (int j = 0; j < old_c; j++) {
        if (!old[j].key) continue;
        unsigned h = env_ptr_slot(old[j].key, new_cap);
        while (env->slots[h].key) h = (h + 1) & (unsigned)(new_cap - 1);
        env->slots[h] = old[j]; env->size++;
    }
    free(old);
}

Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env)); e->refcount = 1; e->cap = 16;
    e->slots = calloc(16, sizeof(EnvSlot)); e->parent = parent;
    if (parent) parent->refcount++;
    return e;
}
Env *env_retain(Env *e) { if(e) e->refcount++; return e; }
void env_free(Env *env) {
    if (!env) return;
    if (--env->refcount > 0) return;
    for (int i = 0; i < env->cap; i++) if (env->slots[i].key) value_release(env->slots[i].val);
    free(env->slots); Env *p = env->parent; free(env); env_free(p);
}

Value *env_get(Env *env, const char *name) {
    if (!name) return NULL;
    const char *key = env_intern(name);
    for (Env *e = env; e; e = e->parent) {
        unsigned h = env_ptr_slot(key, e->cap);
        for (int i = 0; i < e->cap; i++) {
            unsigned idx = (h + i) & (e->cap - 1);
            if (!e->slots[idx].key) break;
            if (e->slots[idx].key == key) return &e->slots[idx].val;
        }
    }
    return NULL;
}

bool env_set(Env *env, const char *name, Value val, bool is_const) {
    if (!name) return false;
    const char *key = env_intern(name);
    unsigned h = env_ptr_slot(key, env->cap);
    for (int i = 0; i < env->cap; i++) {
        unsigned idx = (h + i) & (env->cap - 1);
        if (!env->slots[idx].key) break;
        if (env->slots[idx].key == key) {
            if (env->slots[idx].is_const) return false;
            value_release(env->slots[idx].val); env->slots[idx].val = value_retain(val);
            env->slots[idx].is_const = is_const; return true;
        }
    }
    if (env->size * 4 >= env->cap * 3) { env_rehash(env, env->cap * 2); h = env_ptr_slot(key, env->cap); }
    unsigned idx = h; while (env->slots[idx].key) idx = (idx + 1) & (env->cap - 1);
    env->slots[idx].key = key; env->slots[idx].val = value_retain(val);
    env->slots[idx].is_const = is_const; env->size++; return true;
}

bool env_assign(Env *env, const char *name, Value val) {
    Value *v = env_get(env, name); if (v) { value_release(*v); *v = value_retain(val); return true; } return false;
}
bool env_is_const(Env *env, const char *name) { (void)env;(void)name; return false; }

Interpreter *interpreter_new(void) {
    Interpreter *i = calloc(1, sizeof(Interpreter));
    i->gc = gc_global(); i->globals = env_new(NULL);
    prism_register_stdlib(i->globals); return i;
}

static Value eval_node(Interpreter *i, ASTNode *n, Env *e) {
    if (!n || i->had_error) return TO_NULL();
    switch (n->type) {
        case NODE_INT_LIT: return TO_INT(n->int_lit.value);
        case NODE_STRING_LIT: return value_string(n->string_lit.value);
        case NODE_IDENT: {
            Value *v = env_get(e, n->ident.name);
            if (!v) { i->had_error = 1; printf("Runtime error: name '%s' is not defined\n", n->ident.name); return TO_NULL(); }
            return value_retain(*v);
        }
        case NODE_VAR_DECL: {
            Value val = eval_node(i, n->var_decl.init, e);
            env_set(e, n->var_decl.name, val, n->var_decl.is_const);
            value_release(val); return TO_NULL();
        }
        case NODE_FUNC_CALL: {
            Value callee = eval_node(i, n->func_call.callee, e);
            int argc = n->func_call.arg_count;
            if (VAL_TYPE(callee) == VAL_BUILTIN) {
                Value args[64];
                for (int j = 0; j < argc; j++) args[j] = eval_node(i, n->func_call.args[j], e);
                Value res = AS_BUILTIN(callee).fn(args, argc);
                for (int j = 0; j < argc; j++) value_release(args[j]);
                value_release(callee); return res;
            }
            if (VAL_TYPE(callee) == VAL_FUNCTION) {
                ValueStruct *fs = AS_PTR(callee);
                Env *call_env = env_new(fs->_func_.closure ? fs->_func_.closure : i->globals);
                int pc = fs->_func_.param_count;
                for (int j = 0; j < pc && j < argc; j++) {
                    Value a = eval_node(i, n->func_call.args[j], e);
                    env_set(call_env, fs->_func_.params[j].name, a, false);
                    value_release(a);
                }
                value_release(eval_node(i, fs->_func_.body, call_env));
                Value res = i->return_val;
                i->return_val = TO_NULL();
                i->returning = false;
                env_free(call_env); value_release(callee);
                return res;
            }
            i->had_error = 1; printf("Runtime error: value is not callable\n");
            value_release(callee); return TO_NULL();
        }
        case NODE_FUNC_DECL: {
            Value fn = value_function(n->func_decl.name, n->func_decl.params, n->func_decl.param_count, n->func_decl.body, NULL);
            env_set(e, n->func_decl.name, fn, true);
            value_release(fn); return TO_NULL();
        }
        case NODE_RETURN: {
            Value v = n->ret.value ? eval_node(i, n->ret.value, e) : TO_NULL();
            value_release(i->return_val);
            i->return_val = v; i->returning = true;
            return TO_NULL();
        }
        case NODE_MEMBER: {
            Value obj = eval_node(i, n->member.obj, e);
            if (VAL_TYPE(obj) == VAL_DICT) {
                Value k = value_string(n->member.name);
                Value v = value_dict_get(obj, k);
                value_release(k); value_release(obj);
                return v;
            }
            i->had_error = 1; printf("Runtime error: '.%s' on non-namespace value\n", n->member.name);
            value_release(obj); return TO_NULL();
        }
        case NODE_IMPORT: {
            char path[512];
            const char *modname = n->import_stmt.path;
            FILE *f = NULL;
            const char *tries[] = { "lib/%s.pr", "%s.pr", "%s", NULL };
            for (int k = 0; tries[k]; k++) {
                snprintf(path, sizeof(path), tries[k], modname);
                f = fopen(path, "r"); if (f) break;
            }
            if (!f) { i->had_error = 1; printf("Import error: module '%s' not found\n", modname); return TO_NULL(); }
            fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
            char *src = malloc((size_t)sz + 1);
            size_t nr = fread(src, 1, (size_t)sz, f); (void)nr; src[sz] = 0; fclose(f);
            char errbuf[512] = {0};
            ASTNode *mod = parser_parse_source(src, errbuf, sizeof errbuf);
            free(src);
            if (!mod) { i->had_error = 1; printf("Import error in '%s': %s\n", modname, errbuf); return TO_NULL(); }
            NameSet needed = {{0},0};
            collect_used_names(i->program, &needed, n->import_stmt.alias);
            Value ns = TO_NULL();
            if (n->import_stmt.alias) ns = value_dict_new();
            for (int j = 0; j < mod->block.count; j++) {
                ASTNode *st = mod->block.stmts[j];
                const char *name = NULL;
                if (st->type == NODE_FUNC_DECL) name = st->func_decl.name;
                else if (st->type == NODE_VAR_DECL) name = st->var_decl.name;
                else continue;
                if (!name || !nameset_has(&needed, name)) continue;
                Value v;
                if (st->type == NODE_FUNC_DECL) {
                    v = value_function(st->func_decl.name, st->func_decl.params, st->func_decl.param_count, st->func_decl.body, NULL);
                } else {
                    v = st->var_decl.init ? eval_node(i, st->var_decl.init, i->globals) : TO_NULL();
                }
                if (n->import_stmt.alias) {
                    Value k = value_string(name);
                    value_dict_set(ns, k, v);
                    value_release(k);
                } else {
                    env_set(e, name, v, true);
                }
                value_release(v);
            }
            if (n->import_stmt.alias) {
                env_set(e, n->import_stmt.alias, ns, true);
                value_release(ns);
            }
            return TO_NULL();
        }
        case NODE_FOR_IN: {
            Value iter = eval_node(i, n->for_in.iter, e);
            Env *loop_e = env_new(e);
            if (VAL_TYPE(iter) == VAL_RANGE) {
                long long start = AS_RANGE(iter).start, stop = AS_RANGE(iter).stop, step = AS_RANGE(iter).step;
                if (step > 0) { for (long long v = start; v < stop; v += step) { env_set(loop_e, n->for_in.var, TO_INT(v), false); value_release(eval_node(i, n->for_in.body, loop_e)); } }
                else { for (long long v = start; v > stop; v += step) { env_set(loop_e, n->for_in.var, TO_INT(v), false); value_release(eval_node(i, n->for_in.body, loop_e)); } }
            } else if (VAL_TYPE(iter) == VAL_ARRAY) {
                for (int j = 0; j < AS_ARRAY(iter).len; j++) {
                    env_set(loop_e, n->for_in.var, value_retain(AS_ARRAY(iter).items[j]), false);
                    value_release(eval_node(i, n->for_in.body, loop_e));
                }
            }
            env_free(loop_e); value_release(iter); return TO_NULL();
        }
        case NODE_RANGE: {
            Value start = eval_node(i, n->range_lit.start, e); Value stop = eval_node(i, n->range_lit.end, e);
            long long st = IS_INT(start)?AS_INT(start):0, sp = IS_INT(stop)?AS_INT(stop):0;
            Value arr = value_array_new();
            for (long long v=st; v<sp; v++) value_array_push(arr, TO_INT(v));
            value_release(start); value_release(stop); return arr;
        }
        case NODE_OPTIMIZED_RANGE: {
            Value start = eval_node(i, n->range_lit.start, e); Value stop = eval_node(i, n->range_lit.end, e);
            long long st = IS_INT(start)?AS_INT(start):0, sp = IS_INT(stop)?AS_INT(stop):0, step = 1;
            if (n->range_lit.step) { Value sv = eval_node(i, n->range_lit.step, e); step = IS_INT(sv)?AS_INT(sv):1; value_release(sv); }
            value_release(start); value_release(stop); return value_range_new(st, sp, step);
        }
        case NODE_PROGRAM:
        case NODE_BLOCK: { Value last = TO_NULL(); for (int j = 0; j < n->block.count; j++) { value_release(last); last = eval_node(i, n->block.stmts[j], e); if (i->returning || i->had_error) break; } return last; }
        case NODE_EXPR_STMT: return eval_node(i, n->expr_stmt.expr, e);
        default: return TO_NULL();
    }
}
Value interpreter_eval(Interpreter *i, ASTNode *n, Env *e) { return eval_node(i, n, e); }
void interpreter_run(Interpreter *i, ASTNode *n) { i->program = n; value_release(eval_node(i, n, i->globals)); }
void interpreter_free(Interpreter *i) { env_free(i->globals); free(i); }
char *interpreter_process_fstring(Interpreter *i, const char *t, Env *e) { (void)i;(void)e; return strdup(t); }
