#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
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

typedef struct Value Value;
typedef struct Env   Env;
typedef struct Chunk Chunk;

typedef Value *(*BuiltinFn)(Value **args, int argc);

typedef struct {
    Value **items;
    int     len;
    int     cap;
} ValueArray;

typedef struct {
    Value *key;
    Value *val;
} DictEntry;

typedef struct {
    DictEntry *entries;
    int        len;
    int        cap;
    unsigned int version;
} ValueDict;

struct Value {
    ValueType type;
    int       ref_count;
    unsigned char gc_marked;
    unsigned char gc_immortal;    /* 1 = never freed by GC or value_release */
    unsigned char gc_generation;  /* 0 = young, 1 = old */
    struct Value *gc_next;

    union {
        long long  int_val;
        double     float_val;
        struct { double real; double imag; } complex_val;
        char      *str_val;
        int        bool_val;    /* 1=true, 0=false, -1=unknown */
        ValueArray array;
        ValueDict  dict;
        ValueArray set;         /* reuse ValueArray for set */
        ValueArray tuple;

        struct {
            char    *name;
            Param   *params;
            int      param_count;
            ASTNode *body;
            Env     *closure;
            Chunk   *chunk;
            bool     owns_chunk;
        } func;

        struct {
            char      *name;
            BuiltinFn  fn;
        } builtin;
    };
};

/* Reference counting — immortal values bypass both operations */
Value *value_retain(Value *v);
void   value_release(Value *v);

/* Constructors */
Value *value_null(void);
Value *value_int(long long n);
Value *value_float(double d);
Value *value_complex(double real, double imag);
Value *value_string(const char *s);
Value *value_string_take(char *s);        /* takes ownership of heap string */
Value *value_string_intern(const char *s);/* interned immortal string */
Value *value_bool(int b);
Value *value_array_new(void);
Value *value_dict_new(void);
Value *value_set_new(void);
Value *value_tuple_new(Value **items, int count);
Value *value_function(const char *name, Param *params, int param_count,
                      ASTNode *body, Env *closure);
Value *value_builtin(const char *name, BuiltinFn fn);

/* Immortal singleton lifecycle — call at program start / shutdown */
void value_immortals_init(void);
void value_immortals_free(void);

/* Array operations */
void   value_array_push(Value *arr, Value *item);
Value *value_array_get(Value *arr, long long idx);
void   value_array_insert(Value *arr, long long idx, Value *item);
bool   value_array_remove(Value *arr, Value *item);
Value *value_array_pop(Value *arr, long long idx);
void   value_array_sort(Value *arr);
void   value_array_extend(Value *arr, Value *other);

/* Dict operations */
Value *value_dict_get(Value *dict, Value *key);
void   value_dict_set(Value *dict, Value *key, Value *val);
bool   value_dict_remove(Value *dict, Value *key);
int    value_dict_find_index(Value *dict, Value *key);
Value *value_dict_get_at(Value *dict, int index);
Value *value_dict_get_cached(Value *dict, Value *key, int *index, unsigned int *version);
void   value_dict_clear(Value *dict);

/* Set operations */
bool   value_set_has(Value *set, Value *item);
void   value_set_add(Value *set, Value *item);
bool   value_set_remove(Value *set, Value *item);

/* Utilities */
bool   value_equals(Value *a, Value *b);
int    value_compare(Value *a, Value *b);
bool   value_truthy(Value *v);
char  *value_to_string(Value *v);
void   value_print(Value *v);
Value *value_copy(Value *v);
const char *value_type_name(ValueType t);

/* Arithmetic */
Value *value_add(Value *a, Value *b);
Value *value_sub(Value *a, Value *b);
Value *value_mul(Value *a, Value *b);
Value *value_div(Value *a, Value *b);
Value *value_mod(Value *a, Value *b);
Value *value_pow(Value *a, Value *b);
Value *value_neg(Value *a);

#endif
