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

    /* Expressions */
    NODE_BINOP,
    NODE_UNOP,
    NODE_TERNARY,

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
} NodeType;

typedef struct ASTNode ASTNode;

typedef struct {
    char    *name;
    char    *type_hint;
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

        /* NODE_BINOP */
        struct { char op[4]; ASTNode *left; ASTNode *right; } binop;

        /* NODE_UNOP */
        struct { char op[4]; ASTNode *operand; } unop;

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
    };
};

ASTNode *ast_node_new(NodeType type, int line);
void     ast_node_free(ASTNode *node);

#endif
