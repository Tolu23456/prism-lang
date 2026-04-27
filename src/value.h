#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "ast.h"

typedef enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_COMPLEX,
    VAL_STRING,
    VAL_BOOL,
    VAL_ARRAY,
    VAL_DICT,
    VAL_SET,
    VAL_TUPLE,
    VAL_FUNCTION,
    VAL_BUILTIN,
} ValueType;

typedef uintptr_t Value;
typedef struct ValueStruct ValueStruct;
typedef struct Env   Env;
typedef struct Chunk Chunk;

typedef Value (*BuiltinFn)(Value *args, int argc);

/*
 * Immediate Tagging System (Value is uintptr_t)
 * - Bit 0 = 1: Tagged Integer (63-bit signed)
 * - Bit 1,0 = 1,0 (0x2): Special immediate (Null, Bool)
 * - Bit 1,0 = 0,0: Pointer to ValueStruct (must be aligned)
 */

#define IS_INT(v)      (((v) & 0x1))
#define IS_PTR(v)      (!((v) & 0x3) && (v) != 0)

#define VAL_SPEC_NULL    ((Value)0x02)
#define VAL_SPEC_FALSE   ((Value)0x06)
#define VAL_SPEC_TRUE    ((Value)0x0A)
#define VAL_SPEC_UNKNOWN ((Value)0x0E)

#define IS_NULL(v)     ((v) == VAL_SPEC_NULL || (v) == 0)
#define IS_BOOL(v)     ((v) == VAL_SPEC_FALSE || (v) == VAL_SPEC_TRUE || (v) == VAL_SPEC_UNKNOWN)

#define TO_INT(n)      ((Value)((((uintptr_t)(n)) << 1) | 0x1))
#define AS_INT(v)      ((long long)(((intptr_t)(v)) >> 1))

#define TO_BOOL(b)     ((b) > 0 ? VAL_SPEC_TRUE : ((b) == 0 ? VAL_SPEC_FALSE : VAL_SPEC_UNKNOWN))
#define AS_BOOL(v)     ((v) == VAL_SPEC_TRUE ? 1 : ((v) == VAL_SPEC_FALSE ? 0 : -1))

#define AS_PTR(v)      ((ValueStruct*)(v))

typedef struct {
    Value *items;
    int    len;
    int    cap;
} ValueArray;

typedef struct {
    Value key;
    Value val;
} DictEntry;

typedef struct {
    DictEntry *entries;
    int        len;
    int        cap;
    unsigned int version;
} ValueDict;

struct ValueStruct {
    ValueType type;
    int       ref_count;
    unsigned char gc_marked;
    unsigned char gc_immortal;
    unsigned char gc_generation;
    struct ValueStruct *gc_next;
    struct ValueStruct *gc_prev;

    union {
        double     float_val;
        struct { double real; double imag; } complex_val;
        char      *str_val;
        ValueArray array;
        ValueDict  dict;
        ValueArray set;
        ValueArray tuple;

        struct {
            char      *name;
            Param     *params;
            int        param_count;
            ASTNode   *body;
            Env       *closure;
            Chunk     *chunk;
            bool     owns_chunk;
            bool     owns_params;
        } func;

        struct {
            char      *name;
            BuiltinFn  fn;
        } builtin;
    };
};

static inline ValueType VAL_TYPE(Value v) {
    if (IS_INT(v)) return VAL_INT;
    if (IS_NULL(v)) return VAL_NULL;
    if (IS_BOOL(v)) return VAL_BOOL;
    return AS_PTR(v)->type;
}

#define AS_FLOAT(v)    (AS_PTR(v)->float_val)
#define AS_STR(v)      (AS_PTR(v)->str_val)
#define AS_ARRAY(v)    (AS_PTR(v)->array)
#define AS_DICT(v)     (AS_PTR(v)->dict)
#define AS_SET(v)      (AS_PTR(v)->set)
#define AS_TUPLE(v)    (AS_PTR(v)->tuple)
#define AS_FUNC(v)     (AS_PTR(v)->func)
#define AS_BUILTIN(v)  (AS_PTR(v)->builtin)
#define AS_COMPLEX(v)  (AS_PTR(v)->complex_val)

Value value_retain(Value v);
void  value_release(Value v);

Value value_null(void);
Value value_int(long long n);
Value value_float(double d);
Value value_complex(double real, double imag);
Value value_string(const char *s);
Value value_string_take(char *s);
Value value_string_intern(const char *s);
Value value_bool(int b);
Value value_array_new(void);
Value value_dict_new(void);
Value value_set_new(void);
Value value_tuple_new(Value *items, int count);
Value value_function(const char *name, Param *params, int param_count,
                     ASTNode *body, Env *closure);
Value value_function_copy(Value proto, Env *closure);
Value value_builtin(const char *name, BuiltinFn fn);

void value_immortals_init(void);
void value_immortals_free(void);

void  value_array_push(Value arr, Value item);
Value value_array_get(Value arr, long long idx);
void  value_array_insert(Value arr, long long idx, Value item);
bool  value_array_remove(Value arr, Value item);
Value value_array_pop(Value arr, long long idx);
void  value_array_sort(Value arr);
void  value_array_extend(Value arr, Value other);

Value value_dict_get(Value dict, Value key);
void  value_dict_set(Value dict, Value key, Value val);
bool  value_dict_remove(Value dict, Value key);
int   value_dict_find_index(Value dict, Value key);
Value value_dict_get_at(Value dict, int index);
Value value_dict_get_cached(Value dict, Value key, int *index, unsigned int *version);
void  value_dict_clear(Value dict);

bool  value_set_has(Value set, Value item);
void  value_set_add(Value set, Value item);
bool  value_set_remove(Value set, Value item);

bool  value_equals(Value a, Value b);
int   value_compare(Value a, Value b);
bool  value_truthy(Value v);
char *value_to_string(Value v);
void  value_print(Value v);
Value value_copy(Value v);
const char *value_type_name(ValueType t);

Value value_add(Value a, Value b);
Value value_sub(Value a, Value b);
Value value_mul(Value a, Value b);
Value value_div(Value a, Value b);
Value value_mod(Value a, Value b);
Value value_pow(Value a, Value b);
Value value_neg(Value v);

#endif
