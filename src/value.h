#ifndef VALUE_H
#define VALUE_H
#include <stdbool.h>
#include <stdint.h>
#include "ast.h"
typedef enum { VAL_NULL, VAL_INT, VAL_FLOAT, VAL_COMPLEX, VAL_STRING, VAL_BOOL, VAL_ARRAY, VAL_DICT, VAL_SET, VAL_TUPLE, VAL_FUNCTION, VAL_BUILTIN, VAL_RANGE } ValueType;
typedef uintptr_t Value;
typedef struct Env Env;
typedef struct Chunk Chunk;
typedef struct ValueStruct ValueStruct;
typedef Value (*BuiltinFn)(Value *args, int argc);
typedef struct { Value *items; int len; int cap; } ValueArray;
typedef struct { Value key; Value val; } DictEntry;
typedef struct { DictEntry *entries; int len; int cap; unsigned int version; } ValueDict;
struct ValueStruct {
    ValueType _type_; int ref_count; unsigned char gc_marked, gc_immortal, gc_generation;
    struct ValueStruct *gc_next, *gc_prev;
    union {
        long long _int_val_; double _float_val_; struct { double _real_, _imag_; } _complex_val_;
        char *_str_val_; int _bool_val_; ValueArray _array_; ValueDict _dict_; ValueArray _set_; ValueArray _tuple_;
        struct { long long start, stop, step; } _range_;
        struct { char *name; Param *params; int param_count; ASTNode *body; Env *closure; Chunk *chunk; bool owns_chunk, owns_params; } _func_;
        struct { char *name; BuiltinFn fn; } _builtin_;
    };
};
#define VAL_TAG_PTR 0x0
#define VAL_TAG_INT 0x1
#define VAL_TAG_SPEC 0x2
#define IS_PTR(v) (((v) & 0x3) == VAL_TAG_PTR && (v) != 0)
#define IS_INT(v) (((v) & 0x1) == VAL_TAG_INT)
#define IS_SPEC(v) (((v) & 0x3) == VAL_TAG_SPEC)
#define IS_NULL(v) ((v) == 0)
#define AS_PTR(v) ((ValueStruct*)(v))
#define AS_INT(v) ((long long)((intptr_t)(v) >> 1))
#define TO_INT(n) ((Value)((((intptr_t)(n)) << 1) | VAL_TAG_INT))
#define VAL_SPEC_FALSE (0x0 << 3 | VAL_TAG_SPEC)
#define VAL_SPEC_TRUE (0x1 << 3 | VAL_TAG_SPEC)
#define VAL_SPEC_UNDEFINED (0x3 << 3 | VAL_TAG_SPEC)
#define TO_BOOL(b) ((b) == 1 ? VAL_SPEC_TRUE : ((b) == 0 ? VAL_SPEC_FALSE : VAL_SPEC_UNDEFINED))
#define AS_BOOL(v) ((v) == VAL_SPEC_TRUE ? 1 : ((v) == VAL_SPEC_FALSE ? 0 : -1))
#define TO_NULL() ((Value)0)
#define TO_UNDEFINED() ((Value)VAL_SPEC_UNDEFINED)
#define IS_UNDEFINED(v) ((v) == VAL_SPEC_UNDEFINED)
#define VAL_TYPE(v) (IS_INT(v) ? VAL_INT : (IS_NULL(v) ? VAL_NULL : (IS_SPEC(v) ? ((v) == VAL_SPEC_UNDEFINED ? VAL_NULL : VAL_BOOL) : AS_PTR(v)->_type_)))
#define AS_FLOAT(v) (AS_PTR(v)->_float_val_)
#define AS_STR(v) (AS_PTR(v)->_str_val_)
#define AS_ARRAY(v) (AS_PTR(v)->_array_)
#define AS_DICT(v) (AS_PTR(v)->_dict_)
#define AS_SET(v) (AS_PTR(v)->_set_)
#define AS_TUPLE(v) (AS_PTR(v)->_tuple_)
#define AS_FUNC(v) (AS_PTR(v)->_func_)
#define AS_BUILTIN(v) (AS_PTR(v)->_builtin_)
#define AS_COMPLEX(v) (AS_PTR(v)->_complex_val_)
#define AS_RANGE(v) (AS_PTR(v)->_range_)
Value value_retain(Value v); void value_release(Value v); Value value_null(void); Value value_int(long long n); Value value_float(double d); Value value_complex(double real, double imag); Value value_string(const char *s); Value value_string_take(char *s); Value value_string_intern(const char *s); Value value_bool(int b); Value value_array_new(void); Value value_dict_new(void); Value value_set_new(void); Value value_tuple_new(Value *items, int count); Value value_range_new(long long start, long long stop, long long step); Value value_function(const char *name, Param *params, int param_count, ASTNode *body, Env *closure); Value value_builtin(const char *name, BuiltinFn fn); void value_immortals_init(void); void value_immortals_free(void); void value_array_push(Value arr, Value item); Value value_array_get(Value arr, long long idx); void value_array_insert(Value arr, long long idx, Value item); bool value_array_remove(Value arr, Value item); Value value_array_pop(Value arr, long long idx); void value_array_sort(Value arr); void value_array_extend(Value arr, Value other); Value value_dict_get(Value dict, Value key); void value_dict_set(Value dict, Value key, Value val); bool value_dict_remove(Value dict, Value key); int value_dict_find_index(Value dict, Value key); Value value_dict_get_at(Value dict, int index); Value value_dict_get_cached(Value dict, Value key, int *index, unsigned int *version); void value_dict_clear(Value dict); bool value_set_has(Value set, Value item); void value_set_add(Value set, Value item); bool value_set_remove(Value set, Value item); bool value_equals(Value a, Value b); int value_compare(Value a, Value b); bool value_truthy(Value v); char *value_to_string(Value v); void value_print(Value v); Value value_copy(Value v); const char *value_type_name(ValueType t); Value value_add(Value a, Value b); Value value_sub(Value a, Value b); Value value_mul(Value a, Value b); Value value_div(Value a, Value b); Value value_mod(Value a, Value b); Value value_pow(Value a, Value b); Value value_neg(Value a);
#endif