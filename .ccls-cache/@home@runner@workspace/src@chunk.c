#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "chunk.h"
#include "value.h"

void chunk_init(Chunk *c) {
    c->code        = NULL; c->count = 0; c->cap = 0;
    c->constants   = NULL; c->const_count = 0; c->const_cap = 0;
    c->lines       = NULL;
}

void chunk_free(Chunk *c) {
    for (int i = 0; i < c->const_count; i++) value_release(c->constants[i]);
    free(c->code);
    free(c->constants);
    free(c->lines);
    chunk_init(c);
}

void chunk_emit(Chunk *c, uint8_t byte, int line) {
    if (c->count >= c->cap) {
        c->cap   = c->cap < 8 ? 8 : c->cap * 2;
        c->code  = realloc(c->code,  c->cap * sizeof(uint8_t));
        c->lines = realloc(c->lines, c->cap * sizeof(int));
    }
    c->code[c->count]  = byte;
    c->lines[c->count] = line;
    c->count++;
}

void chunk_emit16(Chunk *c, uint16_t val, int line) {
    chunk_emit(c, (uint8_t)(val & 0xFF),        line);
    chunk_emit(c, (uint8_t)((val >> 8) & 0xFF), line);
}

int chunk_add_const(Chunk *c, Value *v) {
    if (c->const_count >= c->const_cap) {
        c->const_cap  = c->const_cap < 8 ? 8 : c->const_cap * 2;
        c->constants  = realloc(c->constants, c->const_cap * sizeof(Value *));
    }
    c->constants[c->const_count] = value_retain(v);
    return c->const_count++;
}

int chunk_add_const_str(Chunk *c, const char *s) {
    Value *v = value_string(s);
    int idx  = chunk_add_const(c, v);
    value_release(v);
    return idx;
}

void chunk_patch16(Chunk *c, int off, uint16_t val) {
    c->code[off]     = (uint8_t)(val & 0xFF);
    c->code[off + 1] = (uint8_t)((val >> 8) & 0xFF);
}
