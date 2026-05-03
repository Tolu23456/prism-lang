#ifndef OPCODE_H
#define OPCODE_H

/* Prism bytecode opcodes.
 * Instruction sizes:
 *   1 byte  — no operand
 *   3 bytes — opcode + uint16_t operand
 *   5 bytes — opcode + two uint16_t operands (CALL_METHOD)
 *             or opcode + int32_t operand (JUMP_WIDE variants)
 */
typedef enum {
    /* ── constants & literals ──────────────────────────────── */
    OP_PUSH_NULL,           /* push null singleton                  */
    OP_PUSH_TRUE,           /* push true singleton                  */
    OP_PUSH_FALSE,          /* push false singleton                 */
    OP_PUSH_UNKNOWN,        /* push unknown (bool -1) singleton     */
    OP_PUSH_CONST,          /* push constants[operand]              */
    OP_PUSH_INT_IMM,        /* push small int (signed 16-bit imm)   */

    /* ── stack ─────────────────────────────────────────────── */
    OP_POP,                 /* discard top                          */
    OP_DUP,                 /* duplicate top                        */
    OP_POP_N,               /* discard top N (operand) items        */

    /* ── name-based variables (Env hash-map lookup) ────────── */
    OP_LOAD_NAME,           /* push env_get(constants[operand])     */
    OP_STORE_NAME,          /* env_assign(constants[op], top)       */
    OP_DEFINE_NAME,         /* env_set(constants[op], pop(), false) */
    OP_DEFINE_CONST,        /* env_set(constants[op], pop(), true)  */

    /* ── local variable slots (flat array, O(1)) ───────────── */
    OP_LOAD_LOCAL,          /* push frame->locals[operand]          */
    OP_STORE_LOCAL,         /* frame->locals[operand] = pop()       */
    OP_DEFINE_LOCAL,        /* frame->locals[operand] = pop()       */
    OP_INC_LOCAL,           /* frame->locals[operand] += 1          */
    OP_DEC_LOCAL,           /* frame->locals[operand] -= 1          */

    /* ── generic arithmetic (type-dispatched) ──────────────── */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_NEG,
    OP_POS,
    OP_IDIV,                /* floor integer division (//)          */

    /* ── specialized integer arithmetic (no type-check) ────── */
    OP_ADD_INT,
    OP_SUB_INT,
    OP_MUL_INT,
    OP_DIV_INT,
    OP_MOD_INT,
    OP_NEG_INT,

    /* ── bitwise ────────────────────────────────────────────── */
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_LSHIFT,
    OP_RSHIFT,

    /* ── generic comparison ─────────────────────────────────── */
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,

    /* ── specialized integer comparison (no type-check) ────── */
    OP_LT_INT,
    OP_LE_INT,
    OP_GT_INT,
    OP_GE_INT,
    OP_EQ_INT,
    OP_NE_INT,

    /* ── membership ─────────────────────────────────────────── */
    OP_IN,
    OP_NOT_IN,

    /* ── logical ────────────────────────────────────────────── */
    OP_NOT,

    /* ── control flow (16-bit signed offset) ───────────────── */
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_JUMP_IF_FALSE_PEEK,  /* short-circuit: don't pop              */
    OP_JUMP_IF_TRUE_PEEK,

    /* ── wide control flow (32-bit signed offset) ──────────── */
    OP_JUMP_WIDE,
    OP_JUMP_IF_FALSE_WIDE,
    OP_JUMP_IF_TRUE_WIDE,

    /* ── scope ──────────────────────────────────────────────── */
    OP_PUSH_SCOPE,
    OP_POP_SCOPE,

    /* ── collections ────────────────────────────────────────── */
    OP_MAKE_ARRAY,          /* pop N (operand) items → array        */
    OP_MAKE_DICT,           /* pop N*2 items (k,v pairs) → dict     */
    OP_MAKE_SET,            /* pop N items → set                    */
    OP_MAKE_TUPLE,          /* pop N items → tuple                  */
    OP_MAKE_RANGE,          /* pop start,stop[,step] → array range  */

    /* ── indexing / member access ───────────────────────────── */
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_GET_ATTR,            /* operand = name constant index        */
    OP_SET_ATTR,
    OP_SAFE_GET_ATTR,       /* ?. safe attribute — push null if missing */
    OP_SAFE_GET_INDEX,      /* ?[] safe index — push null if missing    */

    /* ── slicing ────────────────────────────────────────────── */
    OP_SLICE,               /* start,stop,step,obj → slice         */

    /* ── functions ──────────────────────────────────────────── */
    OP_MAKE_FUNCTION,       /* constants[operand] → closure          */
    OP_CALL,                /* operand=argc; callee below args       */
    OP_CALL_METHOD,         /* 5-byte: name_idx(2) argc(2)          */
    OP_RETURN,
    OP_RETURN_NULL,
    OP_TAIL_CALL,           /* tail-call optimized variant of CALL  */

    /* ── iteration ──────────────────────────────────────────── */
    OP_GET_ITER,            /* convert top to iterable array         */
    OP_FOR_ITER,            /* advance iter; jump-if-done (operand)  */

    /* ── string construction ────────────────────────────────── */
    OP_BUILD_FSTRING,       /* pop N segments → interpolated string  */

    /* ── module system ──────────────────────────────────────── */
    OP_IMPORT,              /* operand = module path const index     */
    OP_LINK_STYLE,          /* operand = PSS path const index        */

    /* ── type checking ──────────────────────────────────────── */
    OP_IS_TYPE,             /* operand = type name const index       */
    OP_MATCH_TYPE,

    /* ── null safety & pipe ─────────────────────────────────── */
    OP_NULL_COAL,           /* ?? operator: use rhs if lhs is null   */
    OP_PIPE,                /* |> pipe operator                      */

    /* ── expect / assert ────────────────────────────────────── */
    OP_EXPECT,              /* operand = message const idx; assert top truthy */

    /* ── exception handling ──────────────────────────────────── */
    OP_TRY_BEGIN,           /* operand = signed16 offset to catch handler */
    OP_TRY_END,             /* pop the innermost try frame           */
    OP_THROW,               /* pop value, throw as exception         */

    /* ── halt ───────────────────────────────────────────────── */
    OP_HALT,

    OP_COUNT_               /* sentinel — must be last               */
} Opcode;

/* ── has_operand predicate ────────────────────────────────────── */
static inline int opcode_has_operand(Opcode op) {
    switch (op) {
        case OP_PUSH_CONST:
        case OP_PUSH_INT_IMM:
        case OP_LOAD_NAME:     case OP_STORE_NAME:
        case OP_DEFINE_NAME:   case OP_DEFINE_CONST:
        case OP_LOAD_LOCAL:    case OP_STORE_LOCAL:  case OP_DEFINE_LOCAL:
        case OP_INC_LOCAL:     case OP_DEC_LOCAL:
        case OP_JUMP:          case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_FALSE_PEEK: case OP_JUMP_IF_TRUE_PEEK:
        case OP_MAKE_ARRAY:    case OP_MAKE_DICT:    case OP_MAKE_SET:
        case OP_MAKE_TUPLE:    case OP_MAKE_RANGE:
        case OP_GET_ATTR:      case OP_SET_ATTR:     case OP_SAFE_GET_ATTR:
        case OP_POP_N:         case OP_CALL:         case OP_MAKE_FUNCTION:
        case OP_FOR_ITER:      case OP_BUILD_FSTRING:
        case OP_IMPORT:        case OP_LINK_STYLE:
        case OP_IS_TYPE:       case OP_MATCH_TYPE:
        case OP_EXPECT:        case OP_TAIL_CALL:
        case OP_TRY_BEGIN:
            return 1;
        default:
            return 0;
    }
}

/* ── opcode name (for disassembly / diagnostics) ─────────────── */
static inline const char *opcode_name(Opcode op) {
    switch (op) {
        case OP_PUSH_NULL:           return "PUSH_NULL";
        case OP_PUSH_TRUE:           return "PUSH_TRUE";
        case OP_PUSH_FALSE:          return "PUSH_FALSE";
        case OP_PUSH_UNKNOWN:        return "PUSH_UNKNOWN";
        case OP_PUSH_CONST:          return "PUSH_CONST";
        case OP_PUSH_INT_IMM:        return "PUSH_INT_IMM";
        case OP_POP:                 return "POP";
        case OP_DUP:                 return "DUP";
        case OP_POP_N:               return "POP_N";
        case OP_LOAD_NAME:           return "LOAD_NAME";
        case OP_STORE_NAME:          return "STORE_NAME";
        case OP_DEFINE_NAME:         return "DEFINE_NAME";
        case OP_DEFINE_CONST:        return "DEFINE_CONST";
        case OP_LOAD_LOCAL:          return "LOAD_LOCAL";
        case OP_STORE_LOCAL:         return "STORE_LOCAL";
        case OP_DEFINE_LOCAL:        return "DEFINE_LOCAL";
        case OP_INC_LOCAL:           return "INC_LOCAL";
        case OP_DEC_LOCAL:           return "DEC_LOCAL";
        case OP_ADD:                 return "ADD";
        case OP_SUB:                 return "SUB";
        case OP_MUL:                 return "MUL";
        case OP_DIV:                 return "DIV";
        case OP_MOD:                 return "MOD";
        case OP_POW:                 return "POW";
        case OP_NEG:                 return "NEG";
        case OP_POS:                 return "POS";
        case OP_IDIV:                return "IDIV";
        case OP_ADD_INT:             return "ADD_INT";
        case OP_SUB_INT:             return "SUB_INT";
        case OP_MUL_INT:             return "MUL_INT";
        case OP_DIV_INT:             return "DIV_INT";
        case OP_MOD_INT:             return "MOD_INT";
        case OP_NEG_INT:             return "NEG_INT";
        case OP_BIT_AND:             return "BIT_AND";
        case OP_BIT_OR:              return "BIT_OR";
        case OP_BIT_XOR:             return "BIT_XOR";
        case OP_BIT_NOT:             return "BIT_NOT";
        case OP_LSHIFT:              return "LSHIFT";
        case OP_RSHIFT:              return "RSHIFT";
        case OP_EQ:                  return "EQ";
        case OP_NE:                  return "NE";
        case OP_LT:                  return "LT";
        case OP_LE:                  return "LE";
        case OP_GT:                  return "GT";
        case OP_GE:                  return "GE";
        case OP_LT_INT:              return "LT_INT";
        case OP_LE_INT:              return "LE_INT";
        case OP_GT_INT:              return "GT_INT";
        case OP_GE_INT:              return "GE_INT";
        case OP_EQ_INT:              return "EQ_INT";
        case OP_NE_INT:              return "NE_INT";
        case OP_IN:                  return "IN";
        case OP_NOT_IN:              return "NOT_IN";
        case OP_NOT:                 return "NOT";
        case OP_JUMP:                return "JUMP";
        case OP_JUMP_IF_FALSE:       return "JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE:        return "JUMP_IF_TRUE";
        case OP_JUMP_IF_FALSE_PEEK:  return "JUMP_IF_FALSE_PEEK";
        case OP_JUMP_IF_TRUE_PEEK:   return "JUMP_IF_TRUE_PEEK";
        case OP_JUMP_WIDE:           return "JUMP_WIDE";
        case OP_JUMP_IF_FALSE_WIDE:  return "JUMP_IF_FALSE_WIDE";
        case OP_JUMP_IF_TRUE_WIDE:   return "JUMP_IF_TRUE_WIDE";
        case OP_PUSH_SCOPE:          return "PUSH_SCOPE";
        case OP_POP_SCOPE:           return "POP_SCOPE";
        case OP_MAKE_ARRAY:          return "MAKE_ARRAY";
        case OP_MAKE_DICT:           return "MAKE_DICT";
        case OP_MAKE_SET:            return "MAKE_SET";
        case OP_MAKE_TUPLE:          return "MAKE_TUPLE";
        case OP_MAKE_RANGE:          return "MAKE_RANGE";
        case OP_GET_INDEX:           return "GET_INDEX";
        case OP_SET_INDEX:           return "SET_INDEX";
        case OP_GET_ATTR:            return "GET_ATTR";
        case OP_SET_ATTR:            return "SET_ATTR";
        case OP_SAFE_GET_ATTR:       return "SAFE_GET_ATTR";
        case OP_SAFE_GET_INDEX:      return "SAFE_GET_INDEX";
        case OP_SLICE:               return "SLICE";
        case OP_MAKE_FUNCTION:       return "MAKE_FUNCTION";
        case OP_CALL:                return "CALL";
        case OP_CALL_METHOD:         return "CALL_METHOD";
        case OP_RETURN:              return "RETURN";
        case OP_RETURN_NULL:         return "RETURN_NULL";
        case OP_TAIL_CALL:           return "TAIL_CALL";
        case OP_GET_ITER:            return "GET_ITER";
        case OP_FOR_ITER:            return "FOR_ITER";
        case OP_BUILD_FSTRING:       return "BUILD_FSTRING";
        case OP_IMPORT:              return "IMPORT";
        case OP_LINK_STYLE:          return "LINK_STYLE";
        case OP_IS_TYPE:             return "IS_TYPE";
        case OP_MATCH_TYPE:          return "MATCH_TYPE";
        case OP_NULL_COAL:           return "NULL_COAL";
        case OP_PIPE:                return "PIPE";
        case OP_EXPECT:              return "EXPECT";
        case OP_TRY_BEGIN:           return "TRY_BEGIN";
        case OP_TRY_END:             return "TRY_END";
        case OP_THROW:               return "THROW";
        case OP_HALT:                return "HALT";
        default:                     return "UNKNOWN";
    }
}

#endif /* OPCODE_H */
