#ifndef AST_H
#define AST_H
#include <stdbool.h>

typedef enum {
    NODE_PROGRAM, NODE_BLOCK, NODE_VAR_DECL, NODE_IDENT, NODE_INT_LIT, NODE_FLOAT_LIT, NODE_COMPLEX_LIT,
    NODE_STRING_LIT, NODE_FSTRING_LIT, NODE_ARRAY_LIT, NODE_DICT_LIT, NODE_SET_LIT, NODE_TUPLE_LIT,
    NODE_FUNC_CALL, NODE_METHOD_CALL, NODE_INDEX, NODE_MEMBER, NODE_ASSIGN, NODE_COMPOUND_ASSIGN,
    NODE_RETURN, NODE_BREAK, NODE_CONTINUE, NODE_IF, NODE_WHILE, NODE_FOR_IN, NODE_REPEAT,
    NODE_THROW, NODE_TRY_CATCH, NODE_MATCH, NODE_RANGE, NODE_OPTIMIZED_RANGE, NODE_EXPR_STMT,
    NODE_NULL_LIT, NODE_BOOL_LIT, NODE_IMPORT, NODE_LINK_STMT, NODE_IS_EXPR, NODE_TERNARY,
    NODE_FUNC_DECL, NODE_CLASS_DECL, NODE_STRUCT_DECL, NODE_NEW_EXPR, NODE_WALRUS, NODE_WALRUS_EXPR,
    NODE_CHAIN_CMP, NODE_UNOP, NODE_BINOP, NODE_SLICE, NODE_SAFE_ACCESS, NODE_SPREAD, NODE_FN_EXPR, NODE_NULLCOAL, NODE_NOT_IN_EXPR, NODE_IN_EXPR
} NodeType;

typedef struct ASTNode ASTNode;
typedef struct { char *name; char *type_hint; ASTNode *default_val; } Param;

struct ASTNode {
    NodeType type; int line;
    union {
        struct { ASTNode **stmts; int count; } block;
        struct { char *name; ASTNode *init; bool is_const; } var_decl;
        struct { char *name; } ident;
        struct { long long value; } int_lit;
        struct { double value; } float_lit;
        struct { double real, imag; } complex_lit;
        struct { bool value; } bool_lit;
        struct { char *value; } string_lit;
        struct { ASTNode *callee; ASTNode **args; int arg_count; } func_call;
        struct { ASTNode *obj; char *method; ASTNode **args; int arg_count; } method_call;
        struct { ASTNode *obj, *index; } index_expr;
        struct { ASTNode *obj, *start, *stop, *step; } slice_expr;
        struct { ASTNode *obj; char *name; } member;
        struct { ASTNode *target, *value; const char *op; } assign;
        struct { ASTNode *value; } ret;
        struct { ASTNode *cond, *body; } while_stmt;
        struct { char *var; ASTNode *iter; ASTNode *body; } for_in;
        struct { ASTNode **conds, **bodies; int branch_count; ASTNode *else_body; } if_stmt;
        struct { char *name; Param *params; int param_count; ASTNode *body; } func_decl;
        struct { ASTNode *start, *end, *step; bool inclusive; } range_lit;
        struct { ASTNode *expr; } expr_stmt;
        struct { ASTNode *operand; const char *op; } unop;
        struct { ASTNode *left, *right; const char *op; } binop;
        struct { char *path; char *alias; char *symbol; } import_stmt;
        struct { ASTNode **items; int count; } list_lit;
        struct { ASTNode **keys, **vals; int count; } dict_lit;
        struct { ASTNode *count, *cond, *body; bool until; } repeat_stmt;
        struct { ASTNode *try_body, *catch_body, *finally_body; char *catch_var; } try_catch;
        struct { ASTNode *value; ASTNode **patterns, **bodies; int count; ASTNode *else_body; } match_stmt;
        struct { ASTNode *value; } throw_stmt;
        struct { ASTNode *left, *right; } nullcoal;
        struct { ASTNode *item, *container; } in_expr;
        struct { ASTNode *obj; char *name; ASTNode **args; int arg_count; bool is_call; } safe_access;
        struct { ASTNode *obj; char *type_name; bool negate; } is_expr;
        struct { ASTNode *cond, *then_val, *else_val; } ternary;
        struct { Param *params; int param_count; ASTNode *body; } fn_expr;
        struct { ASTNode *expr; } spread;
        struct { char *name; ASTNode *value; } walrus;
        struct { ASTNode **exprs; char **ops; int count; } chain_cmp;
        struct { char **paths; int path_count; } link_stmt;
        struct { char *name, *super; ASTNode **methods; int method_count; } class_decl;
        struct { char *name; char **fields; int field_count; } struct_decl;
        struct { char *class_name; ASTNode **args; int arg_count; } new_expr;
    };
};

ASTNode *ast_node_new(NodeType type, int line);
void ast_node_free(ASTNode *node);
#endif
