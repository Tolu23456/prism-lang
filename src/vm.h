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
#define VM_TRY_MAX         64   /* max nested try/catch blocks */

/* A call frame tracks execution inside one function invocation. */
typedef struct CallFrame {
    Chunk        *chunk;
    int           ip;         /* instruction pointer (index into chunk->code) */
    int           stack_base; /* stack index where this frame's args/locals start */
    Env          *env;        /* variable environment for this frame */
    Env          *root_env;
    int           owns_env;
    int           owns_chunk; /* 1 if this frame allocated the chunk and must free it */

    Value         func;       /* callee function object (retained) */

    /* Local variable flat array (O(1) access — no hash lookup).
     * Compiler emits OP_LOAD_LOCAL / OP_STORE_LOCAL with slot indices. */
    Value locals[VM_LOCALS_MAX];
    int           local_count;

    /* Name mapping for locals (used by debugger / error messages) */
    const char   *local_names[VM_LOCALS_MAX];
} CallFrame;

/* One record per active try/catch block, pushed by OP_TRY_BEGIN. */
typedef struct {
    int handler_ip;   /* chunk ip of the catch handler */
    int frame_count;  /* vm->frame_count when OP_TRY_BEGIN executed */
    int stack_top;    /* vm->stack_top when OP_TRY_BEGIN executed */
} VMTryFrame;

typedef struct VM {
    Value stack[VM_STACK_MAX];
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

    /* Prelude chunk kept alive so function objects defined in the prelude
     * retain valid bytecode pointers throughout the VM lifetime. */
    Chunk          *prelude_chunk;

    /* Module chunks deferred for cleanup at vm_free time so that function
     * objects created via OP_MAKE_FUNCTION (which borrow chunk pointers)
     * remain valid for the entire VM lifetime. */
    Chunk         **mod_chunks;
    int             mod_chunks_count;
    int             mod_chunks_cap;

    /* Try/catch exception handling */
    VMTryFrame      try_frames[VM_TRY_MAX];
    int             try_depth;
    int             exception_handled; /* set by vm_error when exception caught by try */
    char            exception_msg[512];
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
