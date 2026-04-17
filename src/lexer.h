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
    TOKEN_FN,           /* fn — anonymous function keyword */
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
    TOKEN_LINK,         /* link — PSS stylesheet linking */

    /* New keywords */
    TOKEN_REPEAT,
    TOKEN_UNTIL,
    TOKEN_STEP,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_THROW,
    TOKEN_MATCH,
    TOKEN_WHEN,
    TOKEN_IS,
    TOKEN_FROM,
    TOKEN_AS,
    TOKEN_CLASS,
    TOKEN_STRUCT,       /* struct — lightweight record type */
    TOKEN_NEW,          /* new — instantiate class/struct */
    TOKEN_SELF,

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
    TOKEN_DICT_KW,

    /* Identifier */
    TOKEN_IDENT,

    /* Arithmetic */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_STARSTAR,
    TOKEN_SLASH_SLASH,  /* // integer division */

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
    TOKEN_PERCENT_EQ,
    TOKEN_STARSTAR_EQ,

    /* Logical / bitwise */
    TOKEN_AMPAMP,
    TOKEN_PIPEPIPE,
    TOKEN_BANG,
    TOKEN_AMP,
    TOKEN_PIPE,
    TOKEN_CARET,
    TOKEN_TILDE,

    /* Shift operators */
    TOKEN_LSHIFT,   /* << */
    TOKEN_RSHIFT,   /* >> */

    /* New operators */
    TOKEN_DOTDOT,       /* .. range */
    TOKEN_ELLIPSIS,     /* ... spread/varargs */
    TOKEN_SAFE_DOT,     /* ?. safe member access */
    TOKEN_NULLCOAL,     /* ?? null coalescing */
    TOKEN_WALRUS,       /* := declare-assign */
    TOKEN_PIPE_ARROW,   /* |> pipe */
    TOKEN_FAT_ARROW,    /* => arrow for fn expressions */

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
    TOKEN_QUESTION,

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
