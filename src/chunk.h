#ifndef CHUNK_H
#define CHUNK_H
#include <stdint.h>
#include "value.h"

typedef enum {
    VM_METHOD_UNKNOWN = 0, VM_METHOD_STRING_UPPER, VM_METHOD_STRING_LOWER, VM_METHOD_STRING_STRIP, VM_METHOD_STRING_LSTRIP, VM_METHOD_STRING_RSTRIP,
    VM_METHOD_STRING_LEN, VM_METHOD_STRING_CAPITALIZE, VM_METHOD_STRING_FIND, VM_METHOD_STRING_REPLACE, VM_METHOD_STRING_STARTSWITH,
    VM_METHOD_STRING_ENDSWITH, VM_METHOD_STRING_SPLIT, VM_METHOD_STRING_JOIN, VM_METHOD_STRING_ISDIGIT, VM_METHOD_STRING_ISALPHA,
    VM_METHOD_ARRAY_ADD, VM_METHOD_ARRAY_POP, VM_METHOD_ARRAY_SORT, VM_METHOD_ARRAY_INSERT, VM_METHOD_ARRAY_REMOVE,
    VM_METHOD_ARRAY_EXTEND, VM_METHOD_ARRAY_LEN, VM_METHOD_DICT_KEYS, VM_METHOD_DICT_VALUES, VM_METHOD_DICT_ITEMS,
    VM_METHOD_DICT_ERASE, VM_METHOD_DICT_GET, VM_METHOD_SET_ADD, VM_METHOD_SET_REMOVE, VM_METHOD_SET_DISCARD,
    VM_METHOD_SET_UPDATE, VM_METHOD_TUPLE_COUNT, VM_METHOD_TUPLE_INDEX,
} VmMethodId;

/* Forward-declare Env so InlineCache can hold a name-lookup cache pointer.
 * Full definition lives in interpreter.h. */
struct Env;

typedef struct {
    uint8_t opcode; uint16_t name_idx; ValueType receiver_type; int dict_index; unsigned int dict_version; VmMethodId method_id;
    /* Name-lookup inline cache: populated by OP_LOAD_NAME / OP_STORE_NAME after
     * the first hash-table miss so subsequent accesses hit the slot directly.   */
    struct Env *name_env;   /* env in which the name was found       */
    int         name_slot;  /* slot index inside name_env->slots[]   */
} InlineCache;

typedef struct Chunk {
    uint8_t *code; int count; int cap; Value *constants; int const_count; int const_cap; int *lines; const char *source_file; InlineCache *inline_caches;
    /* Set by the compiler when the function body never emits OP_DEFINE_NAME,
     * OP_DEFINE_CONST, or OP_MAKE_FUNCTION.  The VM can then skip env_new()
     * on every call and use the closure directly as frame->env.              */
    uint8_t no_env;
} Chunk;

void chunk_init(Chunk *c);
void chunk_free(Chunk *c);
void chunk_write(Chunk *c, uint8_t byte, int line);
int chunk_add_const(Chunk *c, Value v);
int chunk_add_const_str(Chunk *c, const char *s);
void chunk_emit(Chunk *c, uint8_t byte, int line);
void chunk_emit16(Chunk *c, uint16_t val, int line);
void chunk_patch16(Chunk *c, int offset, uint16_t val);
int chunk_write_bytecode(Chunk *c, const char *path);
int chunk_load_bytecode(Chunk *c, const char *path);
InlineCache *chunk_inline_cache(Chunk *c, int bytecode_offset);
#endif
