#ifndef VM_H
#define VM_H

#include "chunk.h"
#include "value.h"
#include "interpreter.h"  /* reuse Env */
#include "gc.h"
#include "jit.h"

#define VM_STACK_MAX     4096
#define VM_FRAME_MAX      512
#define VM_LOCALS_MAX     256   /* max local variable slots per frame */

/* A call frame tracks execution inside one function invocation. */
typedef struct CallFrame {
    Chunk        *chunk;
    int           ip;         /* instruction pointer (index into chunk->code) */
    int           stack_base; /* stack index where this frame's args/locals start */
    Env          *env;        /* variable environment for this frame */
    Env          *root_env;
    int           owns_env;
    int           owns_chunk; /* 1 if this frame allocated the chunk and must free it */

    /* Local variable flat array (O(1) access — no hash lookup).
     * Compiler emits OP_LOAD_LOCAL / OP_STORE_LOCAL with slot indices. */
    Value        *locals[VM_LOCALS_MAX];
    int           local_count;

    /* Name mapping for locals (used by debugger / error messages) */
    const char   *local_names[VM_LOCALS_MAX];
} CallFrame;

typedef struct VM {
    Value          *stack[VM_STACK_MAX];
    int             stack_top;

    CallFrame       frames[VM_FRAME_MAX];
    int             frame_count;

    Env            *globals;
    PrismGC        *gc;

    int             had_error;
    char            error_msg[512];

    JIT            *jit;         /* NULL if JIT is disabled */
    bool            jit_verbose; /* print IR + stats when set */

    /* Benchmarking */
    long long       instructions_executed;
} VM;

VM  *vm_new(void);
void vm_free(VM *vm);
int  vm_run(VM *vm, Chunk *chunk);   /* returns 0 on success, 1 on error */
int  vm_run_prelude(VM *vm);         /* compile & run built-in prelude (filter, map, reduce …) */

/* Register all built-in functions into the globals env. */
void vm_register_builtins(VM *vm);

/* Disassemble a chunk to stdout (for --disasm flag). */
void vm_disassemble(Chunk *chunk, const char *name);

#endif /* VM_H */
