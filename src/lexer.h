#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

typedef enum {
    /* Literals */
    TOKEN_INT_LIT,
    TOKEN_FLOAT_LIT,
    TOKEN_COMPLEX_LIT,
    TOKEN_STRING_LIT,
    TOKEN_FSTRING_LIT,

    /* Keywords */
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_FUNC,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELIF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_NOT,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_UNKNOWN,
    TOKEN_NULL,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_IMPORT,

    /* Built-in function keywords */
    TOKEN_OUTPUT,
    TOKEN_INPUT,
    TOKEN_LEN,
    TOKEN_BOOL_KW,
    TOKEN_INT_KW,
    TOKEN_FLOAT_KW,
    TOKEN_STR_KW,
    TOKEN_SET_KW,
    TOKEN_ARR_KW,
    TOKEN_TYPE_KW,

    /* Identifier */
    TOKEN_IDENT,

    /* Arithmetic */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_STARSTAR,

    /* Comparison */
    TOKEN_EQ,
    TOKEN_EQEQ,
    TOKEN_NEQ,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LE,
    TOKEN_GE,

    /* Compound assignment */
    TOKEN_PLUS_EQ,
    TOKEN_MINUS_EQ,
    TOKEN_STAR_EQ,
    TOKEN_SLASH_EQ,

    /* Logical / bitwise */
    TOKEN_AMPAMP,
    TOKEN_PIPEPIPE,
    TOKEN_BANG,
    TOKEN_AMP,
    TOKEN_PIPE,
    TOKEN_CARET,
    TOKEN_TILDE,

    /* Punctuation */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_DOT,
    TOKEN_AT,
    TOKEN_ARROW,

    /* Special */
    TOKEN_NEWLINE,
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    char     *value;
    bool      interned; /* true = value is an interned canonical ptr (do not free) */
    int       line;
    int       col;
} Token;

typedef struct {
    const char *source;
    int         pos;
    int         line;
    int         col;
    int         len;
} Lexer;

Lexer  *lexer_new(const char *source);
void    lexer_free(Lexer *lexer);
Token  *lexer_next(Lexer *lexer);
void    token_free(Token *token);
const char *token_type_name(TokenType type);

#endif
