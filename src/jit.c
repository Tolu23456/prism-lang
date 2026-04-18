#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#if defined(__x86_64__) || defined(__amd64__) || defined(__aarch64__)
#  include <sys/mman.h>
#  ifndef MAP_ANONYMOUS
#    ifdef MAP_ANON
#      define MAP_ANONYMOUS MAP_ANON
#    else
#      define MAP_ANONYMOUS 0x20
#    endif
#  endif
#  define JIT_HAS_BACKEND 1
#else
#  define JIT_HAS_BACKEND 0
#endif

#include "jit.h"
#include "vm.h"
#include "chunk.h"
#include "opcode.h"
#include "interpreter.h"   /* Env, env_get, env_assign, env_set */

/* ================================================================== hot-counter helpers */

static int hot_slot(int ip) {
    unsigned h = (unsigned)(ip + 1) * 2654435761u;
    return (int)(h & (JIT_HOT_TABLE_CAP - 1));
}

/* Increment counter for ip; return new count, or 0 if table is full. */
static int hot_increment(JIT *jit, int ip) {
    int key = ip + 1; /* +1 so 0 = empty */
    int s   = hot_slot(ip);
    for (int i = 0; i < JIT_HOT_TABLE_CAP; i++) {
        int idx = (s + i) & (JIT_HOT_TABLE_CAP - 1);
        if (jit->hot_table[idx].ip == 0) {
            jit->hot_table[idx].ip    = key;
            jit->hot_table[idx].count = 1;
            return 1;
        }
        if (jit->hot_table[idx].ip == key) {
            return ++jit->hot_table[idx].count;
        }
    }
    return 0; /* table full */
}

/* Return counter for ip (0 if not found). */
static int hot_count(JIT *jit, int ip) {
    int key = ip + 1;
    int s   = hot_slot(ip);
    for (int i = 0; i < JIT_HOT_TABLE_CAP; i++) {
        int idx = (s + i) & (JIT_HOT_TABLE_CAP - 1);
        if (jit->hot_table[idx].ip == 0)    return 0;
        if (jit->hot_table[idx].ip == key)  return jit->hot_table[idx].count;
    }
    return 0;
}

/* Mark ip as permanently non-JIT-able (set counter to max). */
static void hot_blacklist(JIT *jit, int ip) {
    int key = ip + 1;
    int s   = hot_slot(ip);
    for (int i = 0; i < JIT_HOT_TABLE_CAP; i++) {
        int idx = (s + i) & (JIT_HOT_TABLE_CAP - 1);
        if (jit->hot_table[idx].ip == 0) {
            jit->hot_table[idx].ip    = key;
            jit->hot_table[idx].count = INT_MAX / 2;
            return;
        }
        if (jit->hot_table[idx].ip == key) {
            jit->hot_table[idx].count = INT_MAX / 2;
            return;
        }
    }
}

/* ================================================================== trace cache helpers */

static int cache_slot(int ip) {
    unsigned h = (unsigned)(ip + 1) * 2654435761u;
    return (int)(h & (JIT_CACHE_CAP - 1));
}

static JitTrace *cache_get(JIT *jit, int ip) {
    int s = cache_slot(ip);
    for (JitTrace *t = jit->cache[s]; t; t = t->next)
        if (t->ip == ip) return t;
    return NULL;
}

static void cache_put(JIT *jit, JitTrace *trace) {
    int s = cache_slot(trace->ip);
    trace->next  = jit->cache[s];
    jit->cache[s] = trace;
}

/* ================================================================== variable ↔ slot mapping */

/* Return (or allocate) the slot index for a named variable.
 * Returns -1 if the variable table is full. */
static int var_slot_alloc(JitTrace *trace, const char *name) {
    for (int i = 0; i < trace->var_count; i++) {
        if (trace->vars[i] == name || strcmp(trace->vars[i], name) == 0)
            return i;
    }
    if (trace->var_count >= JIT_VAR_SLOTS) return -1;
    trace->vars[trace->var_count] = name;
    return trace->var_count++;
}

/* Read-only lookup (returns -1 if not found, never allocates). */
static int var_slot_get(JitTrace *trace, const char *name) {
    for (int i = 0; i < trace->var_count; i++) {
        if (trace->vars[i] == name || strcmp(trace->vars[i], name) == 0)
            return i;
    }
    return -1;
}

/* ================================================================== IR emission */

static JIRInstr *ir_emit(JitTrace *t, JIROp op) {
    if (t->ir_count >= JIT_MAX_IR) return NULL;
    JIRInstr *ins = &t->ir[t->ir_count++];
    memset(ins, 0, sizeof(*ins));
    ins->op   = op;
    ins->dst  = ins->src1 = ins->src2 = -1;
    return ins;
}

/* ================================================================== trace recorder
 *
 * Simulate bytecode execution from header_ip through to the backward OP_JUMP
 * at jump_ip, building a linear IR for the loop body.
 *
 * Type stack: (type, slot) pairs tracking what's on the VM stack.
 *   STACK_INT / STACK_FLOAT / STACK_BOOL / STACK_OTHER
 *   slot = register-file index holding the value.
 * ================================================================== */

#define STACK_INT   1
#define STACK_FLOAT 2
#define STACK_BOOL  3
#define STACK_OTHER 0

typedef struct { int type; int slot; } TSItem;
#define TS_MAX 64

static bool record_trace(JitTrace *trace, Chunk *chunk) {
    TSItem ts[TS_MAX];
    int    ts_top = 0;
    int    temp_next = JIT_TEMP_BASE; /* next free temporary slot */

    int ip      = trace->header_ip;
    int stop_ip = trace->ip; /* byte offset of the backward OP_JUMP */

#define TS_PUSH(t,s)  do { if(ts_top<TS_MAX){ts[ts_top].type=(t);ts[ts_top].slot=(s);ts_top++;} } while(0)
#define TS_POP()      (ts_top>0 ? ts[--ts_top] : ((TSItem){STACK_OTHER,-1}))
#define TS_PEEK(n)    (ts_top>(n) ? ts[ts_top-1-(n)] : ((TSItem){STACK_OTHER,-1}))
#define NEW_TEMP()    (temp_next < JIT_MAX_REGS ? temp_next++ : -1)
#define EMT(op)       ir_emit(trace, (op))

    int safety = JIT_MAX_IR * 4;

    while (ip <= stop_ip && safety-- > 0) {
        if (ip >= chunk->count) return false;

        uint8_t  oc = chunk->code[ip++];
        uint16_t op16 = 0;

        /* Most opcodes with an operand use 2 bytes after the opcode byte.
         * We special-case the few that differ. */
        bool has_op = false;
        switch ((Opcode)oc) {
            case OP_PUSH_CONST:
            case OP_LOAD_NAME: case OP_STORE_NAME:
            case OP_DEFINE_NAME: case OP_DEFINE_CONST:
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
            case OP_JUMP_IF_FALSE_PEEK: case OP_JUMP_IF_TRUE_PEEK:
            case OP_MAKE_ARRAY: case OP_MAKE_DICT: case OP_MAKE_SET:
            case OP_MAKE_TUPLE: case OP_CALL: case OP_POP_N:
                has_op = true;
                break;
            default:
                break;
        }
        if (has_op) {
            op16 = (uint16_t)(chunk->code[ip] | ((uint16_t)chunk->code[ip+1] << 8));
            ip  += 2;
        }

        /* Opcodes that make the trace too complex: abort. */
        switch ((Opcode)oc) {
            case OP_CALL: case OP_CALL_METHOD:
            case OP_RETURN: case OP_RETURN_NULL:
            case OP_MAKE_ARRAY: case OP_MAKE_DICT: case OP_MAKE_SET: case OP_MAKE_TUPLE:
            case OP_GET_INDEX: case OP_SET_INDEX:
            case OP_GET_ATTR:  case OP_SET_ATTR:
            case OP_MAKE_FUNCTION:
            case OP_GET_ITER:  case OP_FOR_ITER:
            case OP_BUILD_FSTRING:
            case OP_IMPORT: case OP_HALT:
            case OP_PUSH_SCOPE: case OP_POP_SCOPE:
            case OP_IN: case OP_NOT_IN:
            case OP_BIT_AND: case OP_BIT_OR: case OP_BIT_XOR: case OP_BIT_NOT:
            case OP_LSHIFT: case OP_RSHIFT:
            case OP_PUSH_UNKNOWN:
            case OP_POW:
                return false;
            default: break;
        }

        switch ((Opcode)oc) {

        /* ---- constants ---- */
        case OP_PUSH_NULL:
            TS_PUSH(STACK_OTHER, -1); break;
        case OP_PUSH_TRUE:
        case OP_PUSH_FALSE: {
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_LOAD_INT);
            if (!ins) return false;
            ins->dst = s; ins->imm = ((Opcode)oc == OP_PUSH_TRUE) ? 1 : 0;
            TS_PUSH(STACK_BOOL, s);
            break;
        }
        case OP_PUSH_CONST: {
            Value *c = chunk->constants[op16];
            if (c->type == VAL_INT) {
                int s = NEW_TEMP(); if (s < 0) return false;
                JIRInstr *ins = EMT(JIR_LOAD_INT);
                if (!ins) return false;
                ins->dst = s; ins->imm = c->int_val;
                TS_PUSH(STACK_INT, s);
            } else if (c->type == VAL_FLOAT) {
                int s = NEW_TEMP(); if (s < 0) return false;
                JIRInstr *ins = EMT(JIR_LOAD_FLOAT);
                if (!ins) return false;
                ins->dst = s; ins->fimm = c->float_val;
                TS_PUSH(STACK_FLOAT, s);
            } else {
                return false; /* string / other constants: abort */
            }
            break;
        }

        /* ---- variables ---- */
        case OP_LOAD_NAME: {
            const char *name = chunk->constants[op16]->str_val;
            int s = var_slot_alloc(trace, name);
            if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_LOAD_LOCAL);
            if (!ins) return false;
            ins->dst = s; ins->name = name;
            /* Assume int; guard is checked at execution entry. */
            TS_PUSH(STACK_INT, s);
            break;
        }
        case OP_STORE_NAME:
        case OP_DEFINE_NAME:
        case OP_DEFINE_CONST: {
            const char *name = chunk->constants[op16]->str_val;
            TSItem top = TS_POP();
            if (top.type == STACK_OTHER) return false;
            int s = var_slot_alloc(trace, name);
            if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_STORE_LOCAL);
            if (!ins) return false;
            ins->src1 = top.slot; ins->name = name; ins->dst = s;
            break;
        }

        /* ---- arithmetic ---- */
        case OP_ADD: {
            TSItem b = TS_POP(), a = TS_POP();
            if (a.type == STACK_INT && b.type == STACK_INT) {
                int s = NEW_TEMP(); if (s < 0) return false;
                JIRInstr *ins = EMT(JIR_INT_ADD); if (!ins) return false;
                ins->dst = s; ins->src1 = a.slot; ins->src2 = b.slot;
                TS_PUSH(STACK_INT, s);
            } else return false;
            break;
        }
        case OP_SUB: {
            TSItem b = TS_POP(), a = TS_POP();
            if (a.type != STACK_INT || b.type != STACK_INT) return false;
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_INT_SUB); if (!ins) return false;
            ins->dst = s; ins->src1 = a.slot; ins->src2 = b.slot;
            TS_PUSH(STACK_INT, s); break;
        }
        case OP_MUL: {
            TSItem b = TS_POP(), a = TS_POP();
            if (a.type != STACK_INT || b.type != STACK_INT) return false;
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_INT_MUL); if (!ins) return false;
            ins->dst = s; ins->src1 = a.slot; ins->src2 = b.slot;
            TS_PUSH(STACK_INT, s); break;
        }
        case OP_DIV: {
            TSItem b = TS_POP(), a = TS_POP();
            if (a.type != STACK_INT || b.type != STACK_INT) return false;
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_INT_DIV); if (!ins) return false;
            ins->dst = s; ins->src1 = a.slot; ins->src2 = b.slot;
            TS_PUSH(STACK_INT, s); break;
        }
        case OP_MOD: {
            TSItem b = TS_POP(), a = TS_POP();
            if (a.type != STACK_INT || b.type != STACK_INT) return false;
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_INT_MOD); if (!ins) return false;
            ins->dst = s; ins->src1 = a.slot; ins->src2 = b.slot;
            TS_PUSH(STACK_INT, s); break;
        }
        case OP_NEG: {
            TSItem a = TS_POP();
            if (a.type != STACK_INT) return false;
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(JIR_INT_NEG); if (!ins) return false;
            ins->dst = s; ins->src1 = a.slot;
            TS_PUSH(STACK_INT, s); break;
        }
        case OP_POS: break; /* no-op */

        /* ---- comparison ---- */
        case OP_LT: case OP_LE: case OP_GT:
        case OP_GE: case OP_EQ: case OP_NE: {
            TSItem b = TS_POP(), a = TS_POP();
            if (a.type != STACK_INT || b.type != STACK_INT) return false;
            JIROp cop;
            switch ((Opcode)oc) {
                case OP_LT: cop = JIR_CMP_LT; break;
                case OP_LE: cop = JIR_CMP_LE; break;
                case OP_GT: cop = JIR_CMP_GT; break;
                case OP_GE: cop = JIR_CMP_GE; break;
                case OP_EQ: cop = JIR_CMP_EQ; break;
                default:    cop = JIR_CMP_NE; break;
            }
            int s = NEW_TEMP(); if (s < 0) return false;
            JIRInstr *ins = EMT(cop); if (!ins) return false;
            ins->dst = s; ins->src1 = a.slot; ins->src2 = b.slot;
            TS_PUSH(STACK_BOOL, s); break;
        }

        /* ---- logical not ---- */
        case OP_NOT: {
            TSItem a = TS_POP();
            if (a.type != STACK_BOOL && a.type != STACK_INT) return false;
            int s = NEW_TEMP(); if (s < 0) return false;
            /* not x = (x == 0) */
            JIRInstr *ins = EMT(JIR_LOAD_INT); if (!ins) return false;
            ins->dst = s; ins->imm = 0;
            int s2 = NEW_TEMP(); if (s2 < 0) return false;
            JIRInstr *ins2 = EMT(JIR_CMP_EQ); if (!ins2) return false;
            ins2->dst = s2; ins2->src1 = a.slot; ins2->src2 = s;
            TS_PUSH(STACK_BOOL, s2); break;
        }

        /* ---- stack ops ---- */
        case OP_POP: TS_POP(); break;
        case OP_DUP: { TSItem t = TS_PEEK(0); TS_PUSH(t.type, t.slot); break; }
        case OP_POP_N:
            for (int k = 0; k < (int)op16 && ts_top > 0; k++) TS_POP();
            break;

        /* ---- jumps ---- */
        case OP_JUMP: {
            /* ip is now past the operand (= start_of_instruction + 3) */
            int16_t off    = (int16_t)op16;
            int     target = ip + (int)off;
            if (target == trace->header_ip) {
                /* This is the back-edge — end of trace. */
                JIRInstr *ins = EMT(JIR_LOOP_BACK);
                if (!ins) return false;
                return true; /* success */
            }
            /* Any other unconditional jump inside the trace = too complex. */
            return false;
        }
        case OP_JUMP_IF_FALSE: {
            int16_t off    = (int16_t)op16;
            int     target = ip + (int)off;
            TSItem  cond   = TS_POP();
            if (target > ip) {
                /* Forward jump = loop-exit condition. */
                if (trace->exit_ip == 0) trace->exit_ip = target;
                JIRInstr *ins = EMT(JIR_EXIT_IF_FALSE);
                if (!ins) return false;
                ins->src1 = cond.slot;
            } else {
                return false; /* backward conditional = too complex */
            }
            break;
        }
        case OP_JUMP_IF_TRUE: {
            int16_t off    = (int16_t)op16;
            int     target = ip + (int)off;
            TSItem  cond   = TS_POP();
            if (target > ip) {
                /* Forward jump: exit if true (inverted condition).
                 * Emit: tmp = (cond == 0); EXIT_IF_FALSE tmp */
                int s  = NEW_TEMP(); if (s < 0) return false;
                int s2 = NEW_TEMP(); if (s2 < 0) return false;
                JIRInstr *li = EMT(JIR_LOAD_INT); if (!li) return false;
                li->dst = s; li->imm = 0;
                JIRInstr *eq = EMT(JIR_CMP_EQ); if (!eq) return false;
                eq->dst = s2; eq->src1 = cond.slot; eq->src2 = s;
                if (trace->exit_ip == 0) trace->exit_ip = target;
                JIRInstr *ex = EMT(JIR_EXIT_IF_FALSE); if (!ex) return false;
                ex->src1 = s2;
            } else {
                return false;
            }
            break;
        }
        case OP_JUMP_IF_FALSE_PEEK:
        case OP_JUMP_IF_TRUE_PEEK:
            /* Peek variants used for short-circuit 'and'/'or': too complex. */
            return false;

        default:
            return false;
        }
    }

    return false; /* never found LOOP_BACK */

#undef TS_PUSH
#undef TS_POP
#undef TS_PEEK
#undef NEW_TEMP
#undef EMT
}

/* ================================================================== x86-64 code generator */

#if defined(__x86_64__) || defined(__amd64__)

typedef struct {
    uint8_t *buf;
    size_t   pos;
    size_t   cap;
    /* patch sites: buffer positions where we need to write the exit address */
    int exit_patches[JIT_MAX_IR];
    int exit_patch_count;
} X64B;

static inline void b1(X64B *b, uint8_t v) {
    if (b->pos < b->cap) b->buf[b->pos++] = v;
}
static inline void b4(X64B *b, uint32_t v) {
    b1(b, (uint8_t)(v));
    b1(b, (uint8_t)(v >> 8));
    b1(b, (uint8_t)(v >> 16));
    b1(b, (uint8_t)(v >> 24));
}
static inline void b8(X64B *b, uint64_t v) {
    b4(b, (uint32_t)v);
    b4(b, (uint32_t)(v >> 32));
}

/* Helper: patch a 32-bit little-endian integer at position patch_pos. */
static void x64_patch32(X64B *b, int patch_pos, int32_t val) {
    b->buf[patch_pos]   = (uint8_t)((uint32_t)val);
    b->buf[patch_pos+1] = (uint8_t)((uint32_t)val >>  8);
    b->buf[patch_pos+2] = (uint8_t)((uint32_t)val >> 16);
    b->buf[patch_pos+3] = (uint8_t)((uint32_t)val >> 24);
}

/* rdi = regs pointer.  We use rax, rcx, rdx as scratch.
 * Register file:  long long regs[JIT_MAX_REGS], accessed via [rdi + slot*8]. */

/* mov rax, [rdi + slot*8] */
static void x64_load_rax(X64B *b, int slot) {
    int disp = slot * 8;
    if (disp == 0) {
        b1(b, 0x48); b1(b, 0x8B); b1(b, 0x07);                  /* mov rax, [rdi] */
    } else if (disp <= 127) {
        b1(b, 0x48); b1(b, 0x8B); b1(b, 0x47); b1(b, (uint8_t)disp);
    } else {
        b1(b, 0x48); b1(b, 0x8B); b1(b, 0x87); b4(b, (uint32_t)disp);
    }
}

/* mov rcx, [rdi + slot*8] */
static void x64_load_rcx(X64B *b, int slot) {
    int disp = slot * 8;
    if (disp == 0) {
        b1(b, 0x48); b1(b, 0x8B); b1(b, 0x0F);
    } else if (disp <= 127) {
        b1(b, 0x48); b1(b, 0x8B); b1(b, 0x4F); b1(b, (uint8_t)disp);
    } else {
        b1(b, 0x48); b1(b, 0x8B); b1(b, 0x8F); b4(b, (uint32_t)disp);
    }
}

/* mov [rdi + slot*8], rax */
static void x64_store_rax(X64B *b, int slot) {
    int disp = slot * 8;
    if (disp == 0) {
        b1(b, 0x48); b1(b, 0x89); b1(b, 0x07);
    } else if (disp <= 127) {
        b1(b, 0x48); b1(b, 0x89); b1(b, 0x47); b1(b, (uint8_t)disp);
    } else {
        b1(b, 0x48); b1(b, 0x89); b1(b, 0x87); b4(b, (uint32_t)disp);
    }
}

/* mov rax, imm64 */
static void x64_mov_rax_imm64(X64B *b, long long v) {
    b1(b, 0x48); b1(b, 0xB8); b8(b, (uint64_t)v);
}

/* add rax, rcx     sub rax, rcx     imul rax, rcx     neg rax */
static void x64_add_rax_rcx(X64B *b)  { b1(b,0x48);b1(b,0x01);b1(b,0xC8); }
static void x64_sub_rax_rcx(X64B *b)  { b1(b,0x48);b1(b,0x29);b1(b,0xC8); }
static void x64_imul_rax_rcx(X64B *b) { b1(b,0x48);b1(b,0x0F);b1(b,0xAF);b1(b,0xC1); }
static void x64_neg_rax(X64B *b)      { b1(b,0x48);b1(b,0xF7);b1(b,0xD8); }

/* cqo (sign-extend rax into rdx:rax); idiv rcx */
static void x64_cqo(X64B *b)       { b1(b,0x48);b1(b,0x99); }
static void x64_idiv_rcx(X64B *b)  { b1(b,0x48);b1(b,0xF7);b1(b,0xF9); }

/* test rcx, rcx — used for zero-divisor guard */
static void x64_test_rcx(X64B *b)  { b1(b,0x48);b1(b,0x85);b1(b,0xC9); }

/* cmp rax, rcx */
static void x64_cmp_rax_rcx(X64B *b)  { b1(b,0x48);b1(b,0x39);b1(b,0xC8); }

/* setcc al; movzx rax, al */
static void x64_setcc(X64B *b, uint8_t cc) {
    b1(b,0x0F); b1(b,cc); b1(b,0xC0);          /* setcc al */
    b1(b,0x48);b1(b,0x0F);b1(b,0xB6);b1(b,0xC0);/* movzx rax, al */
}

/* test rax, rax */
static void x64_test_rax(X64B *b) { b1(b,0x48);b1(b,0x85);b1(b,0xC0); }

/* mov rdx, rax */
static void x64_mov_rdx_rax(X64B *b) { b1(b,0x48);b1(b,0x89);b1(b,0xC2); }

/* mov rax, rdx */
static void x64_mov_rax_rdx(X64B *b) { b1(b,0x48);b1(b,0x89);b1(b,0xD0); }

/* xor eax, eax */
static void x64_xor_eax(X64B *b)  { b1(b,0x31);b1(b,0xC0); }
/* mov eax, imm32 */
static void x64_mov_eax_imm(X64B *b, int v) { b1(b,0xB8); b4(b,(uint32_t)v); }
/* ret */
static void x64_ret(X64B *b)      { b1(b,0xC3); }

/* Emit jz rel32 (0F 84 + 4-byte placeholder). Returns patch site position. */
static int x64_jz_rel32(X64B *b) {
    b1(b,0x0F); b1(b,0x84);
    int pos = (int)b->pos;
    b4(b, 0); /* placeholder */
    return pos;
}

/* Emit jmp rel32 (E9 + 4-byte placeholder). Returns patch site position. */
static int x64_jmp_rel32_placeholder(X64B *b) {
    b1(b, 0xE9);
    int pos = (int)b->pos;
    b4(b, 0);
    return pos;
}

static bool x64_compile(JitTrace *trace, uint8_t *buf, size_t cap, size_t *out_size) {
    X64B b = {0};
    b.buf = buf;
    b.cap = cap;

    size_t loop_start = 0; /* offset of first instruction = start of the loop */

    /* guard_fail label: return JIT_EXIT_GUARD_FAIL (= 1) */
    /* We'll collect all guard-fail jumps and patch them at the end. */
    int guard_patches[JIT_MAX_IR];
    int guard_patch_count = 0;

    for (int i = 0; i < trace->ir_count; i++) {
        JIRInstr *ins = &trace->ir[i];

        switch (ins->op) {

        case JIR_LOAD_INT:
            x64_mov_rax_imm64(&b, ins->imm);
            x64_store_rax(&b, ins->dst);
            break;

        case JIR_LOAD_FLOAT:
            /* For now: store as bit-cast of double — handled in execution.
             * Float JIT support is architecture-specific (XMM regs needed).
             * We store the double bits as int64 and the execution path handles it.
             * Actually this path shouldn't be reached for float traces in this version. */
            break;

        case JIR_LOAD_LOCAL:
            /* No code: variable slot is already populated at trace entry. */
            break;

        case JIR_STORE_LOCAL: {
            int var_s = var_slot_get(trace, ins->name);
            if (var_s < 0) return false;
            if (ins->src1 >= 0 && ins->src1 != var_s) {
                x64_load_rax(&b, ins->src1);
                x64_store_rax(&b, var_s);
            }
            break;
        }

        case JIR_INT_ADD:
            x64_load_rax(&b, ins->src1);
            x64_load_rcx(&b, ins->src2);
            x64_add_rax_rcx(&b);
            x64_store_rax(&b, ins->dst);
            break;

        case JIR_INT_SUB:
            x64_load_rax(&b, ins->src1);
            x64_load_rcx(&b, ins->src2);
            x64_sub_rax_rcx(&b);
            x64_store_rax(&b, ins->dst);
            break;

        case JIR_INT_MUL:
            x64_load_rax(&b, ins->src1);
            x64_load_rcx(&b, ins->src2);
            x64_imul_rax_rcx(&b);
            x64_store_rax(&b, ins->dst);
            break;

        case JIR_INT_DIV:
            /* Guard: divisor != 0. On fail → guard_fail path. */
            x64_load_rcx(&b, ins->src2);
            x64_test_rcx(&b);
            /* jz guard_fail */
            { int p = x64_jz_rel32(&b);
              if (guard_patch_count < JIT_MAX_IR)
                  guard_patches[guard_patch_count++] = p; }
            x64_load_rax(&b, ins->src1);
            x64_cqo(&b);
            x64_idiv_rcx(&b);   /* quotient in rax */
            x64_store_rax(&b, ins->dst);
            break;

        case JIR_INT_MOD:
            /* Guard: divisor != 0. */
            x64_load_rcx(&b, ins->src2);
            x64_test_rcx(&b);
            { int p = x64_jz_rel32(&b);
              if (guard_patch_count < JIT_MAX_IR)
                  guard_patches[guard_patch_count++] = p; }
            x64_load_rax(&b, ins->src1);
            x64_cqo(&b);
            x64_idiv_rcx(&b);   /* remainder in rdx */
            x64_mov_rax_rdx(&b);
            x64_store_rax(&b, ins->dst);
            break;

        case JIR_INT_NEG:
            x64_load_rax(&b, ins->src1);
            x64_neg_rax(&b);
            x64_store_rax(&b, ins->dst);
            break;

        /* Comparisons: setcc byte maps to Opcode */
        case JIR_CMP_LT: case JIR_CMP_LE: case JIR_CMP_GT:
        case JIR_CMP_GE: case JIR_CMP_EQ: case JIR_CMP_NE: {
            uint8_t cc;
            switch (ins->op) {
                case JIR_CMP_LT: cc = 0x9C; break; /* setl  */
                case JIR_CMP_LE: cc = 0x9E; break; /* setle */
                case JIR_CMP_GT: cc = 0x9F; break; /* setg  */
                case JIR_CMP_GE: cc = 0x9D; break; /* setge */
                case JIR_CMP_EQ: cc = 0x94; break; /* sete  */
                default:         cc = 0x95; break; /* setne */
            }
            x64_load_rax(&b, ins->src1);
            x64_load_rcx(&b, ins->src2);
            x64_cmp_rax_rcx(&b);
            x64_setcc(&b, cc);
            x64_store_rax(&b, ins->dst);
            break;
        }

        case JIR_EXIT_IF_FALSE: {
            /* Load condition; if 0, jump to exit (condition false = loop done). */
            x64_load_rax(&b, ins->src1);
            x64_test_rax(&b);
            int pos = x64_jz_rel32(&b);
            if (b.exit_patch_count < JIT_MAX_IR)
                b.exit_patches[b.exit_patch_count++] = pos;
            break;
        }

        case JIR_LOOP_BACK: {
            /* Unconditional jump back to the start of this trace. */
            int patch_pos = x64_jmp_rel32_placeholder(&b);
            int32_t off   = (int32_t)loop_start - (int32_t)(patch_pos + 4);
            x64_patch32(&b, patch_pos, off);
            break;
        }

        default: break;
        }
    }

    /* ---- guard_fail label: return 1 ---- */
    size_t guard_label = b.pos;
    for (int i = 0; i < guard_patch_count; i++) {
        int32_t off = (int32_t)guard_label - (int32_t)(guard_patches[i] + 4);
        x64_patch32(&b, guard_patches[i], off);
    }
    x64_mov_eax_imm(&b, JIT_EXIT_GUARD_FAIL);
    x64_ret(&b);

    /* ---- exit label: return 0 (loop done) ---- */
    size_t exit_label = b.pos;
    for (int i = 0; i < b.exit_patch_count; i++) {
        int32_t off = (int32_t)exit_label - (int32_t)(b.exit_patches[i] + 4);
        x64_patch32(&b, b.exit_patches[i], off);
    }
    x64_xor_eax(&b);
    x64_ret(&b);

    *out_size = b.pos;
    return (b.pos > 0 && b.pos < cap);
}

#endif /* x86_64 */

/* ================================================================== ARM64 code generator */

#if defined(__aarch64__)

typedef struct {
    uint32_t *buf;
    size_t    pos;    /* in uint32_t units */
    size_t    cap;
    int exit_patches[JIT_MAX_IR];
    int exit_patch_count;
} A64B;

static inline void a64_emit(A64B *b, uint32_t instr) {
    if (b->pos < b->cap) b->buf[b->pos++] = instr;
}
static inline void a64_patch(A64B *b, int pos, uint32_t instr) {
    if ((size_t)pos < b->cap) b->buf[pos] = instr;
}

/* x0 = regs pointer.  x1, x2 scratch for binary ops.  x3 scratch. */

/* ldr x1, [x0, #slot*8] */
static void a64_load_x1(A64B *b, int slot) {
    uint32_t imm12 = (uint32_t)(slot & 0x1FF);
    a64_emit(b, 0xF9400001u | (imm12 << 10)); /* LDR x1, [x0, #slot*8] */
}
/* ldr x2, [x0, #slot*8] */
static void a64_load_x2(A64B *b, int slot) {
    uint32_t imm12 = (uint32_t)(slot & 0x1FF);
    a64_emit(b, 0xF9400002u | (imm12 << 10));
}
/* str x1, [x0, #slot*8] */
static void a64_store_x1(A64B *b, int slot) {
    uint32_t imm12 = (uint32_t)(slot & 0x1FF);
    a64_emit(b, 0xF9000001u | (imm12 << 10));
}
/* str x3, [x0, #slot*8] */
static void a64_store_x3(A64B *b, int slot) {
    uint32_t imm12 = (uint32_t)(slot & 0x1FF);
    a64_emit(b, 0xF9000003u | (imm12 << 10));
}

/* ADD x1, x1, x2    SUB x1, x1, x2    MUL x1, x1, x2   NEG x1, x1 */
static void a64_add_x1_x2(A64B *b) { a64_emit(b, 0x8B020021u); } /* add x1, x1, x2 */
static void a64_sub_x1_x2(A64B *b) { a64_emit(b, 0xCB020021u); } /* sub x1, x1, x2 */
static void a64_mul_x1_x2(A64B *b) { a64_emit(b, 0x9B027C21u); } /* mul x1, x1, x2 */
static void a64_neg_x1(A64B *b)    { a64_emit(b, 0xCB0103E1u); } /* neg x1, x1 (sub x1, xzr, x1) */

/* SDIV x1, x1, x2 */
static void a64_sdiv_x1_x2(A64B *b) { a64_emit(b, 0x9AC20C21u); }
/* MSUB x1, x1, x2, x3  (x1 = x3 - x1*x2  → used for mod: rem = dividend - quot*divisor) */
static void a64_msub_x1_x2_x3(A64B *b) { a64_emit(b, 0x9B028C61u); } /* msub x1, x1, x2, x3 */

/* CMP x1, x2 (= SUBS xzr, x1, x2) */
static void a64_cmp_x1_x2(A64B *b) { a64_emit(b, 0xEB02001Fu); }

/* CSET x3, cond  (= CSINC x3, xzr, xzr, !cond) */
static void a64_cset_x3(A64B *b, uint32_t cond) {
    uint32_t cond_inv = cond ^ 1u;
    a64_emit(b, 0x9A9F07E3u | (cond_inv << 12)); /* cset x3, cond */
}

/* CBZ x1, label (compare and branch if zero) — placeholder, returns pos for patch */
static int a64_cbz_x1_placeholder(A64B *b) {
    int pos = (int)b->pos;
    a64_emit(b, 0xB4000001u); /* cbz x1, #0 — will patch offset */
    return pos;
}

/* B label — unconditional branch — placeholder */
static int a64_b_placeholder(A64B *b) {
    int pos = (int)b->pos;
    a64_emit(b, 0x14000000u);
    return pos;
}

/* MOV x0, #imm16 */
static void a64_mov_x0_imm(A64B *b, int imm) {
    a64_emit(b, 0xD2800000u | ((uint32_t)(imm & 0xFFFF) << 5));
}
/* RET */
static void a64_ret(A64B *b) { a64_emit(b, 0xD65F03C0u); }

/* Patch a CBZ instruction at pos with the offset to 'target' (in word units). */
static void a64_patch_cbz(A64B *b, int pos, int target) {
    int offset    = target - pos; /* word units */
    uint32_t imm19 = (uint32_t)(offset & 0x7FFFFu);
    uint32_t base = b->buf[pos] & 0xFF00001Fu;
    a64_patch(b, pos, base | (imm19 << 5));
}
/* Patch a B instruction. */
static void a64_patch_b(A64B *b, int pos, int target) {
    int offset    = target - pos;
    uint32_t imm26 = (uint32_t)(offset & 0x3FFFFFFu);
    a64_patch(b, pos, 0x14000000u | imm26);
}

/* ARM64 condition codes used with CSET: LT=11, LE=13, GT=12, GE=10, EQ=0, NE=1 */
#define A64_CC_EQ  0u
#define A64_CC_NE  1u
#define A64_CC_GE  10u
#define A64_CC_LT  11u
#define A64_CC_GT  12u
#define A64_CC_LE  13u

static bool a64_compile(JitTrace *trace, uint8_t *bytebuf, size_t cap, size_t *out_size) {
    A64B b = {0};
    b.buf = (uint32_t *)bytebuf;
    b.cap = cap / 4;

    int loop_start = 0;

    int guard_patches[JIT_MAX_IR];
    int guard_patch_count = 0;

    for (int i = 0; i < trace->ir_count; i++) {
        JIRInstr *ins = &trace->ir[i];

        switch (ins->op) {

        case JIR_LOAD_INT: {
            /* MOVZ x1, #low16; if large, also MOVK for upper bits.
             * For simplicity use a 4-instruction load sequence for full 64-bit. */
            long long v = ins->imm;
            /* MOVZ x1, #bits[15:0] */
            a64_emit(&b, 0xD2800001u | (((uint32_t)(v & 0xFFFF)) << 5));
            if ((v >> 16) & 0xFFFF)
                a64_emit(&b, 0xF2A00001u | (((uint32_t)((v>>16) & 0xFFFF)) << 5)); /* MOVK */
            if ((v >> 32) & 0xFFFF)
                a64_emit(&b, 0xF2C00001u | (((uint32_t)((v>>32) & 0xFFFF)) << 5));
            if ((v >> 48) & 0xFFFF)
                a64_emit(&b, 0xF2E00001u | (((uint32_t)((v>>48) & 0xFFFF)) << 5));
            a64_store_x1(&b, ins->dst);
            break;
        }

        case JIR_LOAD_LOCAL: break; /* no code needed */

        case JIR_STORE_LOCAL: {
            int var_s = var_slot_get(trace, ins->name);
            if (var_s < 0) return false;
            if (ins->src1 >= 0 && ins->src1 != var_s) {
                a64_load_x1(&b, ins->src1);
                a64_store_x1(&b, var_s);
            }
            break;
        }

        case JIR_INT_ADD:
            a64_load_x1(&b, ins->src1);
            a64_load_x2(&b, ins->src2);
            a64_add_x1_x2(&b);
            a64_store_x1(&b, ins->dst);
            break;

        case JIR_INT_SUB:
            a64_load_x1(&b, ins->src1);
            a64_load_x2(&b, ins->src2);
            a64_sub_x1_x2(&b);
            a64_store_x1(&b, ins->dst);
            break;

        case JIR_INT_MUL:
            a64_load_x1(&b, ins->src1);
            a64_load_x2(&b, ins->src2);
            a64_mul_x1_x2(&b);
            a64_store_x1(&b, ins->dst);
            break;

        case JIR_INT_DIV:
            /* Guard: divisor != 0 */
            a64_load_x2(&b, ins->src2);
            { int pp = a64_cbz_x1_placeholder(&b); /* cbz x2... but x2 not x1 */
              /* Emit CBZ x2 instead: */
              b.buf[pp] = 0xB4000002u; /* cbz x2, #0 placeholder */
              if (guard_patch_count < JIT_MAX_IR) guard_patches[guard_patch_count++] = pp; }
            a64_load_x1(&b, ins->src1);
            a64_sdiv_x1_x2(&b);
            a64_store_x1(&b, ins->dst);
            break;

        case JIR_INT_MOD:
            /* x1 = dividend, x2 = divisor */
            a64_load_x2(&b, ins->src2);
            { int pp = a64_cbz_x1_placeholder(&b);
              b.buf[pp] = 0xB4000002u; /* cbz x2, guard */
              if (guard_patch_count < JIT_MAX_IR) guard_patches[guard_patch_count++] = pp; }
            a64_load_x1(&b, ins->src1);
            /* x3 = x1 (save dividend for msub) */
            a64_emit(&b, 0xAA0103E3u); /* mov x3, x1 */
            a64_sdiv_x1_x2(&b);        /* x1 = x1 / x2 */
            /* x1 = x3 - x1 * x2 = dividend - (dividend/divisor)*divisor = remainder */
            a64_msub_x1_x2_x3(&b);
            a64_store_x1(&b, ins->dst);
            break;

        case JIR_INT_NEG:
            a64_load_x1(&b, ins->src1);
            a64_neg_x1(&b);
            a64_store_x1(&b, ins->dst);
            break;

        case JIR_CMP_LT: case JIR_CMP_LE: case JIR_CMP_GT:
        case JIR_CMP_GE: case JIR_CMP_EQ: case JIR_CMP_NE: {
            uint32_t cc;
            switch (ins->op) {
                case JIR_CMP_LT: cc = A64_CC_LT; break;
                case JIR_CMP_LE: cc = A64_CC_LE; break;
                case JIR_CMP_GT: cc = A64_CC_GT; break;
                case JIR_CMP_GE: cc = A64_CC_GE; break;
                case JIR_CMP_EQ: cc = A64_CC_EQ; break;
                default:         cc = A64_CC_NE; break;
            }
            a64_load_x1(&b, ins->src1);
            a64_load_x2(&b, ins->src2);
            a64_cmp_x1_x2(&b);
            a64_cset_x3(&b, cc);
            a64_store_x3(&b, ins->dst);
            break;
        }

        case JIR_EXIT_IF_FALSE: {
            a64_load_x1(&b, ins->src1);
            int pos = a64_cbz_x1_placeholder(&b); /* cbz x1, [exit] */
            if (b.exit_patch_count < JIT_MAX_IR)
                b.exit_patches[b.exit_patch_count++] = pos;
            break;
        }

        case JIR_LOOP_BACK: {
            int pos = a64_b_placeholder(&b);
            a64_patch_b(&b, pos, loop_start);
            break;
        }

        default: break;
        }
    }

    /* guard_fail: mov x0, 1; ret */
    int guard_label = (int)b.pos;
    for (int i = 0; i < guard_patch_count; i++)
        a64_patch_cbz(&b, guard_patches[i], guard_label);
    a64_mov_x0_imm(&b, JIT_EXIT_GUARD_FAIL);
    a64_ret(&b);

    /* exit: mov x0, 0; ret */
    int exit_label = (int)b.pos;
    for (int i = 0; i < b.exit_patch_count; i++)
        a64_patch_cbz(&b, b.exit_patches[i], exit_label);
    a64_mov_x0_imm(&b, JIT_EXIT_LOOP_DONE);
    a64_ret(&b);

    *out_size = b.pos * 4;
    return (b.pos > 0 && b.pos * 4 < cap);
}

#endif /* aarch64 */

/* ================================================================== LLVM IR emitter
 *
 * Produces textual LLVM IR (.ll) that can be compiled with:
 *   llvm-as file.ll -o file.bc
 *   lli file.bc
 * or fed directly to clang.
 * ================================================================== */

void jit_emit_llvm_ir(JitTrace *trace, const char *chunk_name, FILE *out) {
    fprintf(out, "; LLVM IR emitted by Prism JIT for loop at bytecode offset %d\n", trace->ip);
    fprintf(out, "; Source chunk: %s\n\n", chunk_name ? chunk_name : "<unknown>");
    fprintf(out, "define i64 @jit_loop(i64* %%regs) {\n");
    fprintf(out, "entry:\n");

    /* Hoist LOAD_INT constants. */
    for (int i = 0; i < trace->ir_count; i++) {
        JIRInstr *ins = &trace->ir[i];
        if (ins->op == JIR_LOAD_INT) {
            fprintf(out, "  %%cst%d = getelementptr inbounds i64, i64* %%regs, i32 %d\n",
                    ins->dst, ins->dst);
            fprintf(out, "  store i64 %lld, i64* %%cst%d\n", (long long)ins->imm, ins->dst);
        }
    }
    fprintf(out, "  br label %%loop_head\n\n");
    fprintf(out, "loop_head:\n");

    /* Load all variable slots into SSA names. */
    for (int v = 0; v < trace->var_count; v++) {
        fprintf(out, "  %%ptr_v%d = getelementptr inbounds i64, i64* %%regs, i32 %d\n", v, v);
        fprintf(out, "  %%v%d_0 = load i64, i64* %%ptr_v%d\n", v, v);
    }

    /* Emit body IR. */
    int tmp_ver[JIT_MAX_REGS] = {0};
    for (int v = 0; v < trace->var_count; v++) tmp_ver[v] = 0;

    for (int i = 0; i < trace->ir_count; i++) {
        JIRInstr *ins = &trace->ir[i];
        switch (ins->op) {
        case JIR_INT_ADD:
            fprintf(out, "  %%t%d_%d = add i64 %%t%d_%d, %%t%d_%d\n",
                    ins->dst, ++tmp_ver[ins->dst],
                    ins->src1, tmp_ver[ins->src1],
                    ins->src2, tmp_ver[ins->src2]);
            break;
        case JIR_INT_SUB:
            fprintf(out, "  %%t%d_%d = sub i64 %%t%d_%d, %%t%d_%d\n",
                    ins->dst, ++tmp_ver[ins->dst],
                    ins->src1, tmp_ver[ins->src1],
                    ins->src2, tmp_ver[ins->src2]);
            break;
        case JIR_INT_MUL:
            fprintf(out, "  %%t%d_%d = mul i64 %%t%d_%d, %%t%d_%d\n",
                    ins->dst, ++tmp_ver[ins->dst],
                    ins->src1, tmp_ver[ins->src1],
                    ins->src2, tmp_ver[ins->src2]);
            break;
        case JIR_INT_DIV:
            fprintf(out, "  %%t%d_%d = sdiv i64 %%t%d_%d, %%t%d_%d\n",
                    ins->dst, ++tmp_ver[ins->dst],
                    ins->src1, tmp_ver[ins->src1],
                    ins->src2, tmp_ver[ins->src2]);
            break;
        case JIR_INT_MOD:
            fprintf(out, "  %%t%d_%d = srem i64 %%t%d_%d, %%t%d_%d\n",
                    ins->dst, ++tmp_ver[ins->dst],
                    ins->src1, tmp_ver[ins->src1],
                    ins->src2, tmp_ver[ins->src2]);
            break;
        case JIR_INT_NEG:
            fprintf(out, "  %%t%d_%d = sub i64 0, %%t%d_%d\n",
                    ins->dst, ++tmp_ver[ins->dst],
                    ins->src1, tmp_ver[ins->src1]);
            break;
        case JIR_CMP_LT: case JIR_CMP_LE: case JIR_CMP_GT:
        case JIR_CMP_GE: case JIR_CMP_EQ: case JIR_CMP_NE: {
            const char *pred = "";
            switch (ins->op) {
                case JIR_CMP_LT: pred="slt"; break; case JIR_CMP_LE: pred="sle"; break;
                case JIR_CMP_GT: pred="sgt"; break; case JIR_CMP_GE: pred="sge"; break;
                case JIR_CMP_EQ: pred="eq";  break; default: pred="ne"; break;
            }
            fprintf(out, "  %%cmp%d_%d = icmp %s i64 %%t%d_%d, %%t%d_%d\n",
                    ins->dst, tmp_ver[ins->dst]+1, pred,
                    ins->src1, tmp_ver[ins->src1],
                    ins->src2, tmp_ver[ins->src2]);
            fprintf(out, "  %%t%d_%d = zext i1 %%cmp%d_%d to i64\n",
                    ins->dst, ++tmp_ver[ins->dst], ins->dst, tmp_ver[ins->dst]);
            break;
        }
        case JIR_STORE_LOCAL: {
            int vs = var_slot_get(trace, ins->name);
            if (vs >= 0 && ins->src1 >= 0) {
                fprintf(out, "  ; store %s = slot %d\n", ins->name, ins->src1);
                /* copy ins->src1 version to vs version */
                tmp_ver[vs]++;
                fprintf(out, "  %%t%d_%d = add i64 0, %%t%d_%d\n",
                        vs, tmp_ver[vs], ins->src1, tmp_ver[ins->src1]);
            }
            break;
        }
        case JIR_EXIT_IF_FALSE:
            fprintf(out, "  %%cond_exit%d = icmp ne i64 %%t%d_%d, 0\n",
                    i, ins->src1, tmp_ver[ins->src1]);
            fprintf(out, "  br i1 %%cond_exit%d, label %%continue%d, label %%loop_exit\n", i, i);
            fprintf(out, "continue%d:\n", i);
            break;
        case JIR_LOOP_BACK:
            /* Write back all variable slots and branch. */
            for (int v = 0; v < trace->var_count; v++) {
                fprintf(out, "  store i64 %%t%d_%d, i64* %%ptr_v%d\n",
                        v, tmp_ver[v], v);
            }
            fprintf(out, "  br label %%loop_head\n");
            break;
        default: break;
        }
    }

    fprintf(out, "\nloop_exit:\n");
    fprintf(out, "  ret i64 0\n");
    fprintf(out, "}\n");
}

/* ================================================================== mmap allocation */

#if JIT_HAS_BACKEND
static uint8_t *alloc_exec(size_t size) {
    void *p = mmap(NULL, size,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;
    return (uint8_t *)p;
}
static void free_exec(uint8_t *p, size_t size) {
    if (p) munmap(p, size);
}
#endif

/* ================================================================== compile trace */

static JitTrace *compile_trace(Chunk *chunk, int header_ip, int jump_ip) {
    JitTrace *trace = (JitTrace *)calloc(1, sizeof(JitTrace));
    if (!trace) return NULL;

    trace->ip        = jump_ip;
    trace->header_ip = header_ip;
    trace->exit_ip   = 0;

    /* Phase 1: record IR. */
    if (!record_trace(trace, chunk)) {
        free(trace);
        return NULL;
    }

    /* Phase 2: allocate executable buffer and compile. */
#if JIT_HAS_BACKEND
    uint8_t *code = alloc_exec(JIT_CODE_MAXBYTES);
    if (!code) { free(trace); return NULL; }

    size_t  code_size = 0;
    bool    ok        = false;

#if defined(__x86_64__) || defined(__amd64__)
    ok = x64_compile(trace, code, JIT_CODE_MAXBYTES, &code_size);
#elif defined(__aarch64__)
    ok = a64_compile(trace, code, JIT_CODE_MAXBYTES, &code_size);
#endif

    if (!ok || code_size == 0) {
        free_exec(code, JIT_CODE_MAXBYTES);
        free(trace);
        return NULL;
    }

    /* Make code read-only + executable (good security practice). */
    mprotect(code, code_size, PROT_READ | PROT_EXEC);

    trace->code      = code;
    trace->code_size = code_size;
    trace->fn        = (JitFn)code;
#else
    /* No native backend: mark as non-JIT-able. */
    free(trace);
    return NULL;
#endif

    return trace;
}

/* ================================================================== public API */

JIT *jit_new(void) {
    JIT *jit = (JIT *)calloc(1, sizeof(JIT));
    if (jit) jit->enabled = true;
    return jit;
}

void jit_free(JIT *jit) {
    if (!jit) return;
    for (int i = 0; i < JIT_CACHE_CAP; i++) {
        JitTrace *t = jit->cache[i];
        while (t) {
            JitTrace *next = t->next;
#if JIT_HAS_BACKEND
            if (t->code) free_exec(t->code, JIT_CODE_MAXBYTES);
#endif
            free(t);
            t = next;
        }
    }
    free(jit);
}

JitTrace *jit_on_backward_jump(JIT *jit, VM *vm, Env *env,
                                int jump_ip, int header_ip, Chunk *chunk) {
    (void)vm; (void)env;
    if (!jit || !jit->enabled) return NULL;

    int count = hot_increment(jit, jump_ip);
    if (count < JIT_HOT_THRESHOLD) return NULL;
    if (count >= INT_MAX / 2)      return NULL; /* blacklisted */

    /* Already compiled? */
    JitTrace *cached = cache_get(jit, jump_ip);
    if (cached) return cached;

    /* First time crossing threshold: compile. */
    if (count > JIT_HOT_THRESHOLD) {
        /* Compilation already attempted and failed (count was bumped past threshold
         * without producing a trace); don't retry. */
        return NULL;
    }

    JitTrace *trace = compile_trace(chunk, header_ip, jump_ip);
    if (!trace) {
        hot_blacklist(jit, jump_ip);
        return NULL;
    }

    cache_put(jit, trace);
    jit->traces_compiled++;
    return trace;
}

int jit_execute(JitTrace *trace, VM *vm, Env *env) {
    (void)vm;
    if (!trace || !trace->fn) return JIT_EXIT_GUARD_FAIL;

    /* Build register file from environment. */
    long long regs[JIT_MAX_REGS];
    memset(regs, 0, sizeof(regs));

    for (int i = 0; i < trace->var_count; i++) {
        Value *v = env_get(env, trace->vars[i]);
        if (!v || v->type != VAL_INT) {
            trace->guard_fails++;
            return JIT_EXIT_GUARD_FAIL;
        }
        regs[i] = v->int_val;
    }

    /* Call the native trace function. */
    int result = trace->fn(regs);
    trace->exec_count++;

    if (result == JIT_EXIT_GUARD_FAIL) {
        trace->guard_fails++;
        return JIT_EXIT_GUARD_FAIL;
    }

    /* Write back all traced variable values to the environment. */
    for (int i = 0; i < trace->var_count; i++) {
        Value *nv = value_int(regs[i]);
        env_assign(env, trace->vars[i], nv);
        value_release(nv);
    }

    return result;
}

void jit_print_stats(JIT *jit) {
    if (!jit) return;
    fprintf(stderr, "[JIT] traces compiled : %zu\n",  jit->traces_compiled);
    fprintf(stderr, "[JIT] traces executed : %zu\n",  jit->traces_executed);
    fprintf(stderr, "[JIT] guard exits     : %zu\n",  jit->guard_exits);
    size_t total_exec = 0;
    for (int i = 0; i < JIT_CACHE_CAP; i++)
        for (JitTrace *t = jit->cache[i]; t; t = t->next)
            total_exec += t->exec_count;
    fprintf(stderr, "[JIT] total trace iters: %zu\n", total_exec);
}

void jit_dump_ir(JitTrace *trace) {
    static const char *op_names[] = {
        "NOP","LOAD_INT","LOAD_FLOAT","LOAD_LOCAL","STORE_LOCAL",
        "INT_ADD","INT_SUB","INT_MUL","INT_DIV","INT_MOD","INT_NEG",
        "FLOAT_ADD","FLOAT_SUB","FLOAT_MUL","FLOAT_DIV",
        "CMP_LT","CMP_LE","CMP_GT","CMP_GE","CMP_EQ","CMP_NE",
        "EXIT_IF_FALSE","LOOP_BACK",
    };
    fprintf(stderr, "[JIT] IR dump for backward-jump at ip=%d (header=%d exit=%d):\n",
            trace->ip, trace->header_ip, trace->exit_ip);
    for (int i = 0; i < trace->var_count; i++)
        fprintf(stderr, "  var[%d] = '%s'\n", i, trace->vars[i]);
    for (int i = 0; i < trace->ir_count; i++) {
        JIRInstr *ins = &trace->ir[i];
        const char *nm = (ins->op < (int)(sizeof(op_names)/sizeof(op_names[0])))
                         ? op_names[ins->op] : "?";
        if (ins->op == JIR_LOAD_INT)
            fprintf(stderr, "  %3d  %-16s  r%-2d = %lld\n", i, nm, ins->dst, (long long)ins->imm);
        else if (ins->op == JIR_LOAD_LOCAL || ins->op == JIR_STORE_LOCAL)
            fprintf(stderr, "  %3d  %-16s  r%-2d  '%s'\n", i, nm, ins->dst >= 0 ? ins->dst : ins->src1, ins->name);
        else if (ins->op == JIR_EXIT_IF_FALSE)
            fprintf(stderr, "  %3d  %-16s  r%d\n", i, nm, ins->src1);
        else if (ins->src2 >= 0)
            fprintf(stderr, "  %3d  %-16s  r%-2d = r%d, r%d\n", i, nm, ins->dst, ins->src1, ins->src2);
        else if (ins->src1 >= 0)
            fprintf(stderr, "  %3d  %-16s  r%-2d = r%d\n", i, nm, ins->dst, ins->src1);
        else
            fprintf(stderr, "  %3d  %s\n", i, nm);
    }
}
