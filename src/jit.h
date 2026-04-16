#ifndef JIT_H
#define JIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "value.h"
#include "chunk.h"

/* ================================================================== tuning */

#define JIT_HOT_THRESHOLD   200      /* backward-jump count before JIT fires  */
#define JIT_MAX_IR          512      /* max IR instructions per trace          */
#define JIT_MAX_REGS        32       /* register-file slots (0-15 vars, 16-31 temps) */
#define JIT_VAR_SLOTS       16       /* first N slots are named-variable slots */
#define JIT_TEMP_BASE       16       /* temporary slots start here             */
#define JIT_CODE_MAXBYTES   16384    /* max native-code bytes per trace        */
#define JIT_CACHE_CAP       64       /* trace-cache capacity (power of 2)      */
#define JIT_HOT_TABLE_CAP   512      /* hot-counter table capacity (power of 2)*/

/* ================================================================== linear IR opcodes */

typedef enum {
    JIR_NOP,

    /* load/store */
    JIR_LOAD_INT,       /* dst = imm (constant integer)                      */
    JIR_LOAD_FLOAT,     /* dst = fimm (constant float)                       */
    JIR_LOAD_LOCAL,     /* dst = regs[var_slot(name)] — no native code       */
    JIR_STORE_LOCAL,    /* regs[var_slot(name)] = regs[src1] — may copy slot */

    /* integer arithmetic */
    JIR_INT_ADD,        /* dst = src1 + src2                                 */
    JIR_INT_SUB,        /* dst = src1 - src2                                 */
    JIR_INT_MUL,        /* dst = src1 * src2                                 */
    JIR_INT_DIV,        /* dst = src1 / src2  (guard: src2 != 0)             */
    JIR_INT_MOD,        /* dst = src1 % src2  (guard: src2 != 0)             */
    JIR_INT_NEG,        /* dst = -src1                                       */

    /* float arithmetic */
    JIR_FLOAT_ADD, JIR_FLOAT_SUB, JIR_FLOAT_MUL, JIR_FLOAT_DIV,

    /* integer comparison → bool (0 or 1) in dst */
    JIR_CMP_LT, JIR_CMP_LE, JIR_CMP_GT, JIR_CMP_GE, JIR_CMP_EQ, JIR_CMP_NE,

    /* control */
    JIR_EXIT_IF_FALSE,  /* if !regs[src1] → stop looping (condition false)  */
    JIR_LOOP_BACK,      /* jump back to the start of the trace               */
} JIROp;

typedef struct {
    JIROp      op;
    int        dst;        /* destination slot                                */
    int        src1, src2; /* source slots                                    */
    long long  imm;        /* integer immediate                               */
    double     fimm;       /* float immediate                                 */
    const char *name;      /* variable name for LOAD/STORE_LOCAL              */
} JIRInstr;

/* ================================================================== JIT exit codes */

#define JIT_EXIT_LOOP_DONE   0   /* loop condition became false — normal exit */
#define JIT_EXIT_GUARD_FAIL  1   /* type guard failed — fall back to interpreter */

/* JIT trace function:  int fn(long long *regs)
 * regs[0..var_count-1]  hold the current integer values of traced variables.
 * regs[JIT_TEMP_BASE..]  are scratch temporaries.
 * Returns JIT_EXIT_* code. */
typedef int (*JitFn)(long long *regs);

/* ================================================================== compiled trace */

typedef struct JitTrace {
    int      ip;                       /* bytecode IP of the backward jump     */
    int      header_ip;                /* bytecode IP of the loop header        */
    int      exit_ip;                  /* bytecode IP to resume after loop ends */

    /* IR */
    JIRInstr ir[JIT_MAX_IR];
    int      ir_count;

    /* variable ↔ slot mapping (slots 0..var_count-1) */
    const char *vars[JIT_VAR_SLOTS];
    int         var_count;

    /* generated native code */
    uint8_t *code;         /* mmap'd R+W+X buffer                            */
    size_t   code_size;
    JitFn    fn;           /* entry point into code                           */

    /* stats */
    size_t exec_count;
    size_t guard_fails;

    struct JitTrace *next; /* hash-chain for cache                            */
} JitTrace;

/* ================================================================== hot-counter entry */

typedef struct {
    int ip;    /* bytecode IP + 1 (0 = empty slot)                          */
    int count;
} HotEntry;

/* ================================================================== JIT state */

typedef struct JIT {
    bool enabled;

    /* hot-loop detection: open-address hash table keyed by bytecode IP */
    HotEntry hot_table[JIT_HOT_TABLE_CAP];

    /* trace cache: open-address by IP, chained */
    JitTrace *cache[JIT_CACHE_CAP];

    /* stats */
    size_t traces_compiled;
    size_t traces_executed;
    size_t guard_exits;
} JIT;

/* ================================================================== forward declarations */
typedef struct VM  VM;
typedef struct Env Env;

/* ================================================================== public API */

JIT      *jit_new(void);
void      jit_free(JIT *jit);

/* Called on every backward OP_JUMP.
 * jump_ip   = bytecode offset of the OP_JUMP byte.
 * header_ip = jump target (start of loop condition).
 * Returns compiled JitTrace if hot and compiled, NULL otherwise. */
JitTrace *jit_on_backward_jump(JIT *jit, VM *vm, Env *env,
                                int jump_ip, int header_ip, Chunk *chunk);

/* Execute a compiled trace.
 * Returns JIT_EXIT_LOOP_DONE or JIT_EXIT_GUARD_FAIL. */
int       jit_execute(JitTrace *trace, VM *vm, Env *env);

/* Print JIT statistics to stderr. */
void      jit_print_stats(JIT *jit);
void      jit_emit_llvm_ir(JitTrace *trace, const char *name, FILE *out);

/* Dump the IR of a trace to stderr (debug). */
void      jit_dump_ir(JitTrace *trace);

#endif /* JIT_H */
