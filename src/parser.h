#ifndef PARSER_H
#define PARSER_H
#include "lexer.h"
#include "ast.h"

/* Maximum parse errors collected per parse pass before giving up */
#define PARSER_MAX_ERRORS 32

typedef struct {
    int  line;
    int  col;
    char msg[384];
} ParseError;

typedef struct {
    Lexer      *lexer;
    Token      *current;
    Token      *peek;

    /* Original source (kept for caret-underline display) */
    const char *source;

    /* --- Multi-error collection --- */
    int         had_error;           /* non-zero if at least one error occurred */
    int         panic_mode;          /* 1 while recovering from an error */
    int         error_count;         /* number of errors collected so far */
    ParseError  errors[PARSER_MAX_ERRORS];

    /* Single-error compat: always mirrors errors[0] when had_error */
    char        error_msg[512];
} Parser;

/* lifecycle */
Parser  *parser_new(const char *source);
void     parser_free(Parser *p);
ASTNode *parser_parse(Parser *p);
ASTNode *parser_parse_source(const char *source, char *errbuf, int errlen);

/* Pretty-print all collected errors to stderr with file:line:col + caret underline.
 * filename may be NULL (shown as "<source>"). */
void     parser_print_errors(const Parser *p, const char *filename);

#endif
