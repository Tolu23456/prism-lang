#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "ast.h"

Parser *parser_new(const char *src) { Parser *p = calloc(1, sizeof(Parser)); p->lexer = lexer_new(src); p->current = lexer_next(p->lexer); return p; }
void parser_free(Parser *p) { lexer_free(p->lexer); token_free(p->current); free(p); }
static void advance(Parser *p) { token_free(p->current); p->current = lexer_next(p->lexer); }

static ASTNode *parse_expr(Parser *p);

static ASTNode *parse_primary(Parser *p) {
    if (!p->current) return NULL;
    if (p->current->type == TOKEN_INT_LIT) { ASTNode *n = ast_node_new(NODE_INT_LIT, p->current->line); n->int_lit.value = atoll(p->current->value); advance(p); return n; }
    if (p->current->type == TOKEN_STRING_LIT) { ASTNode *n = ast_node_new(NODE_STRING_LIT, p->current->line); n->string_lit.value = strdup(p->current->value); advance(p); return n; }
    if (p->current->type == TOKEN_IDENT) {
        ASTNode *n = ast_node_new(NODE_IDENT, p->current->line); n->ident.name = strdup(p->current->value); advance(p);
        if (p->current->type == TOKEN_LPAREN) {
            advance(p); ASTNode *call = ast_node_new(NODE_FUNC_CALL, n->line); call->func_call.callee = n;
            call->func_call.args = malloc(sizeof(ASTNode*) * 64); int c = 0;
            while (p->current->type != TOKEN_RPAREN && p->current->type != TOKEN_EOF) {
                call->func_call.args[c++] = parse_expr(p);
                if (p->current->type == TOKEN_COMMA) advance(p);
            }
            if (p->current->type == TOKEN_RPAREN) advance(p);
            call->func_call.arg_count = c; return call;
        }
        return n;
    }
    if (p->current->type == TOKEN_RANGE_KW) {
        advance(p); ASTNode *start = parse_expr(p);
        if (p->current->type != TOKEN_DOTDOT) return start;
        advance(p); ASTNode *end = parse_expr(p);
        ASTNode *step = NULL; if (p->current->type == TOKEN_STEP) { advance(p); step = parse_expr(p); }
        ASTNode *n = ast_node_new(NODE_OPTIMIZED_RANGE, start?start->line:0); n->range_lit.start = start; n->range_lit.end = end; n->range_lit.step = step; return n;
    }
    advance(p); return NULL;
}

static ASTNode *parse_expr(Parser *p) {
    ASTNode *n = parse_primary(p);
    if (n && p->current && p->current->type == TOKEN_DOTDOT) {
        advance(p); ASTNode *end = parse_primary(p);
        ASTNode *range = ast_node_new(NODE_RANGE, n->line);
        range->range_lit.start = n; range->range_lit.end = end; range->range_lit.step = NULL;
        return range;
    }
    return n;
}

static ASTNode *parse_stmt(Parser *p) {
    while (p->current->type == TOKEN_NEWLINE) advance(p);
    if (p->current->type == TOKEN_LET) {
        advance(p); if (p->current->type != TOKEN_IDENT) return NULL;
        char *name = strdup(p->current->value); advance(p);
        if (p->current->type == TOKEN_EQ) advance(p);
        ASTNode *n = ast_node_new(NODE_VAR_DECL, p->current->line); n->var_decl.name = name; n->var_decl.init = parse_expr(p); return n;
    }
    if (p->current->type == TOKEN_FOR) {
        advance(p); if (p->current->type != TOKEN_IDENT) return NULL;
        char *v = strdup(p->current->value); advance(p);
        if (p->current->type == TOKEN_IN) advance(p);
        ASTNode *iter = parse_expr(p); while (p->current->type == TOKEN_NEWLINE) advance(p);
        if (p->current->type == TOKEN_LBRACE) advance(p);
        ASTNode *n = ast_node_new(NODE_FOR_IN, iter?iter->line:0); n->for_in.var = v; n->for_in.iter = iter;
        n->for_in.body = ast_node_new(NODE_BLOCK, n->line); n->for_in.body->block.stmts = malloc(sizeof(ASTNode*) * 256); int c = 0;
        while (p->current->type != TOKEN_RBRACE && p->current->type != TOKEN_EOF) {
            ASTNode *s = parse_stmt(p); if (s) n->for_in.body->block.stmts[c++] = s;
        }
        if (p->current->type == TOKEN_RBRACE) advance(p);
        n->for_in.body->block.count = c; return n;
    }
    ASTNode *expr = parse_expr(p); if (!expr) return NULL;
    ASTNode *n = ast_node_new(NODE_EXPR_STMT, expr->line); n->expr_stmt.expr = expr; return n;
}

ASTNode *parser_parse(Parser *p) {
    ASTNode *prog = ast_node_new(NODE_PROGRAM, 1); prog->block.stmts = malloc(sizeof(ASTNode*) * 1024); int c = 0;
    while (p->current->type != TOKEN_EOF) {
        ASTNode *s = parse_stmt(p); if (s) prog->block.stmts[c++] = s;
        while (p->current->type == TOKEN_NEWLINE) advance(p);
    }
    prog->block.count = c; return prog;
}
ASTNode *parser_parse_source(const char *src, char *err, int len) { (void)err;(void)len; Parser *p = parser_new(src); ASTNode *res = parser_parse(p); parser_free(p); return res; }
