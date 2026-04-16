#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "formatter.h"
#include "lexer.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} FormatBuffer;

static void fb_append_n(FormatBuffer *b, const char *s, size_t n) {
    if (b->len + n + 1 >= b->cap) {
        while (b->len + n + 1 >= b->cap) b->cap = b->cap ? b->cap * 2 : 256;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void fb_append(FormatBuffer *b, const char *s) {
    fb_append_n(b, s, strlen(s));
}

static void fb_ch(FormatBuffer *b, char c) {
    fb_append_n(b, &c, 1);
}

static void fb_rstrip_space(FormatBuffer *b) {
    while (b->len > 0 && (b->data[b->len - 1] == ' ' || b->data[b->len - 1] == '\t')) {
        b->data[--b->len] = '\0';
    }
}

static bool fb_ends_newline(FormatBuffer *b) {
    return b->len == 0 || b->data[b->len - 1] == '\n';
}

static void fb_indent(FormatBuffer *b, int indent) {
    if (!fb_ends_newline(b)) return;
    for (int i = 0; i < indent; i++) fb_append(b, "    ");
}

static void fb_newline(FormatBuffer *b) {
    fb_rstrip_space(b);
    if (!fb_ends_newline(b)) fb_ch(b, '\n');
}

static bool is_word(TokenType t) {
    return t == TOKEN_IDENT || t == TOKEN_INT_LIT || t == TOKEN_FLOAT_LIT ||
           t == TOKEN_COMPLEX_LIT || t == TOKEN_STRING_LIT || t == TOKEN_FSTRING_LIT ||
           t == TOKEN_TRUE || t == TOKEN_FALSE || t == TOKEN_UNKNOWN || t == TOKEN_NULL ||
           t == TOKEN_LET || t == TOKEN_CONST || t == TOKEN_FUNC || t == TOKEN_RETURN ||
           t == TOKEN_IF || t == TOKEN_ELIF || t == TOKEN_ELSE || t == TOKEN_WHILE ||
           t == TOKEN_FOR || t == TOKEN_IN || t == TOKEN_NOT || t == TOKEN_AND ||
           t == TOKEN_OR || t == TOKEN_BREAK || t == TOKEN_CONTINUE || t == TOKEN_IMPORT ||
           t == TOKEN_OUTPUT || t == TOKEN_INPUT || t == TOKEN_LEN || t == TOKEN_BOOL_KW ||
           t == TOKEN_INT_KW || t == TOKEN_FLOAT_KW || t == TOKEN_STR_KW ||
           t == TOKEN_SET_KW || t == TOKEN_ARR_KW || t == TOKEN_TYPE_KW;
}

static bool is_operator(TokenType t) {
    return t == TOKEN_PLUS || t == TOKEN_MINUS || t == TOKEN_STAR || t == TOKEN_SLASH ||
           t == TOKEN_PERCENT || t == TOKEN_STARSTAR || t == TOKEN_EQ || t == TOKEN_EQEQ ||
           t == TOKEN_NEQ || t == TOKEN_LT || t == TOKEN_GT || t == TOKEN_LE ||
           t == TOKEN_GE || t == TOKEN_PLUS_EQ || t == TOKEN_MINUS_EQ ||
           t == TOKEN_STAR_EQ || t == TOKEN_SLASH_EQ || t == TOKEN_AMPAMP ||
           t == TOKEN_PIPEPIPE || t == TOKEN_AMP || t == TOKEN_PIPE ||
           t == TOKEN_CARET || t == TOKEN_ARROW;
}

static const char *keyword_text(TokenType t) {
    switch (t) {
        case TOKEN_LET: return "let";
        case TOKEN_CONST: return "const";
        case TOKEN_FUNC: return "func";
        case TOKEN_RETURN: return "return";
        case TOKEN_IF: return "if";
        case TOKEN_ELIF: return "elif";
        case TOKEN_ELSE: return "else";
        case TOKEN_WHILE: return "while";
        case TOKEN_FOR: return "for";
        case TOKEN_IN: return "in";
        case TOKEN_NOT: return "not";
        case TOKEN_AND: return "and";
        case TOKEN_OR: return "or";
        case TOKEN_TRUE: return "true";
        case TOKEN_FALSE: return "false";
        case TOKEN_UNKNOWN: return "unknown";
        case TOKEN_NULL: return "null";
        case TOKEN_BREAK: return "break";
        case TOKEN_CONTINUE: return "continue";
        case TOKEN_IMPORT: return "import";
        case TOKEN_OUTPUT: return "output";
        case TOKEN_INPUT: return "input";
        case TOKEN_LEN: return "len";
        case TOKEN_BOOL_KW: return "bool";
        case TOKEN_INT_KW: return "int";
        case TOKEN_FLOAT_KW: return "float";
        case TOKEN_STR_KW: return "str";
        case TOKEN_SET_KW: return "set";
        case TOKEN_ARR_KW: return "arr";
        case TOKEN_TYPE_KW: return "type";
        default: return NULL;
    }
}

static char *quote_string(const char *s, bool fstring) {
    size_t cap = strlen(s) * 2 + 8;
    char *out = malloc(cap);
    size_t len = 0;
    if (fstring) out[len++] = 'f';
    out[len++] = '"';
    for (const char *p = s; *p; p++) {
        if (len + 4 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        switch (*p) {
            case '\n': out[len++] = '\\'; out[len++] = 'n'; break;
            case '\t': out[len++] = '\\'; out[len++] = 't'; break;
            case '\r': out[len++] = '\\'; out[len++] = 'r'; break;
            case '"': out[len++] = '\\'; out[len++] = '"'; break;
            case '\\': out[len++] = '\\'; out[len++] = '\\'; break;
            default: out[len++] = *p; break;
        }
    }
    out[len++] = '"';
    out[len] = '\0';
    return out;
}

static char *token_text(Token *t) {
    const char *kw = keyword_text(t->type);
    if (kw) return strdup(kw);
    if (t->type == TOKEN_STRING_LIT) return quote_string(t->value ? t->value : "", false);
    if (t->type == TOKEN_FSTRING_LIT) return quote_string(t->value ? t->value : "", true);
    if (t->value && t->value[0]) return strdup(t->value);
    return strdup(token_type_name(t->type));
}

char *prism_format_source(const char *source, char *errbuf, int errlen) {
    Lexer *lexer = lexer_new(source ? source : "");
    FormatBuffer out = {0};
    int indent = 0;
    TokenType prev = TOKEN_EOF;

    for (;;) {
        Token *tok = lexer_next(lexer);
        if (tok->type == TOKEN_EOF) {
            token_free(tok);
            break;
        }
        if (tok->type == TOKEN_ERROR) {
            if (errbuf && errlen > 0)
                snprintf(errbuf, (size_t)errlen, "line %d:%d: unexpected token '%s'", tok->line, tok->col, tok->value ? tok->value : "");
            token_free(tok);
            lexer_free(lexer);
            free(out.data);
            return NULL;
        }
        if (tok->type == TOKEN_NEWLINE) {
            fb_newline(&out);
            token_free(tok);
            continue;
        }
        if (tok->type == TOKEN_RBRACE) {
            if (!fb_ends_newline(&out)) fb_newline(&out);
            if (indent > 0) indent--;
        }
        fb_indent(&out, indent);
        if (tok->type == TOKEN_COMMA) {
            fb_rstrip_space(&out);
            fb_append(&out, ", ");
        } else if (tok->type == TOKEN_SEMICOLON) {
            fb_newline(&out);
        } else if (tok->type == TOKEN_COLON) {
            fb_rstrip_space(&out);
            fb_append(&out, ": ");
        } else if (tok->type == TOKEN_DOT) {
            fb_rstrip_space(&out);
            fb_ch(&out, '.');
        } else if (tok->type == TOKEN_LPAREN || tok->type == TOKEN_LBRACKET) {
            if (!is_operator(prev)) fb_rstrip_space(&out);
            fb_append(&out, tok->type == TOKEN_LPAREN ? "(" : "[");
        } else if (tok->type == TOKEN_RPAREN || tok->type == TOKEN_RBRACKET) {
            fb_rstrip_space(&out);
            fb_append(&out, tok->type == TOKEN_RPAREN ? ")" : "]");
        } else if (tok->type == TOKEN_LBRACE) {
            fb_rstrip_space(&out);
            if (out.len > 0 && !fb_ends_newline(&out)) fb_ch(&out, ' ');
            fb_append(&out, "{");
            fb_newline(&out);
            indent++;
        } else if (tok->type == TOKEN_RBRACE) {
            fb_append(&out, "}");
        } else {
            char *text = token_text(tok);
            if (is_operator(tok->type)) {
                fb_rstrip_space(&out);
                if (out.len > 0 && !fb_ends_newline(&out)) fb_ch(&out, ' ');
                fb_append(&out, text);
                fb_ch(&out, ' ');
            } else {
                if (out.len > 0 && !fb_ends_newline(&out) && (is_word(prev) || prev == TOKEN_RPAREN || prev == TOKEN_RBRACKET))
                    fb_ch(&out, ' ');
                fb_append(&out, text);
            }
            free(text);
        }
        prev = tok->type;
        token_free(tok);
    }

    fb_rstrip_space(&out);
    if (!fb_ends_newline(&out)) fb_ch(&out, '\n');
    lexer_free(lexer);
    return out.data ? out.data : strdup("\n");
}

int prism_format_file(const char *path, int write_back, char *errbuf, int errlen) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errbuf && errlen > 0) snprintf(errbuf, (size_t)errlen, "cannot open '%s'", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *src = malloc((size_t)sz + 1);
    if (fread(src, 1, (size_t)sz, f) != (size_t)sz && ferror(f)) {
        fclose(f);
        free(src);
        if (errbuf && errlen > 0) snprintf(errbuf, (size_t)errlen, "cannot read '%s'", path);
        return 1;
    }
    src[sz] = '\0';
    fclose(f);
    char *formatted = prism_format_source(src, errbuf, errlen);
    free(src);
    if (!formatted) return 1;
    if (write_back) {
        f = fopen(path, "w");
        if (!f) {
            if (errbuf && errlen > 0) snprintf(errbuf, (size_t)errlen, "cannot write '%s'", path);
            free(formatted);
            return 1;
        }
        fputs(formatted, f);
        fclose(f);
    } else {
        fputs(formatted, stdout);
    }
    free(formatted);
    return 0;
}