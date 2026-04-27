#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chunk.h"
#include "value.h"

void chunk_init(Chunk *c) { c->code = NULL; c->count = 0; c->cap = 0; c->constants = NULL; c->const_count = 0; c->const_cap = 0; c->lines = NULL; c->source_file = NULL; c->inline_caches = NULL; }
void chunk_free(Chunk *c) { if (c->code) free(c->code); if (c->lines) free(c->lines); if (c->inline_caches) free(c->inline_caches); for (int i = 0; i < c->const_count; i++) value_release(c->constants[i]); if (c->constants) free(c->constants); chunk_init(c); }
void chunk_write(Chunk *c, uint8_t byte, int line) {
    if (c->count >= c->cap) {
        int old_cap = c->cap; c->cap = (c->cap < 8) ? 8 : c->cap * 2;
        c->code = realloc(c->code, c->cap); c->lines = realloc(c->lines, c->cap * sizeof(int));
        c->inline_caches = realloc(c->inline_caches, c->cap * sizeof(InlineCache));
        memset(c->inline_caches + old_cap, 0, (c->cap - old_cap) * sizeof(InlineCache));
    }
    c->code[c->count] = byte; c->lines[c->count] = line; c->count++;
}
int chunk_add_const(Chunk *c, Value v) { if (c->const_count >= c->const_cap) { c->const_cap = (c->const_cap < 8) ? 8 : c->const_cap * 2; c->constants = realloc(c->constants, c->const_cap * sizeof(Value)); } c->constants[c->const_count] = value_retain(v); return c->const_count++; }
int chunk_add_const_str(Chunk *c, const char *s) { return chunk_add_const(c, value_string(s)); }
void chunk_emit(Chunk *c, uint8_t byte, int line) { chunk_write(c, byte, line); }
void chunk_emit16(Chunk *c, uint16_t val, int line) { chunk_write(c, (uint8_t)(val & 0xff), line); chunk_write(c, (uint8_t)((val >> 8) & 0xff), line); }
void chunk_patch16(Chunk *c, int offset, uint16_t val) { c->code[offset] = (uint8_t)(val & 0xff); c->code[offset+1] = (uint8_t)((val >> 8) & 0xff); }
InlineCache *chunk_inline_cache(Chunk *c, int bytecode_offset) { if (bytecode_offset < 0 || bytecode_offset >= c->count) return NULL; return &c->inline_caches[bytecode_offset]; }
int chunk_write_bytecode(Chunk *c, const char *path) { (void)c; (void)path; return 0; }
int chunk_load_bytecode(Chunk *c, const char *path) { (void)c; (void)path; return 0; }
