#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "ast.h"
#include "value.h"
#include "gc.h"

/* Open-address hash map slot for Env.
 * Keys are interned canonical const char* pointers — comparison is pointer equality. */
typedef struct {
    const char *key;      /* interned pointer; NULL = empty slot */
    Value val;
    bool        is_const;
} EnvSlot;

typedef struct Env {
    int         refcount; /* reference count: env_retain/env_free manage lifetime */
    EnvSlot    *slots;
    int         cap;      /* power of 2 */
    int         size;     /* number of active entries */
    struct Env *parent;
} Env;

typedef struct {
    Env        *globals;
    int         had_error;
    char        error_msg[2048];
    Value return_val;
    bool        returning;
    bool        breaking;
    bool        continuing;
    PrismGC         *gc;
    const char *filename;   /* source file path, used for alloc-site tracking */
} Interpreter;

Env         *env_new(Env *parent);
Env         *env_retain(Env *env);  /* increment refcount, returns env */
void         env_free(Env *env);    /* decrement refcount, free when 0 (skips root) */
void         env_free_root(Env *env); /* explicitly free root (global) env */
Value env_get(Env *env, const char *name);
bool         env_set(Env *env, const char *name, Value val, bool is_const);
bool         env_assign(Env *env, const char *name, Value val);
bool         env_is_const(Env *env, const char *name);

Interpreter *interpreter_new(void);
void         interpreter_free(Interpreter *interp);
void         interpreter_run(Interpreter *interp, ASTNode *program);
Value interpreter_eval(Interpreter *interp, ASTNode *node, Env *env);

/* Public f-string processor: evaluate {expr} segments using the given env. */
char        *interpreter_process_fstring(Interpreter *interp, const char *tmpl, Env *env);

#endif
