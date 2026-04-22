#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "gc.h"

static char cur(Lexer *l)  { return l->pos < l->len ? l->source[l->pos] : '\0'; }
static char peek1(Lexer *l){ return (l->pos+1) < l->len ? l->source[l->pos+1] : '\0'; }
static void advance(Lexer *l) { if (l->pos < l->len) { if (l->source[l->pos] == '\n') { l->line++; l->col = 1; } else l->col++; l->pos++; } }

static Token *make_token(Lexer *l, TokenType type, const char *val) {
    Token *t = malloc(sizeof(Token)); t->type = type; t->value = val ? strdup(val) : NULL;
    t->interned = false; t->line = l->line; t->col = l->col; return t;
}

Lexer *lexer_new(const char *source) { Lexer *l = malloc(sizeof(Lexer)); l->source = source; l->pos = 0; l->line = 1; l->col = 1; l->len = (int)strlen(source); return l; }
void lexer_free(Lexer *l) { free(l); }
void token_free(Token *t) { if (t) { if (!t->interned) free(t->value); free(t); } }

typedef struct { const char *word; TokenType type; } KW;
static const KW KEYWORDS[] = {
    {"let",TOKEN_LET},{"func",TOKEN_FUNC},{"fn",TOKEN_FN},{"return",TOKEN_RETURN},
    {"if",TOKEN_IF},{"else",TOKEN_ELSE},{"while",TOKEN_WHILE},{"for",TOKEN_FOR},
    {"in",TOKEN_IN},{"true",TOKEN_TRUE},{"false",TOKEN_FALSE},{"null",TOKEN_NULL},
    {"step",TOKEN_STEP},{"range",TOKEN_RANGE_KW},
    {NULL, 0}
};

Token *lexer_next(Lexer *l) {
    while (cur(l) == ' ' || cur(l) == '\t' || cur(l) == '\r') advance(l);
    if (l->pos >= l->len) return make_token(l, TOKEN_EOF, "");
    if (cur(l) == '#') { while (l->pos < l->len && cur(l) != '\n') advance(l); return lexer_next(l); }
    if (cur(l) == '\n') { advance(l); return make_token(l, TOKEN_NEWLINE, "\n"); }
    char c = cur(l);
    if (isdigit(c)) {
        int cap = 64, sz = 0; char *buf = malloc(cap);
        while (isdigit(cur(l))) { buf[sz++] = cur(l); advance(l); }
        if (cur(l) == '.' && isdigit(peek1(l))) {
             buf[sz++] = cur(l); advance(l);
             while (isdigit(cur(l))) { buf[sz++] = cur(l); advance(l); }
             buf[sz] = 0; Token *t = make_token(l, TOKEN_FLOAT_LIT, buf); free(buf); return t;
        }
        buf[sz] = 0; Token *t = make_token(l, TOKEN_INT_LIT, buf); free(buf); return t;
    }
    if (isalpha(c) || c == '_') {
        int cap = 64, sz = 0; char *buf = malloc(cap);
        while (isalnum(cur(l)) || cur(l) == '_') { buf[sz++] = cur(l); advance(l); }
        buf[sz] = 0; TokenType tt = TOKEN_IDENT;
        for (int i = 0; KEYWORDS[i].word; i++) if (strcmp(KEYWORDS[i].word, buf) == 0) { tt = KEYWORDS[i].type; break; }
        if (tt == TOKEN_IDENT) {
            const char *canon = gc_intern_cstr(gc_global(), buf); free(buf);
            Token *t = malloc(sizeof(Token)); t->type = TOKEN_IDENT; t->value = (char*)canon;
            t->interned = true; t->line = l->line; t->col = l->col; return t;
        }
        Token *t = make_token(l, tt, buf); free(buf); return t;
    }
    if (c == '"') {
        advance(l); int cap = 64, sz = 0; char *buf = malloc(cap);
        while (cur(l) != '"' && cur(l) != 0) {
            buf[sz++] = cur(l); advance(l);
            if (sz + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        }
        if (cur(l) == '"') advance(l);
        buf[sz] = 0; Token *t = make_token(l, TOKEN_STRING_LIT, buf); free(buf); return t;
    }
    advance(l);
    if (c == '.' && cur(l) == '.') { advance(l); return make_token(l, TOKEN_DOTDOT, ".."); }
    if (c == '(') return make_token(l, TOKEN_LPAREN, "(");
    if (c == ')') return make_token(l, TOKEN_RPAREN, ")");
    if (c == '{') return make_token(l, TOKEN_LBRACE, "{");
    if (c == '}') return make_token(l, TOKEN_RBRACE, "}");
    if (c == ',') return make_token(l, TOKEN_COMMA, ",");
    if (c == '=') return make_token(l, TOKEN_EQ, "=");
    return make_token(l, TOKEN_ERROR, "");
}
const char *token_type_name(TokenType t) { (void)t; return "TOKEN"; }
