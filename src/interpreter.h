#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "ast.h"
#include "value.h"
#include "gc.h"

typedef struct Env {
    char   **keys;
    Value  **values;
    bool    *is_const;
    int      size;
    int      cap;
    struct Env *parent;
} Env;

typedef struct {
    Env        *globals;
    int         had_error;
    char        error_msg[512];
    Value      *return_val;
    bool        returning;
    bool        breaking;
    bool        continuing;
    GC         *gc;
    const char *filename;   /* source file path, used for alloc-site tracking */
} Interpreter;

Env         *env_new(Env *parent);
void         env_free(Env *env);
Value       *env_get(Env *env, const char *name);
bool         env_set(Env *env, const char *name, Value *val, bool is_const);
bool         env_assign(Env *env, const char *name, Value *val);
bool         env_is_const(Env *env, const char *name);

Interpreter *interpreter_new(void);
void         interpreter_free(Interpreter *interp);
void         interpreter_run(Interpreter *interp, ASTNode *program);
Value       *interpreter_eval(Interpreter *interp, ASTNode *node, Env *env);

/* Public f-string processor: evaluate {expr} segments using the given env. */
char        *interpreter_process_fstring(Interpreter *interp, const char *tmpl, Env *env);

#endif
