#ifndef CHUNK_H
#define CHUNK_H

#include <stdint.h>
#include "value.h"
#include "opcode.h"

/* A Chunk holds a compiled bytecode sequence + constant pool + line info. */
typedef struct Chunk {
    uint8_t *code;
    int      count;
    int      cap;

    Value  **constants;
    int      const_count;
    int      const_cap;

    int     *lines;   /* parallel to code: source line for each byte */
} Chunk;

void     chunk_init(Chunk *c);
void     chunk_free(Chunk *c);

/* Emit one byte. */
void     chunk_emit(Chunk *c, uint8_t byte, int line);

/* Emit a uint16_t operand (little-endian, 2 bytes). */
void     chunk_emit16(Chunk *c, uint16_t val, int line);

/* Add a constant to the pool; returns its index. */
int      chunk_add_const(Chunk *c, Value *v);

/* Emit OP_PUSH_CONST for a Value (adds to pool). */
int      chunk_add_const_str(Chunk *c, const char *s);

/* Patch a uint16_t at offset `off` (for backpatching jumps). */
void     chunk_patch16(Chunk *c, int off, uint16_t val);

#endif /* CHUNK_H */
