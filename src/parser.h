#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer  *lexer;
    Token  *current;
    Token  *peek;
    int     had_error;
    char    error_msg[512];
} Parser;

Parser  *parser_new(const char *source);
void     parser_free(Parser *p);
ASTNode *parser_parse(Parser *p);

#endif
