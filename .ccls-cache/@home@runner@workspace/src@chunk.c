#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
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

static int write_u8(FILE *f, uint8_t v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? 0 : 1;
}

static int write_u32(FILE *f, uint32_t v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? 0 : 1;
}

static int write_i64(FILE *f, long long v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? 0 : 1;
}

static int write_double(FILE *f, double v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? 0 : 1;
}

static int write_string(FILE *f, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    if (write_u32(f, len)) return 1;
    return len > 0 && fwrite(s, 1, len, f) != len;
}

int chunk_write_bytecode(Chunk *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    const unsigned char magic[10] = {'P','R','I','S','M','P','M','C','1','\0'};
    int failed = fwrite(magic, 1, sizeof(magic), f) != sizeof(magic);
    failed = failed || write_u32(f, (uint32_t)c->count);
    if (!failed && c->count > 0)
        failed = fwrite(c->code, 1, c->count, f) != (size_t)c->count;

    failed = failed || write_u32(f, (uint32_t)c->const_count);
    for (int i = 0; !failed && i < c->const_count; i++) {
        Value *v = c->constants[i];
        failed = failed || write_u8(f, (uint8_t)v->type);
        switch (v->type) {
            case VAL_INT:
                failed = failed || write_i64(f, v->int_val);
                break;
            case VAL_FLOAT:
                failed = failed || write_double(f, v->float_val);
                break;
            case VAL_COMPLEX:
                failed = failed || write_double(f, v->complex_val.real);
                failed = failed || write_double(f, v->complex_val.imag);
                break;
            case VAL_STRING:
                failed = failed || write_string(f, v->str_val);
                break;
            case VAL_BOOL:
                failed = failed || write_i64(f, v->bool_val);
                break;
            case VAL_FUNCTION:
                failed = failed || write_string(f, v->func.name);
                failed = failed || write_u32(f, v->func.chunk ? (uint32_t)v->func.chunk->count : 0);
                if (!failed && v->func.chunk && v->func.chunk->count > 0)
                    failed = fwrite(v->func.chunk->code, 1, v->func.chunk->count, f) != (size_t)v->func.chunk->count;
                break;
            default:
                break;
        }
    }

    if (fclose(f) != 0) failed = 1;
    return failed;
}
