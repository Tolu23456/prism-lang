#ifndef PARSER_H
#define PARSER_H
#include "lexer.h"
#include "ast.h"
typedef struct { Lexer *lexer; Token *current; int had_error; char error_msg[512]; } Parser;
Parser *parser_new(const char *src); void parser_free(Parser *p); ASTNode *parser_parse(Parser *p); ASTNode *parser_parse_source(const char *src, char *err, int len);
#endif
