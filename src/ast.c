#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "ast.h"

ASTNode *ast_node_new(NodeType type, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->type = type; n->line = line; return n;
}

void ast_node_free(ASTNode *n) {
    if (!n) return;
    switch (n->type) {
        case NODE_PROGRAM:
        case NODE_BLOCK:
            for (int i = 0; i < n->block.count; i++) ast_node_free(n->block.stmts[i]);
            free(n->block.stmts); break;
        case NODE_VAR_DECL:
            free(n->var_decl.name); ast_node_free(n->var_decl.init); break;
        case NODE_IDENT:
            free(n->ident.name); break;
        case NODE_FUNC_CALL:
            ast_node_free(n->func_call.callee);
            for (int i = 0; i < n->func_call.arg_count; i++) ast_node_free(n->func_call.args[i]);
            free(n->func_call.args); break;
        case NODE_FOR_IN:
            free(n->for_in.var); ast_node_free(n->for_in.iter); ast_node_free(n->for_in.body); break;
        case NODE_RANGE:
        case NODE_OPTIMIZED_RANGE:
            ast_node_free(n->range_lit.start); ast_node_free(n->range_lit.end); ast_node_free(n->range_lit.step); break;
        case NODE_EXPR_STMT:
            ast_node_free(n->expr_stmt.expr); break;
        default: break;
    }
    free(n);
}
