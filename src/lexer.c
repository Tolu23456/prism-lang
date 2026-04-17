#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "gc.h"

/* ------------------------------------------------------------------ helpers */

static char cur(Lexer *l)  { return l->pos < l->len ? l->source[l->pos] : '\0'; }
static char peek1(Lexer *l){ return (l->pos+1) < l->len ? l->source[l->pos+1] : '\0'; }
static char peek2(Lexer *l){ return (l->pos+2) < l->len ? l->source[l->pos+2] : '\0'; }

static void advance(Lexer *l) {
    if (l->pos < l->len) {
        if (l->source[l->pos] == '\n') { l->line++; l->col = 1; }
        else                           { l->col++; }
        l->pos++;
    }
}

static Token *make_token(Lexer *l, TokenType type, const char *val) {
    Token *t    = malloc(sizeof(Token));
    t->type     = type;
    t->value    = val ? strdup(val) : NULL;
    t->interned = false;
    t->line     = l->line;
    t->col      = l->col;
    return t;
}

static Token *make_token_char(Lexer *l, TokenType type, char c) {
    char buf[2] = {c, '\0'};
    return make_token(l, type, buf);
}

/* ------------------------------------------------------------------ Lexer lifecycle */

Lexer *lexer_new(const char *source) {
    Lexer *l = malloc(sizeof(Lexer));
    l->source = source;
    l->pos    = 0;
    l->line   = 1;
    l->col    = 1;
    l->len    = (int)strlen(source);
    return l;
}

void lexer_free(Lexer *l) { free(l); }

void token_free(Token *t) {
    if (t) {
        if (!t->interned) free(t->value);
        free(t);
    }
}

/* ------------------------------------------------------------------ keyword map */

typedef struct { const char *word; TokenType type; } KW;
static const KW KEYWORDS[] = {
    {"let",      TOKEN_LET},
    {"const",    TOKEN_CONST},
    {"func",     TOKEN_FUNC},
    {"fn",       TOKEN_FN},
    {"return",   TOKEN_RETURN},
    {"if",       TOKEN_IF},
    {"elif",     TOKEN_ELIF},
    {"else",     TOKEN_ELSE},
    {"while",    TOKEN_WHILE},
    {"for",      TOKEN_FOR},
    {"in",       TOKEN_IN},
    {"not",      TOKEN_NOT},
    {"and",      TOKEN_AND},
    {"or",       TOKEN_OR},
    {"true",     TOKEN_TRUE},
    {"false",    TOKEN_FALSE},
    {"unknown",  TOKEN_UNKNOWN},
    {"null",     TOKEN_NULL},
    {"break",    TOKEN_BREAK},
    {"continue", TOKEN_CONTINUE},
    {"import",   TOKEN_IMPORT},
    {"repeat",   TOKEN_REPEAT},
    {"until",    TOKEN_UNTIL},
    {"step",     TOKEN_STEP},
    {"try",      TOKEN_TRY},
    {"catch",    TOKEN_CATCH},
    {"throw",    TOKEN_THROW},
    {"match",    TOKEN_MATCH},
    {"when",     TOKEN_WHEN},
    {"is",       TOKEN_IS},
    {"from",     TOKEN_FROM},
    {"as",       TOKEN_AS},
    {"class",    TOKEN_CLASS},
    {"struct",   TOKEN_STRUCT},
    {"new",      TOKEN_NEW},
    {"self",     TOKEN_SELF},
    {"output",   TOKEN_OUTPUT},
    {"input",    TOKEN_INPUT},
    {"len",      TOKEN_LEN},
    {"bool",     TOKEN_BOOL_KW},
    {"int",      TOKEN_INT_KW},
    {"float",    TOKEN_FLOAT_KW},
    {"str",      TOKEN_STR_KW},
    {"set",      TOKEN_SET_KW},
    {"arr",      TOKEN_ARR_KW},
    {"type",     TOKEN_TYPE_KW},
    {"dict",     TOKEN_DICT_KW},
    {NULL, 0}
};

static TokenType keyword_type(const char *word) {
    for (int i = 0; KEYWORDS[i].word; i++)
        if (strcmp(KEYWORDS[i].word, word) == 0) return KEYWORDS[i].type;
    return TOKEN_IDENT;
}

/* ------------------------------------------------------------------ string helpers */

/* Returns true if a double-quoted string content contains an unescaped { */
static bool has_interpolation(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '\\') { p++; continue; } /* skip escaped chars */
        if (*p == '{' && *(p+1) != '{')    /* {{ is escaped brace, not interpolation */
            return true;
    }
    return false;
}

static char *read_string(Lexer *l, char quote, bool verbatim, bool fstr) {
    /* caller already consumed the opening quote (and prefix chars) */
    int   cap = 64, sz = 0;
    char *buf = malloc(cap);

    /* triple-quote detection */
    bool triple = false;
    if (cur(l) == quote && peek1(l) == quote) {
        advance(l); advance(l);  /* consume 2nd and 3rd quote chars */
        triple = true;
    }

    while (l->pos < l->len) {
        char c = cur(l);
        if (triple) {
            if (c == quote && peek1(l) == quote && peek2(l) == quote) {
                advance(l); advance(l); advance(l);
                break;
            }
        } else {
            if (c == quote) { advance(l); break; }
            if (c == '\n')  { break; } /* unterminated */
        }

        if (!verbatim && c == '\\') {
            advance(l);
            char esc = cur(l); advance(l);
            char ec;
            switch (esc) {
                case 'n':  ec = '\n'; break;
                case 't':  ec = '\t'; break;
                case 'r':  ec = '\r'; break;
                case '\\': ec = '\\'; break;
                case '\'': ec = '\''; break;
                case '"':  ec = '"';  break;
                case '0':  ec = '\0'; break;
                default:   ec = esc;  break;
            }
            if (sz+1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[sz++] = ec;
        } else {
            if (sz+1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[sz++] = c;
            advance(l);
        }
    }
    (void)fstr;
    buf[sz] = '\0';
    return buf;
}

/* Strip underscore separators from a numeric string (modifies copy) */
static char *strip_underscores(const char *src) {
    char *buf = malloc(strlen(src) + 1);
    char *dst = buf;
    for (const char *p = src; *p; p++) {
        if (*p != '_') *dst++ = *p;
    }
    *dst = '\0';
    return buf;
}

/* ------------------------------------------------------------------ main tokenizer */

Token *lexer_next(Lexer *l) {
    /* skip spaces and tabs */
    while (cur(l) == ' ' || cur(l) == '\t' || cur(l) == '\r') advance(l);

    if (l->pos >= l->len) return make_token(l, TOKEN_EOF, "");

    /* # line comment */
    if (cur(l) == '#') {
        while (l->pos < l->len && cur(l) != '\n') advance(l);
        return lexer_next(l);
    }

    /* block comment: slash-star ... star-slash */
    if (cur(l) == '/' && peek1(l) == '*') {
        advance(l); advance(l); /* skip slash-star */
        while (l->pos < l->len && !(cur(l) == '*' && peek1(l) == '/')) {
            if (cur(l) == '\n') l->line++;
            advance(l);
        }
        if (l->pos < l->len) { advance(l); advance(l); } /* skip star-slash */
        return lexer_next(l);
    }

    /* newline */
    if (cur(l) == '\n') {
        advance(l);
        return make_token(l, TOKEN_NEWLINE, "\n");
    }

    char c = cur(l);

    /* ---- numbers ---- */
    if (isdigit(c) || (c == '0' && (peek1(l) == 'x' || peek1(l) == 'b' ||
                                     peek1(l) == 'o' || peek1(l) == 'O'))) {
        int   cap = 64, sz = 0;
        char *buf = malloc(cap);
        bool  is_float = false, is_complex = false;
        bool  is_hex = false, is_bin = false, is_oct = false;

        if (c == '0' && peek1(l) == 'x') { /* hex */
            buf[sz++] = cur(l); advance(l);
            buf[sz++] = cur(l); advance(l);
            is_hex = true;
            while (isxdigit(cur(l)) || cur(l) == '_') {
                if (cur(l) != '_') buf[sz++] = cur(l);
                advance(l);
            }
        } else if (c == '0' && peek1(l) == 'b') { /* binary */
            buf[sz++] = cur(l); advance(l);
            buf[sz++] = cur(l); advance(l);
            is_bin = true;
            while (cur(l) == '0' || cur(l) == '1' || cur(l) == '_') {
                if (cur(l) != '_') buf[sz++] = cur(l);
                advance(l);
            }
        } else if (c == '0' && (peek1(l) == 'o' || peek1(l) == 'O')) { /* octal */
            advance(l); advance(l); /* skip 0o */
            is_oct = true;
            while ((cur(l) >= '0' && cur(l) <= '7') || cur(l) == '_') {
                if (cur(l) != '_') buf[sz++] = cur(l);
                advance(l);
            }
        } else {
            while (isdigit(cur(l)) || cur(l) == '_') {
                if (cur(l) != '_') buf[sz++] = cur(l);
                advance(l);
            }
            /* Only lex . as float if not followed by another . (avoid 1..10 parsing as float) */
            if (cur(l) == '.' && isdigit(peek1(l))) {
                is_float = true;
                buf[sz++] = cur(l); advance(l);
                while (isdigit(cur(l)) || cur(l) == '_') {
                    if (cur(l) != '_') buf[sz++] = cur(l);
                    advance(l);
                }
            }
            if (cur(l) == 'e' || cur(l) == 'E') {
                is_float = true;
                buf[sz++] = cur(l); advance(l);
                if (cur(l) == '+' || cur(l) == '-') { buf[sz++] = cur(l); advance(l); }
                while (isdigit(cur(l))) { buf[sz++] = cur(l); advance(l); }
            }
        }
        if (cur(l) == 'j') { /* complex */
            is_complex = true;
            advance(l);
        }
        buf[sz] = '\0';

        TokenType tt;
        if (is_complex)     tt = TOKEN_COMPLEX_LIT;
        else if (is_float)  tt = TOKEN_FLOAT_LIT;
        else if (is_oct) {
            /* convert octal string to decimal for storage */
            long long oval = strtoll(buf, NULL, 8);
            char dbuf[32];
            snprintf(dbuf, sizeof(dbuf), "%lld", oval);
            Token *t = make_token(l, TOKEN_INT_LIT, dbuf);
            free(buf);
            return t;
        }
        else                tt = TOKEN_INT_LIT;

        Token *t = make_token(l, tt, buf);
        free(buf);
        return t;
    }

    /* ---- strings ---- */
    if (c == '"' || c == '\'') {
        char quote = c; advance(l);
        char *s = read_string(l, quote, false, false);
        TokenType tt = TOKEN_STRING_LIT;
        /* Auto-interpolation: double-quoted strings with {expr} become f-strings */
        if (quote == '"' && has_interpolation(s))
            tt = TOKEN_FSTRING_LIT;
        Token *t = make_token(l, tt, s);
        free(s);
        return t;
    }

    /* f-string */
    if (c == 'f' && (peek1(l) == '"' || peek1(l) == '\'')) {
        advance(l);
        char quote = cur(l); advance(l);
        char *s = read_string(l, quote, false, true);
        Token *t = make_token(l, TOKEN_FSTRING_LIT, s);
        free(s);
        return t;
    }

    /* verbatim string */
    if (c == '@' && (peek1(l) == '"' || peek1(l) == '\'')) {
        advance(l);
        char quote = cur(l); advance(l);
        char *s = read_string(l, quote, true, false);
        Token *t = make_token(l, TOKEN_STRING_LIT, s);
        free(s);
        return t;
    }

    /* ---- identifiers / keywords ---- */
    if (isalpha(c) || c == '_') {
        int   cap = 64, sz = 0;
        char *buf = malloc(cap);
        while (isalnum(cur(l)) || cur(l) == '_') {
            if (sz+1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[sz++] = cur(l);
            advance(l);
        }
        buf[sz] = '\0';
        TokenType tt = keyword_type(buf);
        if (tt == TOKEN_IDENT) {
            /* Intern identifier tokens at lex time.
             * The canonical pointer is immortal — no strdup, no free later. */
            const char *canon = gc_intern_cstr(gc_global(), buf);
            free(buf);
            Token *t    = malloc(sizeof(Token));
            t->type     = TOKEN_IDENT;
            t->value    = (char *)canon;
            t->interned = true;
            t->line     = l->line;
            t->col      = l->col;
            return t;
        }
        Token *t = make_token(l, tt, buf);
        free(buf);
        return t;
    }

    /* ---- operators & punctuation ---- */
    advance(l); /* consume c */
    switch (c) {
        case '+':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_PLUS_EQ, "+="); }
            return make_token_char(l, TOKEN_PLUS, '+');
        case '-':
            if (cur(l) == '>') { advance(l); return make_token(l, TOKEN_ARROW, "->"); }
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_MINUS_EQ, "-="); }
            return make_token_char(l, TOKEN_MINUS, '-');
        case '*':
            if (cur(l) == '*') {
                advance(l);
                if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_STARSTAR_EQ, "**="); }
                return make_token(l, TOKEN_STARSTAR, "**");
            }
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_STAR_EQ, "*="); }
            return make_token_char(l, TOKEN_STAR, '*');
        case '/':
            if (cur(l) == '/') {
                advance(l);
                return make_token(l, TOKEN_SLASH_SLASH, "//");
            }
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_SLASH_EQ, "/="); }
            return make_token_char(l, TOKEN_SLASH, '/');
        case '%':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_PERCENT_EQ, "%="); }
            return make_token_char(l, TOKEN_PERCENT, '%');
        case '=':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_EQEQ, "=="); }
            if (cur(l) == '>') { advance(l); return make_token(l, TOKEN_FAT_ARROW, "=>"); }
            return make_token_char(l, TOKEN_EQ, '=');
        case '!':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_NEQ, "!="); }
            return make_token_char(l, TOKEN_BANG, '!');
        case '<':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_LE, "<="); }
            if (cur(l) == '<') { advance(l); return make_token(l, TOKEN_LSHIFT, "<<"); }
            return make_token_char(l, TOKEN_LT, '<');
        case '>':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_GE, ">="); }
            if (cur(l) == '>') { advance(l); return make_token(l, TOKEN_RSHIFT, ">>"); }
            return make_token_char(l, TOKEN_GT, '>');
        case '&':
            if (cur(l) == '&') { advance(l); return make_token(l, TOKEN_AMPAMP, "&&"); }
            return make_token_char(l, TOKEN_AMP, '&');
        case '|':
            if (cur(l) == '|') { advance(l); return make_token(l, TOKEN_PIPEPIPE, "||"); }
            if (cur(l) == '>') { advance(l); return make_token(l, TOKEN_PIPE_ARROW, "|>"); }
            return make_token_char(l, TOKEN_PIPE, '|');
        case '^': return make_token_char(l, TOKEN_CARET, '^');
        case '~': return make_token_char(l, TOKEN_TILDE, '~');
        case '(': return make_token_char(l, TOKEN_LPAREN, '(');
        case ')': return make_token_char(l, TOKEN_RPAREN, ')');
        case '[': return make_token_char(l, TOKEN_LBRACKET, '[');
        case ']': return make_token_char(l, TOKEN_RBRACKET, ']');
        case '{': return make_token_char(l, TOKEN_LBRACE, '{');
        case '}': return make_token_char(l, TOKEN_RBRACE, '}');
        case ',': return make_token_char(l, TOKEN_COMMA, ',');
        case ';': return make_token_char(l, TOKEN_SEMICOLON, ';');
        case ':':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_WALRUS, ":="); }
            return make_token_char(l, TOKEN_COLON, ':');
        case '.':
            if (cur(l) == '.') {
                advance(l);
                if (cur(l) == '.') { advance(l); return make_token(l, TOKEN_ELLIPSIS, "..."); }
                return make_token(l, TOKEN_DOTDOT, "..");
            }
            return make_token_char(l, TOKEN_DOT, '.');
        case '@': return make_token_char(l, TOKEN_AT, '@');
        case '?':
            if (cur(l) == '.') { advance(l); return make_token(l, TOKEN_SAFE_DOT, "?."); }
            if (cur(l) == '?') { advance(l); return make_token(l, TOKEN_NULLCOAL, "??"); }
            return make_token_char(l, TOKEN_QUESTION, '?');
        default: {
            char buf[2] = {c, '\0'};
            return make_token(l, TOKEN_ERROR, buf);
        }
    }
}

/* ------------------------------------------------------------------ name helper */

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOKEN_INT_LIT:    return "INT";
        case TOKEN_FLOAT_LIT:  return "FLOAT";
        case TOKEN_COMPLEX_LIT:return "COMPLEX";
        case TOKEN_STRING_LIT: return "STRING";
        case TOKEN_FSTRING_LIT:return "FSTRING";
        case TOKEN_LET:        return "let";
        case TOKEN_CONST:      return "const";
        case TOKEN_FUNC:       return "func";
        case TOKEN_FN:         return "fn";
        case TOKEN_RETURN:     return "return";
        case TOKEN_IF:         return "if";
        case TOKEN_ELIF:       return "elif";
        case TOKEN_ELSE:       return "else";
        case TOKEN_WHILE:      return "while";
        case TOKEN_FOR:        return "for";
        case TOKEN_IN:         return "in";
        case TOKEN_NOT:        return "not";
        case TOKEN_AND:        return "and";
        case TOKEN_OR:         return "or";
        case TOKEN_TRUE:       return "true";
        case TOKEN_FALSE:      return "false";
        case TOKEN_UNKNOWN:    return "unknown";
        case TOKEN_NULL:       return "null";
        case TOKEN_BREAK:      return "break";
        case TOKEN_CONTINUE:   return "continue";
        case TOKEN_IMPORT:     return "import";
        case TOKEN_REPEAT:     return "repeat";
        case TOKEN_UNTIL:      return "until";
        case TOKEN_STEP:       return "step";
        case TOKEN_TRY:        return "try";
        case TOKEN_CATCH:      return "catch";
        case TOKEN_THROW:      return "throw";
        case TOKEN_MATCH:      return "match";
        case TOKEN_WHEN:       return "when";
        case TOKEN_IS:         return "is";
        case TOKEN_FROM:       return "from";
        case TOKEN_AS:         return "as";
        case TOKEN_CLASS:      return "class";
        case TOKEN_STRUCT:     return "struct";
        case TOKEN_NEW:        return "new";
        case TOKEN_SELF:       return "self";
        case TOKEN_OUTPUT:     return "output";
        case TOKEN_INPUT:      return "input";
        case TOKEN_IDENT:      return "IDENT";
        case TOKEN_PLUS:       return "+";
        case TOKEN_MINUS:      return "-";
        case TOKEN_STAR:       return "*";
        case TOKEN_SLASH:      return "/";
        case TOKEN_PERCENT:    return "%";
        case TOKEN_STARSTAR:     return "**";
        case TOKEN_SLASH_SLASH:  return "//";
        case TOKEN_EQ:           return "=";
        case TOKEN_EQEQ:         return "==";
        case TOKEN_NEQ:          return "!=";
        case TOKEN_LT:           return "<";
        case TOKEN_GT:           return ">";
        case TOKEN_LE:           return "<=";
        case TOKEN_GE:           return ">=";
        case TOKEN_PLUS_EQ:      return "+=";
        case TOKEN_MINUS_EQ:     return "-=";
        case TOKEN_STAR_EQ:      return "*=";
        case TOKEN_SLASH_EQ:     return "/=";
        case TOKEN_PERCENT_EQ:   return "%=";
        case TOKEN_STARSTAR_EQ:  return "**=";
        case TOKEN_AMPAMP:       return "&&";
        case TOKEN_PIPEPIPE:     return "||";
        case TOKEN_BANG:         return "!";
        case TOKEN_AMP:          return "&";
        case TOKEN_PIPE:         return "|";
        case TOKEN_CARET:        return "^";
        case TOKEN_TILDE:        return "~";
        case TOKEN_LSHIFT:       return "<<";
        case TOKEN_RSHIFT:       return ">>";
        case TOKEN_DOTDOT:       return "..";
        case TOKEN_ELLIPSIS:     return "...";
        case TOKEN_SAFE_DOT:     return "?.";
        case TOKEN_NULLCOAL:     return "??";
        case TOKEN_WALRUS:       return ":=";
        case TOKEN_PIPE_ARROW:   return "|>";
        case TOKEN_FAT_ARROW:    return "=>";
        case TOKEN_LPAREN:     return "(";
        case TOKEN_RPAREN:     return ")";
        case TOKEN_LBRACKET:   return "[";
        case TOKEN_RBRACKET:   return "]";
        case TOKEN_LBRACE:     return "{";
        case TOKEN_RBRACE:     return "}";
        case TOKEN_COMMA:      return ",";
        case TOKEN_SEMICOLON:  return ";";
        case TOKEN_COLON:      return ":";
        case TOKEN_DOT:        return ".";
        case TOKEN_QUESTION:   return "?";
        case TOKEN_NEWLINE:    return "NEWLINE";
        case TOKEN_EOF:        return "EOF";
        case TOKEN_ERROR:      return "ERROR";
        default:               return "?";
    }
}
