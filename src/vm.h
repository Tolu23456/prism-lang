#ifndef VM_H
#define VM_H

#include "chunk.h"
#include "value.h"
#include "interpreter.h"  /* reuse Env */
#include "gc.h"

#define VM_STACK_MAX   2048
#define VM_FRAME_MAX   256

/* A call frame tracks execution inside a function. */
typedef struct {
    Chunk  *chunk;
    int     ip;          /* instruction pointer (index into chunk->code) */
    int     stack_base;  /* stack index where this frame's locals start */
    Env    *env;         /* variable environment for this frame */
    Env    *root_env;
    int     owns_env;
} CallFrame;

typedef struct VM {
    Value      *stack[VM_STACK_MAX];
    int         stack_top;

    CallFrame   frames[VM_FRAME_MAX];
    int         frame_count;

    Env        *globals;
    PrismGC         *gc;

    int         had_error;
    char        error_msg[512];
} VM;

VM  *vm_new(void);
void vm_free(VM *vm);
int  vm_run(VM *vm, Chunk *chunk);   /* returns 0 on success, 1 on error */

/* Register all built-in functions into the globals env. */
void vm_register_builtins(VM *vm);

#endif /* VM_H */
