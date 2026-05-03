#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "ast.h"

ASTNode *ast_node_new(NodeType type, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->type = type;
    n->line = line;
    return n;
}

void ast_node_free(ASTNode *n) {
    if (!n) return;

    switch (n->type) {
        case NODE_PROGRAM:
        case NODE_BLOCK:
            for (int i = 0; i < n->block.count; i++)
                ast_node_free(n->block.stmts[i]);
            free(n->block.stmts);
            break;

        case NODE_VAR_DECL:
            free(n->var_decl.name);
            ast_node_free(n->var_decl.init);
            break;

        case NODE_ASSIGN:
        case NODE_COMPOUND_ASSIGN:
            ast_node_free(n->assign.target);
            ast_node_free(n->assign.value);
            break;

        case NODE_EXPR_STMT:
            ast_node_free(n->expr_stmt.expr);
            break;

        case NODE_RETURN:
            ast_node_free(n->ret.value);
            break;

        case NODE_BREAK:
        case NODE_CONTINUE:
        case NODE_NULL_LIT:
            break;

        case NODE_IF:
            for (int i = 0; i < n->if_stmt.branch_count; i++) {
                ast_node_free(n->if_stmt.conds[i]);
                ast_node_free(n->if_stmt.bodies[i]);
            }
            free(n->if_stmt.conds);
            free(n->if_stmt.bodies);
            ast_node_free(n->if_stmt.else_body);
            break;

        case NODE_WHILE:
            ast_node_free(n->while_stmt.cond);
            ast_node_free(n->while_stmt.body);
            break;

        case NODE_FOR_IN:
            free(n->for_in.var);
            ast_node_free(n->for_in.iter);
            ast_node_free(n->for_in.body);
            break;

        case NODE_FUNC_DECL:
            free(n->func_decl.name);
            for (int i = 0; i < n->func_decl.param_count; i++) {
                free(n->func_decl.params[i].name);
                free(n->func_decl.params[i].type_hint);
                ast_node_free(n->func_decl.params[i].default_val);
            }
            free(n->func_decl.params);
            ast_node_free(n->func_decl.body);
            break;

        case NODE_REPEAT:
            ast_node_free(n->repeat_stmt.count);
            ast_node_free(n->repeat_stmt.cond);
            ast_node_free(n->repeat_stmt.body);
            break;

        case NODE_TRY_CATCH:
            ast_node_free(n->try_catch.try_body);
            free(n->try_catch.catch_var);
            ast_node_free(n->try_catch.catch_body);
            ast_node_free(n->try_catch.finally_body);
            break;

        case NODE_THROW:
            ast_node_free(n->throw_stmt.value);
            break;

        case NODE_MATCH_EXPR:
        case NODE_MATCH:
            ast_node_free(n->match_stmt.value);
            for (int i = 0; i < n->match_stmt.count; i++) {
                ast_node_free(n->match_stmt.patterns[i]);
                ast_node_free(n->match_stmt.bodies[i]);
            }
            free(n->match_stmt.patterns);
            free(n->match_stmt.bodies);
            ast_node_free(n->match_stmt.else_body);
            break;

        case NODE_BINOP:
            ast_node_free(n->binop.left);
            ast_node_free(n->binop.right);
            break;

        case NODE_UNOP:
            ast_node_free(n->unop.operand);
            break;

        case NODE_RANGE:
            ast_node_free(n->range_lit.start);
            ast_node_free(n->range_lit.end);
            ast_node_free(n->range_lit.step);
            break;

        case NODE_NULLCOAL:
            ast_node_free(n->nullcoal.left);
            ast_node_free(n->nullcoal.right);
            break;

        case NODE_SAFE_ACCESS:
            ast_node_free(n->safe_access.obj);
            free(n->safe_access.name);
            for (int i = 0; i < n->safe_access.arg_count; i++)
                ast_node_free(n->safe_access.args[i]);
            free(n->safe_access.args);
            break;

        case NODE_IS_EXPR:
            ast_node_free(n->is_expr.obj);
            free(n->is_expr.type_name);
            break;

        case NODE_STRING_LIT:
        case NODE_FSTRING_LIT:
            free(n->string_lit.value);
            break;

        case NODE_IDENT:
            free(n->ident.name);
            break;

        case NODE_ARRAY_LIT:
        case NODE_SET_LIT:
        case NODE_TUPLE_LIT:
            for (int i = 0; i < n->list_lit.count; i++)
                ast_node_free(n->list_lit.items[i]);
            free(n->list_lit.items);
            break;

        case NODE_DICT_LIT:
            for (int i = 0; i < n->dict_lit.count; i++) {
                ast_node_free(n->dict_lit.keys[i]);
                ast_node_free(n->dict_lit.vals[i]);
            }
            free(n->dict_lit.keys);
            free(n->dict_lit.vals);
            break;

        case NODE_INDEX:
            ast_node_free(n->index_expr.obj);
            ast_node_free(n->index_expr.index);
            break;

        case NODE_SLICE:
            ast_node_free(n->slice_expr.obj);
            ast_node_free(n->slice_expr.start);
            ast_node_free(n->slice_expr.stop);
            ast_node_free(n->slice_expr.step);
            break;

        case NODE_MEMBER:
            ast_node_free(n->member.obj);
            free(n->member.name);
            break;

        case NODE_METHOD_CALL:
            ast_node_free(n->method_call.obj);
            free(n->method_call.method);
            for (int i = 0; i < n->method_call.arg_count; i++)
                ast_node_free(n->method_call.args[i]);
            free(n->method_call.args);
            break;

        case NODE_FUNC_CALL:
            ast_node_free(n->func_call.callee);
            for (int i = 0; i < n->func_call.arg_count; i++)
                ast_node_free(n->func_call.args[i]);
            free(n->func_call.args);
            break;

        case NODE_IN_EXPR:
        case NODE_NOT_IN_EXPR:
            ast_node_free(n->in_expr.item);
            ast_node_free(n->in_expr.container);
            break;

        case NODE_IMPORT:
            free(n->import_stmt.path);
            free(n->import_stmt.alias);
            free(n->import_stmt.symbol);
            break;

        case NODE_CLASS_DECL:
            free(n->class_decl.name);
            free(n->class_decl.super);
            for (int i = 0; i < n->class_decl.method_count; i++)
                ast_node_free(n->class_decl.methods[i]);
            free(n->class_decl.methods);
            break;

        case NODE_STRUCT_DECL:
            free(n->struct_decl.name);
            for (int i = 0; i < n->struct_decl.field_count; i++)
                free(n->struct_decl.fields[i]);
            free(n->struct_decl.fields);
            break;

        case NODE_NEW_EXPR:
            free(n->new_expr.class_name);
            for (int i = 0; i < n->new_expr.arg_count; i++)
                ast_node_free(n->new_expr.args[i]);
            free(n->new_expr.args);
            break;

        case NODE_FN_EXPR:
            for (int i = 0; i < n->fn_expr.param_count; i++) {
                free(n->fn_expr.params[i].name);
                free(n->fn_expr.params[i].type_hint);
                ast_node_free(n->fn_expr.params[i].default_val);
            }
            free(n->fn_expr.params);
            ast_node_free(n->fn_expr.body);
            break;

        case NODE_SPREAD:
            ast_node_free(n->spread.expr);
            break;

        case NODE_WALRUS:
        case NODE_WALRUS_EXPR:
            free(n->walrus.name);
            ast_node_free(n->walrus.value);
            break;

        case NODE_TERNARY:
            ast_node_free(n->ternary.cond);
            ast_node_free(n->ternary.then_val);
            ast_node_free(n->ternary.else_val);
            break;

        case NODE_CHAIN_CMP: {
            for (int i = 0; i < n->chain_cmp.count; i++)
                ast_node_free(n->chain_cmp.exprs[i]);
            free(n->chain_cmp.exprs);
            for (int i = 0; i < n->chain_cmp.count - 1; i++)
                free(n->chain_cmp.ops[i]);
            free(n->chain_cmp.ops);
            break;
        }

        case NODE_LINK_STMT:
            for (int i = 0; i < n->link_stmt.path_count; i++)
                free(n->link_stmt.paths[i]);
            free(n->link_stmt.paths);
            break;

        default:
            break;
    }
    free(n);
}
