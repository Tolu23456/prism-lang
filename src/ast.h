#ifndef AST_H
#define AST_H

#include <stdbool.h>

typedef enum {
    NODE_PROGRAM,
    NODE_BLOCK,

    /* Declarations / statements */
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_COMPOUND_ASSIGN,
    NODE_EXPR_STMT,
    NODE_RETURN,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR_IN,
    NODE_FUNC_DECL,

    /* New control flow */
    NODE_REPEAT,
    NODE_TRY_CATCH,
    NODE_THROW,
    NODE_MATCH,

    /* Expressions */
    NODE_BINOP,
    NODE_UNOP,
    NODE_TERNARY,

    /* New expression nodes */
    NODE_RANGE,
    NODE_NULLCOAL,
    NODE_SAFE_ACCESS,
    NODE_IS_EXPR,
    NODE_WALRUS,

    /* Literals */
    NODE_INT_LIT,
    NODE_FLOAT_LIT,
    NODE_COMPLEX_LIT,
    NODE_STRING_LIT,
    NODE_FSTRING_LIT,
    NODE_BOOL_LIT,
    NODE_NULL_LIT,
    NODE_ARRAY_LIT,
    NODE_DICT_LIT,
    NODE_SET_LIT,
    NODE_TUPLE_LIT,

    /* Access */
    NODE_IDENT,
    NODE_INDEX,
    NODE_SLICE,
    NODE_MEMBER,
    NODE_METHOD_CALL,
    NODE_FUNC_CALL,

    /* Membership */
    NODE_IN_EXPR,
    NODE_NOT_IN_EXPR,

    /* Import */
    NODE_IMPORT,

    /* Class / struct / object system */
    NODE_CLASS_DECL,    /* class Foo { ... } */
    NODE_STRUCT_DECL,   /* struct Vec { x, y, z } */
    NODE_NEW_EXPR,      /* new Foo(args...) */

    /* Anonymous function expression: fn(params) { body } or fn(params) => expr */
    NODE_FN_EXPR,

    /* Spread / varargs */
    NODE_SPREAD,        /* ...expr */

    /* Walrus in expression context */
    NODE_WALRUS_EXPR,

    /* Chain comparisons: 1 < x < 10 */
    NODE_CHAIN_CMP,

    /* PSS stylesheet link: link style "file.pss" */
    NODE_LINK_STMT,
} NodeType;

typedef struct ASTNode ASTNode;

typedef struct {
    char    *name;
    char    *type_hint;
    ASTNode *default_val;  /* optional default value expression; NULL if none */
} Param;

struct ASTNode {
    NodeType type;
    int      line;

    union {
        /* NODE_PROGRAM / NODE_BLOCK */
        struct { ASTNode **stmts; int count; } block;

        /* NODE_VAR_DECL */
        struct { char *name; ASTNode *init; bool is_const; } var_decl;

        /* NODE_ASSIGN / NODE_COMPOUND_ASSIGN */
        struct { ASTNode *target; ASTNode *value; char op[4]; } assign;

        /* NODE_EXPR_STMT */
        struct { ASTNode *expr; } expr_stmt;

        /* NODE_RETURN */
        struct { ASTNode *value; } ret;

        /* NODE_IF */
        struct {
            ASTNode **conds;
            ASTNode **bodies;
            int        branch_count;
            ASTNode   *else_body;
        } if_stmt;

        /* NODE_WHILE */
        struct { ASTNode *cond; ASTNode *body; } while_stmt;

        /* NODE_FOR_IN */
        struct { char *var; ASTNode *iter; ASTNode *body; } for_in;

        /* NODE_FUNC_DECL */
        struct {
            char    *name;
            Param   *params;
            int      param_count;
            ASTNode *body;
        } func_decl;

        /* NODE_REPEAT */
        struct {
            ASTNode *count;    /* repeat N: count expr; NULL if while/until */
            ASTNode *cond;     /* repeat while/until: condition; NULL if count */
            bool     until;    /* true = repeat until (negate cond) */
            ASTNode *body;
        } repeat_stmt;

        /* NODE_TRY_CATCH */
        struct {
            ASTNode *try_body;
            char    *catch_var;     /* variable name for caught error; may be NULL */
            ASTNode *catch_body;
            ASTNode *finally_body;  /* optional; NULL if absent */
        } try_catch;

        /* NODE_THROW */
        struct { ASTNode *value; } throw_stmt;

        /* NODE_MATCH */
        struct {
            ASTNode  *value;
            ASTNode **patterns;  /* NULL entry = else */
            ASTNode **bodies;
            int       count;
            ASTNode  *else_body;
        } match_stmt;

        /* NODE_BINOP */
        struct { char op[4]; ASTNode *left; ASTNode *right; } binop;

        /* NODE_UNOP */
        struct { char op[4]; ASTNode *operand; } unop;

        /* NODE_RANGE */
        struct {
            ASTNode *start;
            ASTNode *end;
            ASTNode *step;       /* NULL = step 1 */
            bool     inclusive;  /* true: start..end includes end */
        } range_lit;

        /* NODE_NULLCOAL */
        struct { ASTNode *left; ASTNode *right; } nullcoal;

        /* NODE_SAFE_ACCESS */
        struct {
            ASTNode *obj;
            char    *name;
            /* method call fields */
            ASTNode **args;
            int       arg_count;
            bool      is_call;
        } safe_access;

        /* NODE_IS_EXPR */
        struct { ASTNode *obj; char *type_name; bool negate; } is_expr;

        /* NODE_INT_LIT */
        struct { long long value; } int_lit;

        /* NODE_FLOAT_LIT */
        struct { double value; } float_lit;

        /* NODE_COMPLEX_LIT */
        struct { double real; double imag; } complex_lit;

        /* NODE_STRING_LIT / NODE_FSTRING_LIT */
        struct { char *value; } string_lit;

        /* NODE_BOOL_LIT */
        struct { int value; } bool_lit; /* 1=true, 0=false, -1=unknown */

        /* NODE_ARRAY_LIT / NODE_SET_LIT / NODE_TUPLE_LIT */
        struct { ASTNode **items; int count; } list_lit;

        /* NODE_DICT_LIT */
        struct { ASTNode **keys; ASTNode **vals; int count; } dict_lit;

        /* NODE_IDENT */
        struct { char *name; } ident;

        /* NODE_INDEX */
        struct { ASTNode *obj; ASTNode *index; } index_expr;

        /* NODE_SLICE */
        struct {
            ASTNode *obj;
            ASTNode *start;
            ASTNode *stop;
            ASTNode *step;
        } slice_expr;

        /* NODE_MEMBER */
        struct { ASTNode *obj; char *name; } member;

        /* NODE_METHOD_CALL */
        struct {
            ASTNode  *obj;
            char     *method;
            ASTNode **args;
            int       arg_count;
        } method_call;

        /* NODE_FUNC_CALL */
        struct {
            ASTNode  *callee;
            ASTNode **args;
            int       arg_count;
        } func_call;

        /* NODE_IN_EXPR / NODE_NOT_IN_EXPR */
        struct { ASTNode *item; ASTNode *container; } in_expr;

        /* NODE_IMPORT */
        struct {
            char *path;
            char *alias;    /* 'as' alias; NULL if none */
            char *symbol;   /* from X import Y: symbol name; NULL for plain import */
        } import_stmt;

        /* NODE_CLASS_DECL */
        struct {
            char     *name;
            char     *super;      /* parent class name; NULL if none */
            ASTNode **methods;    /* array of NODE_FUNC_DECL nodes */
            int       method_count;
        } class_decl;

        /* NODE_STRUCT_DECL */
        struct {
            char  *name;
            char **fields;   /* field names */
            int    field_count;
        } struct_decl;

        /* NODE_NEW_EXPR */
        struct {
            char     *class_name;
            ASTNode **args;
            int       arg_count;
        } new_expr;

        /* NODE_FN_EXPR */
        struct {
            Param   *params;
            int      param_count;
            ASTNode *body;    /* block or single expression (arrow form) */
            bool     is_arrow; /* true if => form */
        } fn_expr;

        /* NODE_SPREAD */
        struct { ASTNode *expr; } spread;

        /* NODE_WALRUS_EXPR */
        struct { char *name; ASTNode *value; } walrus;

        /* NODE_TERNARY */
        struct { ASTNode *cond; ASTNode *then_val; ASTNode *else_val; } ternary;

        /* NODE_CHAIN_CMP: a < b <= c  (count exprs, count-1 ops) */
        struct {
            ASTNode **exprs;
            char    **ops;
            int       count;
        } chain_cmp;

        /* NODE_LINK_STMT: link style "a.pss", "b.pss" */
        struct {
            char **paths;
            int    path_count;
        } link_stmt;
    };
};

ASTNode *ast_node_new(NodeType type, int line);
void     ast_node_free(ASTNode *node);

#endif
