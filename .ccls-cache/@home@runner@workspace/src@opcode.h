#ifndef OPCODE_H
#define OPCODE_H

/* Prism bytecode opcodes.
 * Instructions are either:
 *   1 byte  (no operand)
 *   3 bytes (1-byte opcode + uint16_t operand)
 *   5 bytes (1-byte opcode + two uint16_t operands) — CALL_METHOD only
 */
typedef enum {
    /* --- constants & literals --- */
    OP_PUSH_NULL,      /* push null */
    OP_PUSH_TRUE,      /* push true */
    OP_PUSH_FALSE,     /* push false */
    OP_PUSH_UNKNOWN,   /* push unknown (bool -1) */
    OP_PUSH_CONST,     /* push constants[operand] */

    /* --- stack --- */
    OP_POP,            /* discard top */
    OP_DUP,            /* duplicate top */
    OP_POP_N,          /* discard top N items */

    /* --- variables (name-based, Env lookup) --- */
    OP_LOAD_NAME,      /* push env_get(constants[operand]) */
    OP_STORE_NAME,     /* env_assign(constants[operand], top); error if undeclared */
    OP_DEFINE_NAME,    /* env_set(constants[operand], pop(), false) */
    OP_DEFINE_CONST,   /* env_set(constants[operand], pop(), true) */

    /* --- arithmetic --- */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_NEG, OP_POS,

    /* --- bitwise --- */
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT,
    OP_LSHIFT, OP_RSHIFT,

    /* --- comparison --- */
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,

    /* --- membership --- */
    OP_IN, OP_NOT_IN,

    /* --- logical --- */
    OP_NOT,

    /* --- control flow (operand = signed 16-bit offset from AFTER operand) --- */
    OP_JUMP,            /* unconditional */
    OP_JUMP_IF_FALSE,   /* pop & jump if falsy */
    OP_JUMP_IF_TRUE,    /* pop & jump if truthy */
    OP_JUMP_IF_FALSE_PEEK, /* jump if falsy WITHOUT popping (short-circuit) */
    OP_JUMP_IF_TRUE_PEEK,  /* jump if truthy WITHOUT popping (short-circuit) */

    /* --- scope --- */
    OP_PUSH_SCOPE,     /* create child Env */
    OP_POP_SCOPE,      /* restore parent Env */

    /* --- collections --- */
    OP_MAKE_ARRAY,     /* pop operand items → array */
    OP_MAKE_DICT,      /* pop operand*2 items (k,v pairs) → dict */
    OP_MAKE_SET,       /* pop operand items → set */
    OP_MAKE_TUPLE,     /* pop operand items → tuple */

    /* --- indexing / member --- */
    OP_GET_INDEX,      /* obj=pop, idx=pop → push obj[idx] */
    OP_SET_INDEX,      /* val=pop, obj=pop_next, idx=pop → obj[idx]=val */
    OP_GET_ATTR,       /* obj=pop → push obj.constants[operand] */
    OP_SET_ATTR,       /* val=pop, obj=pop → obj.constants[operand]=val */

    /* --- slicing --- */
    OP_SLICE,          /* step=pop, stop=pop, start=pop, obj=pop → push slice */

    /* --- functions --- */
    OP_MAKE_FUNCTION,  /* push function value stored in constants[operand] */
    OP_CALL,           /* operand=argc; callee below args on stack */
    OP_CALL_METHOD,    /* 5-byte: name_idx(2B) argc(2B); obj below args on stack */
    OP_RETURN,         /* return top of stack */
    OP_RETURN_NULL,    /* return null */

    /* --- iteration (for-in) --- */
    OP_GET_ITER,       /* convert top to flat Value* array wrapped as VAL_ARRAY */
    OP_FOR_ITER,       /* operand=jump-offset-if-done; expects [arr,idx] on stack */

    /* --- f-string --- */
    OP_BUILD_FSTRING,  /* pop operand segments, join as string */

    /* --- import --- */
    OP_IMPORT,         /* operand = path constant index */

    /* --- halt --- */
    OP_HALT,
} Opcode;

/* Returns whether this opcode has a uint16_t operand (3 bytes total).
 * OP_CALL_METHOD has TWO uint16_t operands (5 bytes total) — handled separately. */
static inline int opcode_has_operand(Opcode op) {
    switch (op) {
        case OP_PUSH_CONST:
        case OP_LOAD_NAME: case OP_STORE_NAME:
        case OP_DEFINE_NAME: case OP_DEFINE_CONST:
        case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_FALSE_PEEK: case OP_JUMP_IF_TRUE_PEEK:
        case OP_MAKE_ARRAY: case OP_MAKE_DICT: case OP_MAKE_SET: case OP_MAKE_TUPLE:
        case OP_GET_ATTR: case OP_SET_ATTR:
        case OP_POP_N: case OP_CALL: case OP_MAKE_FUNCTION:
        case OP_FOR_ITER: case OP_BUILD_FSTRING: case OP_IMPORT:
            return 1;
        default:
            return 0;
    }
}

#endif /* OPCODE_H */
