#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

/* ------------------------------------------------------------------ helpers */

static void skip_newlines(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_block(Parser *p);
static ASTNode *parse_nullcoal(Parser *p);
static ASTNode *parse_range(Parser *p);
static ASTNode *parse_logical_or(Parser *p);

static Token *advance(Parser *p) {
    Token *prev = p->current;
    p->current = p->peek;
    p->peek = lexer_next(p->lexer);
    return prev;
}

static bool check(Parser *p, TokenType t) { return p->current->type == t; }
static bool check_peek(Parser *p, TokenType t) { return p->peek->type == t; }

/* Allow "soft keywords" and type keywords to be used as identifiers */
static bool is_name_token(TokenType t) {
    return t == TOKEN_IDENT   || t == TOKEN_ARR_KW   || t == TOKEN_SET_KW   ||
           t == TOKEN_BOOL_KW || t == TOKEN_INT_KW    || t == TOKEN_FLOAT_KW ||
           t == TOKEN_STR_KW  || t == TOKEN_TYPE_KW   || t == TOKEN_LEN      ||
           t == TOKEN_OUTPUT  || t == TOKEN_INPUT      || t == TOKEN_DICT_KW  ||
           t == TOKEN_SELF;
}

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { advance(p); return true; }
    return false;
}

static void skip_newlines(Parser *p) {
    while (check(p, TOKEN_NEWLINE)) advance(p);
}

static void error_at(Parser *p, const char *msg) {
    if (p->had_error) return;
    p->had_error = 1;
    snprintf(p->error_msg, sizeof(p->error_msg),
             "line %d: %s (got '%s')", p->current->line, msg,
             p->current->value ? p->current->value : token_type_name(p->current->type));
}

static Token *expect(Parser *p, TokenType t, const char *msg) {
    if (check(p, t)) return advance(p);
    error_at(p, msg);
    return p->current;
}

/* consume optional semicolons / newlines as statement terminators */
static void consume_stmt_end(Parser *p) {
    while (check(p, TOKEN_SEMICOLON) || check(p, TOKEN_NEWLINE)) advance(p);
}

/* ------------------------------------------------------------------ Parser lifecycle */

Parser *parser_new(const char *source) {
    Parser *p   = calloc(1, sizeof(Parser));
    p->lexer    = lexer_new(source);
    p->current  = lexer_next(p->lexer);
    p->peek     = lexer_next(p->lexer);
    return p;
}

void parser_free(Parser *p) {
    if (!p) return;
    token_free(p->current);
    token_free(p->peek);
    lexer_free(p->lexer);
    free(p);
}

/* ================================================================== expressions */

static ASTNode *parse_primary(Parser *p);
static ASTNode *parse_postfix(Parser *p);
static ASTNode *parse_unary(Parser *p);
static ASTNode *parse_power(Parser *p);
static ASTNode *parse_mul(Parser *p);
static ASTNode *parse_add(Parser *p);
static ASTNode *parse_compare(Parser *p);
static ASTNode *parse_equality(Parser *p);
static ASTNode *parse_bitand(Parser *p);
static ASTNode *parse_bitxor(Parser *p);
static ASTNode *parse_bitor(Parser *p);
static ASTNode *parse_logical_and(Parser *p);
static ASTNode *parse_logical_or(Parser *p);
static ASTNode *parse_range(Parser *p);
static ASTNode *parse_nullcoal(Parser *p);
static ASTNode *parse_assign(Parser *p);

/* ---- primary ---- */

static ASTNode **parse_arg_list(Parser *p, int *out_count) {
    *out_count = 0;
    int cap = 4;
    ASTNode **args = malloc(cap * sizeof(ASTNode *));
    skip_newlines(p);
    while (!check(p, TOKEN_RPAREN) && !check(p, TOKEN_EOF)) {
        if (*out_count >= cap) { cap *= 2; args = realloc(args, cap * sizeof(ASTNode *)); }
        args[(*out_count)++] = parse_expr(p);
        if (p->had_error) { free(args); return NULL; }
        skip_newlines(p);
        if (!match(p, TOKEN_COMMA)) break;
        skip_newlines(p);
    }
    return args;
}

static ASTNode *parse_fstring(Parser *p, const char *raw, int line) {
    ASTNode *n = ast_node_new(NODE_FSTRING_LIT, line);
    n->string_lit.value = strdup(raw);
    return n;
}

static ASTNode *parse_array_literal(Parser *p, int line, bool use_arr_kw) {
    /* opening '[' already consumed */
    (void)use_arr_kw;
    ASTNode *n  = ast_node_new(NODE_ARRAY_LIT, line);
    n->list_lit.items = malloc(8 * sizeof(ASTNode *));
    n->list_lit.count = 0;
    int cap = 8;
    skip_newlines(p);
    while (!check(p, TOKEN_RBRACKET) && !check(p, TOKEN_EOF)) {
        if (n->list_lit.count >= cap) { cap *= 2; n->list_lit.items = realloc(n->list_lit.items, cap * sizeof(ASTNode *)); }
        n->list_lit.items[n->list_lit.count++] = parse_expr(p);
        if (p->had_error) { ast_node_free(n); return NULL; }
        skip_newlines(p);
        if (!match(p, TOKEN_COMMA)) break;
        skip_newlines(p);
    }
    expect(p, TOKEN_RBRACKET, "expected ']' after array literal");
    return n;
}

/* parse {key:val, ...} as dict or {val, val, ...} as set */
static ASTNode *parse_brace_literal(Parser *p, int line) {
    /* opening '{' already consumed */
    skip_newlines(p);

    /* empty set literal {} */
    if (check(p, TOKEN_RBRACE)) {
        advance(p);
        ASTNode *n = ast_node_new(NODE_SET_LIT, line);
        n->list_lit.items = malloc(1 * sizeof(ASTNode *));
        n->list_lit.count = 0;
        return n;
    }

    /* peek ahead: if first expr is followed by ':', it's a dict */
    ASTNode *first = parse_expr(p);
    if (p->had_error) { ast_node_free(first); return NULL; }
    skip_newlines(p);

    if (check(p, TOKEN_COLON)) {
        /* dict literal */
        advance(p); /* consume ':' */
        skip_newlines(p);
        ASTNode *n = ast_node_new(NODE_DICT_LIT, line);
        int cap = 8;
        n->dict_lit.keys = malloc(cap * sizeof(ASTNode *));
        n->dict_lit.vals = malloc(cap * sizeof(ASTNode *));
        n->dict_lit.count = 0;
        n->dict_lit.keys[n->dict_lit.count] = first;
        n->dict_lit.vals[n->dict_lit.count] = parse_expr(p);
        n->dict_lit.count++;
        skip_newlines(p);
        while (match(p, TOKEN_COMMA)) {
            skip_newlines(p);
            if (check(p, TOKEN_RBRACE)) break;
            if (n->dict_lit.count >= cap) {
                cap *= 2;
                n->dict_lit.keys = realloc(n->dict_lit.keys, cap * sizeof(ASTNode *));
                n->dict_lit.vals = realloc(n->dict_lit.vals, cap * sizeof(ASTNode *));
            }
            n->dict_lit.keys[n->dict_lit.count] = parse_expr(p);
            if (p->had_error) { ast_node_free(n); return NULL; }
            expect(p, TOKEN_COLON, "expected ':' in dict literal");
            n->dict_lit.vals[n->dict_lit.count] = parse_expr(p);
            n->dict_lit.count++;
            skip_newlines(p);
        }
        expect(p, TOKEN_RBRACE, "expected '}' after dict literal");
        return n;
    } else {
        /* set literal */
        ASTNode *n = ast_node_new(NODE_SET_LIT, line);
        int cap = 8;
        n->list_lit.items = malloc(cap * sizeof(ASTNode *));
        n->list_lit.items[0] = first;
        n->list_lit.count = 1;
        skip_newlines(p);
        while (match(p, TOKEN_COMMA)) {
            skip_newlines(p);
            if (check(p, TOKEN_RBRACE)) break;
            if (n->list_lit.count >= cap) { cap *= 2; n->list_lit.items = realloc(n->list_lit.items, cap * sizeof(ASTNode *)); }
            n->list_lit.items[n->list_lit.count++] = parse_expr(p);
            if (p->had_error) { ast_node_free(n); return NULL; }
            skip_newlines(p);
        }
        expect(p, TOKEN_RBRACE, "expected '}' after set literal");
        return n;
    }
}

/* parse () or (expr) or (a, b, c) */
static ASTNode *parse_paren_or_tuple(Parser *p, int line) {
    /* '(' already consumed */
    skip_newlines(p);
    if (check(p, TOKEN_RPAREN)) {
        advance(p);
        /* empty tuple */
        ASTNode *n = ast_node_new(NODE_TUPLE_LIT, line);
        n->list_lit.items = malloc(1 * sizeof(ASTNode *));
        n->list_lit.count = 0;
        return n;
    }

    ASTNode *first = parse_expr(p);
    if (p->had_error) { ast_node_free(first); return NULL; }
    skip_newlines(p);

    if (check(p, TOKEN_COMMA)) {
        /* tuple */
        ASTNode *n = ast_node_new(NODE_TUPLE_LIT, line);
        int cap = 8;
        n->list_lit.items = malloc(cap * sizeof(ASTNode *));
        n->list_lit.items[0] = first;
        n->list_lit.count = 1;
        while (match(p, TOKEN_COMMA)) {
            skip_newlines(p);
            if (check(p, TOKEN_RPAREN)) break;
            if (n->list_lit.count >= cap) { cap *= 2; n->list_lit.items = realloc(n->list_lit.items, cap * sizeof(ASTNode *)); }
            n->list_lit.items[n->list_lit.count++] = parse_expr(p);
            if (p->had_error) { ast_node_free(n); return NULL; }
            skip_newlines(p);
        }
        expect(p, TOKEN_RPAREN, "expected ')' after tuple");
        return n;
    } else {
        /* parenthesized expression */
        expect(p, TOKEN_RPAREN, "expected ')'");
        return first;
    }
}

static ASTNode *parse_primary(Parser *p) {
    if (p->had_error) return ast_node_new(NODE_NULL_LIT, 0);
    int line = p->current->line;

    /* null */
    if (match(p, TOKEN_NULL)) {
        return ast_node_new(NODE_NULL_LIT, line);
    }

    /* true / false / unknown */
    if (check(p, TOKEN_TRUE) || check(p, TOKEN_FALSE) || check(p, TOKEN_UNKNOWN)) {
        int bval = check(p, TOKEN_TRUE) ? 1 : (check(p, TOKEN_FALSE) ? 0 : -1);
        advance(p);
        ASTNode *n = ast_node_new(NODE_BOOL_LIT, line);
        n->bool_lit.value = bval;
        return n;
    }

    /* integer */
    if (check(p, TOKEN_INT_LIT)) {
        char *s = p->current->value;
        long long val;
        if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0)
            val = strtoll(s, NULL, 16);
        else if (strncmp(s, "0b", 2) == 0 || strncmp(s, "0B", 2) == 0)
            val = strtoll(s + 2, NULL, 2);
        else
            val = strtoll(s, NULL, 10);
        advance(p);
        ASTNode *n = ast_node_new(NODE_INT_LIT, line);
        n->int_lit.value = val;
        return n;
    }

    /* float */
    if (check(p, TOKEN_FLOAT_LIT)) {
        double val = strtod(p->current->value, NULL);
        advance(p);
        ASTNode *n = ast_node_new(NODE_FLOAT_LIT, line);
        n->float_lit.value = val;
        return n;
    }

    /* complex */
    if (check(p, TOKEN_COMPLEX_LIT)) {
        double imag = strtod(p->current->value, NULL);
        advance(p);
        ASTNode *n = ast_node_new(NODE_COMPLEX_LIT, line);
        n->complex_lit.real = 0.0;
        n->complex_lit.imag = imag;
        return n;
    }

    /* string */
    if (check(p, TOKEN_STRING_LIT)) {
        char *s = strdup(p->current->value);
        advance(p);
        ASTNode *n = ast_node_new(NODE_STRING_LIT, line);
        n->string_lit.value = s;
        return n;
    }

    /* f-string */
    if (check(p, TOKEN_FSTRING_LIT)) {
        const char *raw = p->current->value;
        ASTNode *n = ast_node_new(NODE_FSTRING_LIT, line);
        n->string_lit.value = strdup(raw);
        advance(p);
        return n;
    }

    /* array literal: [a, b, c] */
    if (check(p, TOKEN_LBRACKET)) {
        advance(p);
        return parse_array_literal(p, line, false);
    }

    /* arr keyword: arr[a, b, c] */
    if (check(p, TOKEN_ARR_KW) && check_peek(p, TOKEN_LBRACKET)) {
        advance(p); advance(p);
        return parse_array_literal(p, line, true);
    }

    /* brace literal: {key:val} or {val, val} */
    if (check(p, TOKEN_LBRACE)) {
        advance(p);
        return parse_brace_literal(p, line);
    }

    /* parenthesized expr or tuple */
    if (check(p, TOKEN_LPAREN)) {
        advance(p);
        return parse_paren_or_tuple(p, line);
    }

    /* type-cast / builtin calls: int(x), float(x), str(x), bool(x), len(x), type(x) */
    if (check(p, TOKEN_INT_KW)   || check(p, TOKEN_FLOAT_KW) ||
        check(p, TOKEN_STR_KW)   || check(p, TOKEN_BOOL_KW)  ||
        check(p, TOKEN_TYPE_KW)  || check(p, TOKEN_LEN)       ||
        check(p, TOKEN_DICT_KW)  || check(p, TOKEN_SET_KW)) {
        if (check_peek(p, TOKEN_LPAREN)) {
            ASTNode *ident = ast_node_new(NODE_IDENT, line);
            ident->ident.name = strdup(p->current->value);
            advance(p);  /* consume keyword */
            advance(p);  /* consume '(' */
            int argc = 0;
            ASTNode **args = parse_arg_list(p, &argc);
            expect(p, TOKEN_RPAREN, "expected ')' after argument list");
            ASTNode *n = ast_node_new(NODE_FUNC_CALL, line);
            n->func_call.callee    = ident;
            n->func_call.args      = args;
            n->func_call.arg_count = argc;
            return n;
        }
    }

    /* output / input keyword used as a statement expression */
    if (check(p, TOKEN_OUTPUT) || check(p, TOKEN_INPUT)) {
        if (check_peek(p, TOKEN_LPAREN)) {
            ASTNode *ident = ast_node_new(NODE_IDENT, line);
            ident->ident.name = strdup(p->current->value);
            advance(p);
            advance(p); /* consume '(' */
            int argc = 0;
            ASTNode **args = parse_arg_list(p, &argc);
            expect(p, TOKEN_RPAREN, "expected ')' after argument list");
            ASTNode *n = ast_node_new(NODE_FUNC_CALL, line);
            n->func_call.callee    = ident;
            n->func_call.args      = args;
            n->func_call.arg_count = argc;
            return n;
        }
        /* output expr (no parens) */
        if (check(p, TOKEN_OUTPUT)) {
            ASTNode *ident = ast_node_new(NODE_IDENT, line);
            ident->ident.name = strdup("output");
            advance(p);
            int cap = 4, argc = 0;
            ASTNode **args = malloc(cap * sizeof(ASTNode *));
            /* parse comma-separated exprs until newline/semicolon */
            while (!check(p, TOKEN_NEWLINE) && !check(p, TOKEN_SEMICOLON) &&
                   !check(p, TOKEN_EOF)      && !check(p, TOKEN_RBRACE)) {
                if (argc >= cap) { cap *= 2; args = realloc(args, cap * sizeof(ASTNode *)); }
                args[argc++] = parse_expr(p);
                if (p->had_error) { ast_node_free(ident); free(args); return ast_node_new(NODE_NULL_LIT, line); }
                if (!match(p, TOKEN_COMMA)) break;
            }
            ASTNode *n = ast_node_new(NODE_FUNC_CALL, line);
            n->func_call.callee    = ident;
            n->func_call.args      = args;
            n->func_call.arg_count = argc;
            return n;
        }
        /* input with no parens — just treat as identifier */
        ASTNode *n = ast_node_new(NODE_IDENT, line);
        n->ident.name = strdup(p->current->value);
        advance(p);
        return n;
    }

    /* identifier */
    if (check(p, TOKEN_IDENT) || is_name_token(p->current->type)) {
        ASTNode *n = ast_node_new(NODE_IDENT, line);
        n->ident.name = strdup(p->current->value);
        advance(p);
        return n;
    }

    error_at(p, "unexpected token in expression");
    return ast_node_new(NODE_NULL_LIT, line);
}

/* ---- postfix: calls, indexing, slicing, member access, safe access ---- */

static ASTNode *parse_postfix(Parser *p) {
    ASTNode *node = parse_primary(p);
    if (p->had_error) return node;

    while (true) {
        int line = p->current->line;

        /* function call */
        if (check(p, TOKEN_LPAREN)) {
            advance(p);
            int argc = 0;
            ASTNode **args = parse_arg_list(p, &argc);
            if (p->had_error) { ast_node_free(node); free(args); return ast_node_new(NODE_NULL_LIT, line); }
            expect(p, TOKEN_RPAREN, "expected ')' after arguments");
            ASTNode *call = ast_node_new(NODE_FUNC_CALL, line);
            call->func_call.callee    = node;
            call->func_call.args      = args;
            call->func_call.arg_count = argc;
            node = call;
            continue;
        }

        /* indexing / slicing */
        if (check(p, TOKEN_LBRACKET)) {
            advance(p);
            skip_newlines(p);

            ASTNode *start = NULL, *stop = NULL, *step = NULL;
            bool is_slice = false;

            if (!check(p, TOKEN_COLON)) {
                start = parse_expr(p);
                if (p->had_error) { ast_node_free(node); ast_node_free(start); return ast_node_new(NODE_NULL_LIT, line); }
            }

            if (check(p, TOKEN_COLON)) {
                is_slice = true;
                advance(p);
                if (!check(p, TOKEN_COLON) && !check(p, TOKEN_RBRACKET))
                    stop = parse_expr(p);
                if (check(p, TOKEN_COLON)) {
                    advance(p);
                    if (!check(p, TOKEN_RBRACKET))
                        step = parse_expr(p);
                }
            }

            expect(p, TOKEN_RBRACKET, "expected ']'");

            if (is_slice) {
                ASTNode *n = ast_node_new(NODE_SLICE, line);
                n->slice_expr.obj   = node;
                n->slice_expr.start = start;
                n->slice_expr.stop  = stop;
                n->slice_expr.step  = step;
                node = n;
            } else {
                ASTNode *n = ast_node_new(NODE_INDEX, line);
                n->index_expr.obj   = node;
                n->index_expr.index = start;
                node = n;
            }
            continue;
        }

        /* member access / method call */
        if (check(p, TOKEN_DOT)) {
            advance(p);
            if (!check(p, TOKEN_IDENT) && !check(p, TOKEN_LEN) &&
                !check(p, TOKEN_STR_KW) && !check(p, TOKEN_INT_KW) &&
                !check(p, TOKEN_FLOAT_KW) && !check(p, TOKEN_DICT_KW)) {
                error_at(p, "expected method/property name after '.'");
                ast_node_free(node);
                return ast_node_new(NODE_NULL_LIT, line);
            }
            char *name = strdup(p->current->value);
            advance(p);

            if (check(p, TOKEN_LPAREN)) {
                advance(p);
                int argc = 0;
                ASTNode **args = parse_arg_list(p, &argc);
                expect(p, TOKEN_RPAREN, "expected ')' after method arguments");
                ASTNode *n = ast_node_new(NODE_METHOD_CALL, line);
                n->method_call.obj       = node;
                n->method_call.method    = name;
                n->method_call.args      = args;
                n->method_call.arg_count = argc;
                node = n;
            } else {
                ASTNode *n = ast_node_new(NODE_MEMBER, line);
                n->member.obj  = node;
                n->member.name = name;
                node = n;
            }
            continue;
        }

        /* safe member access: obj?.member or obj?.method() */
        if (check(p, TOKEN_SAFE_DOT)) {
            advance(p);
            if (!check(p, TOKEN_IDENT) && !is_name_token(p->current->type)) {
                error_at(p, "expected member name after '?.'");
                ast_node_free(node);
                return ast_node_new(NODE_NULL_LIT, line);
            }
            char *name = strdup(p->current->value);
            advance(p);

            ASTNode *n = ast_node_new(NODE_SAFE_ACCESS, line);
            n->safe_access.obj      = node;
            n->safe_access.name     = name;
            n->safe_access.args     = NULL;
            n->safe_access.arg_count = 0;
            n->safe_access.is_call  = false;

            if (check(p, TOKEN_LPAREN)) {
                advance(p);
                int argc = 0;
                ASTNode **args = parse_arg_list(p, &argc);
                expect(p, TOKEN_RPAREN, "expected ')' after safe method call");
                n->safe_access.args      = args;
                n->safe_access.arg_count = argc;
                n->safe_access.is_call   = true;
            }

            node = n;
            continue;
        }

        break;
    }
    return node;
}

/* ---- unary ---- */

static ASTNode *parse_unary(Parser *p) {
    int line = p->current->line;
    if (check(p, TOKEN_MINUS) || check(p, TOKEN_BANG) || check(p, TOKEN_TILDE) || check(p, TOKEN_NOT)) {
        char op[8];
        strncpy(op, p->current->value ? p->current->value : token_type_name(p->current->type), 7);
        op[7] = '\0';
        advance(p);
        ASTNode *n = ast_node_new(NODE_UNOP, line);
        strncpy(n->unop.op, op, 3);
        n->unop.operand = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

/* ---- power ---- */

static ASTNode *parse_power(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (p->had_error) return left;
    if (check(p, TOKEN_STARSTAR)) {
        int line = p->current->line;
        advance(p);
        ASTNode *right = parse_unary(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, "**", 3);
        n->binop.left  = left;
        n->binop.right = right;
        return n;
    }
    return left;
}

/* ---- multiplicative ---- */

static ASTNode *parse_mul(Parser *p) {
    ASTNode *left = parse_power(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_STAR) || check(p, TOKEN_SLASH) || check(p, TOKEN_PERCENT)) {
        int line = p->current->line;
        char op[4]; strncpy(op, p->current->value, 3); op[3] = '\0';
        advance(p);
        ASTNode *right = parse_power(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, op, 3);
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- additive ---- */

static ASTNode *parse_add(Parser *p) {
    ASTNode *left = parse_mul(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_PLUS) || check(p, TOKEN_MINUS)) {
        int line = p->current->line;
        char op[4]; strncpy(op, p->current->value, 3); op[3] = '\0';
        advance(p);
        ASTNode *right = parse_mul(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, op, 3);
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- membership: in / not in ---- */

static ASTNode *parse_membership(Parser *p) {
    ASTNode *left = parse_add(p);
    if (p->had_error) return left;
    int line = p->current->line;

    if (check(p, TOKEN_IN)) {
        advance(p);
        ASTNode *right = parse_add(p);
        ASTNode *n = ast_node_new(NODE_IN_EXPR, line);
        n->in_expr.item      = left;
        n->in_expr.container = right;
        return n;
    }
    if (check(p, TOKEN_NOT) && check_peek(p, TOKEN_IN)) {
        advance(p); advance(p);
        ASTNode *right = parse_add(p);
        ASTNode *n = ast_node_new(NODE_NOT_IN_EXPR, line);
        n->in_expr.item      = left;
        n->in_expr.container = right;
        return n;
    }
    return left;
}

/* ---- comparison ---- */

static ASTNode *parse_compare(Parser *p) {
    ASTNode *left = parse_membership(p);
    if (p->had_error) return left;

    /* 'is' / 'is not' type-check operator */
    if (check(p, TOKEN_IS)) {
        int line = p->current->line;
        advance(p);
        bool negate = false;
        if (check(p, TOKEN_NOT)) { advance(p); negate = true; }
        /* type name can be a keyword like int, str, float, bool, or identifier */
        if (!is_name_token(p->current->type) && !check(p, TOKEN_NULL) &&
            !check(p, TOKEN_UNKNOWN) && !check(p, TOKEN_INT_KW) &&
            !check(p, TOKEN_FLOAT_KW) && !check(p, TOKEN_STR_KW) &&
            !check(p, TOKEN_BOOL_KW) && !check(p, TOKEN_DICT_KW) &&
            !check(p, TOKEN_SET_KW)  && !check(p, TOKEN_ARR_KW)) {
            error_at(p, "expected type name after 'is'");
            ast_node_free(left);
            return ast_node_new(NODE_NULL_LIT, line);
        }
        char *type_name = strdup(p->current->value);
        advance(p);
        ASTNode *n = ast_node_new(NODE_IS_EXPR, line);
        n->is_expr.obj       = left;
        n->is_expr.type_name = type_name;
        n->is_expr.negate    = negate;
        return n;
    }

    while (check(p, TOKEN_LT) || check(p, TOKEN_GT) || check(p, TOKEN_LE) || check(p, TOKEN_GE)) {
        int line = p->current->line;
        char op[4]; strncpy(op, p->current->value, 3); op[3] = '\0';
        advance(p);
        ASTNode *right = parse_membership(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, op, 3);
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- equality ---- */

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_compare(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_EQEQ) || check(p, TOKEN_NEQ)) {
        int line = p->current->line;
        char op[4]; strncpy(op, p->current->value, 3); op[3] = '\0';
        advance(p);
        ASTNode *right = parse_compare(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, op, 3);
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- bitwise and ---- */

static ASTNode *parse_bitand(Parser *p) {
    ASTNode *left = parse_equality(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_AMP)) {
        int line = p->current->line; advance(p);
        ASTNode *right = parse_equality(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, "&", 3);
        n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- bitwise xor ---- */

static ASTNode *parse_bitxor(Parser *p) {
    ASTNode *left = parse_bitand(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_CARET)) {
        int line = p->current->line; advance(p);
        ASTNode *right = parse_bitand(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, "^", 3);
        n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- bitwise or ---- */

static ASTNode *parse_bitor(Parser *p) {
    ASTNode *left = parse_bitxor(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_PIPE)) {
        int line = p->current->line; advance(p);
        ASTNode *right = parse_bitxor(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, "|", 3);
        n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- logical and ---- */

static ASTNode *parse_logical_and(Parser *p) {
    ASTNode *left = parse_bitor(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_AMPAMP) || check(p, TOKEN_AND)) {
        int line = p->current->line; advance(p);
        ASTNode *right = parse_bitor(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, "&&", 3);
        n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- logical or ---- */

static ASTNode *parse_logical_or(Parser *p) {
    ASTNode *left = parse_logical_and(p);
    if (p->had_error) return left;
    while (check(p, TOKEN_PIPEPIPE) || check(p, TOKEN_OR)) {
        int line = p->current->line; advance(p);
        ASTNode *right = parse_logical_and(p);
        ASTNode *n = ast_node_new(NODE_BINOP, line);
        strncpy(n->binop.op, "||", 3);
        n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* ---- range: expr .. expr (step expr)? ---- */

static ASTNode *parse_range(Parser *p) {
    ASTNode *left = parse_logical_or(p);
    if (p->had_error) return left;

    if (check(p, TOKEN_DOTDOT)) {
        int line = p->current->line;
        advance(p);
        ASTNode *right = parse_logical_or(p);
        ASTNode *step  = NULL;
        if (check(p, TOKEN_STEP)) {
            advance(p);
            step = parse_logical_or(p);
        }
        ASTNode *n = ast_node_new(NODE_RANGE, line);
        n->range_lit.start     = left;
        n->range_lit.end       = right;
        n->range_lit.step      = step;
        n->range_lit.inclusive = true; /* 1..10 includes 10 */
        return n;
    }

    return left;
}

/* ---- null coalescing: expr ?? expr ---- */

static ASTNode *parse_nullcoal(Parser *p) {
    ASTNode *left = parse_range(p);
    if (p->had_error) return left;

    while (check(p, TOKEN_NULLCOAL)) {
        int line = p->current->line;
        advance(p);
        ASTNode *right = parse_range(p);
        ASTNode *n = ast_node_new(NODE_NULLCOAL, line);
        n->nullcoal.left  = left;
        n->nullcoal.right = right;
        left = n;
    }
    return left;
}

/* ---- assignment (rightmost prec) ---- */

static ASTNode *parse_assign(Parser *p) {
    ASTNode *left = parse_nullcoal(p);
    if (p->had_error) return left;
    int line = p->current->line;

    /* walrus := (declare + assign in current scope) */
    if (check(p, TOKEN_WALRUS)) {
        if (left->type != NODE_IDENT) {
            error_at(p, ":= requires a simple identifier on the left");
            ast_node_free(left);
            return ast_node_new(NODE_NULL_LIT, line);
        }
        char *name = strdup(left->ident.name);
        ast_node_free(left);
        advance(p); /* consume := */
        ASTNode *right = parse_assign(p);
        ASTNode *n = ast_node_new(NODE_VAR_DECL, line);
        n->var_decl.name     = name;
        n->var_decl.init     = right;
        n->var_decl.is_const = false;
        return n;
    }

    /* compound assignment */
    if (check(p, TOKEN_PLUS_EQ)  || check(p, TOKEN_MINUS_EQ) ||
        check(p, TOKEN_STAR_EQ)  || check(p, TOKEN_SLASH_EQ) ||
        check(p, TOKEN_PERCENT_EQ)) {
        char op[4]; strncpy(op, p->current->value, 3); op[3] = '\0';
        advance(p);
        ASTNode *right = parse_assign(p);
        ASTNode *n = ast_node_new(NODE_COMPOUND_ASSIGN, line);
        strncpy(n->assign.op, op, 3);
        n->assign.target = left;
        n->assign.value  = right;
        return n;
    }

    /* simple assignment */
    if (check(p, TOKEN_EQ)) {
        advance(p);
        ASTNode *right = parse_assign(p);
        ASTNode *n = ast_node_new(NODE_ASSIGN, line);
        n->assign.target = left;
        n->assign.value  = right;
        return n;
    }

    return left;
}

static ASTNode *parse_expr(Parser *p) {
    return parse_assign(p);
}

/* ================================================================== statements */

static ASTNode *parse_var_decl(Parser *p) {
    int line = p->current->line;
    bool is_const = check(p, TOKEN_CONST);
    advance(p); /* consume let/const */

    if (!is_name_token(p->current->type)) {
        error_at(p, "expected variable name after let/const");
        return ast_node_new(NODE_NULL_LIT, line);
    }

    char *name = strdup(p->current->value);
    advance(p);

    ASTNode *init = NULL;
    if (match(p, TOKEN_EQ)) {
        init = parse_expr(p);
        if (p->had_error) { free(name); return ast_node_new(NODE_NULL_LIT, line); }

        /* trailing comma creates a tuple: let t = 1, 2, 3 */
        if (check(p, TOKEN_COMMA)) {
            int cap = 8;
            ASTNode *tuple = ast_node_new(NODE_TUPLE_LIT, line);
            tuple->list_lit.items = malloc(cap * sizeof(ASTNode *));
            tuple->list_lit.items[0] = init;
            tuple->list_lit.count = 1;
            while (match(p, TOKEN_COMMA)) {
                if (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                    check(p, TOKEN_EOF)     || check(p, TOKEN_RBRACE)) break;
                if (tuple->list_lit.count >= cap) {
                    cap *= 2;
                    tuple->list_lit.items = realloc(tuple->list_lit.items, cap * sizeof(ASTNode *));
                }
                tuple->list_lit.items[tuple->list_lit.count++] = parse_expr(p);
                if (p->had_error) { free(name); ast_node_free(tuple); return ast_node_new(NODE_NULL_LIT, line); }
            }
            init = tuple;
        }
    } else if (is_const) {
        error_at(p, "const declaration must have an initializer");
        free(name);
        return ast_node_new(NODE_NULL_LIT, line);
    }

    ASTNode *n = ast_node_new(NODE_VAR_DECL, line);
    n->var_decl.name     = name;
    n->var_decl.init     = init;
    n->var_decl.is_const = is_const;
    return n;
}

static ASTNode *parse_if(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_IF, "expected 'if'");

    int cap = 4;
    ASTNode **conds  = malloc(cap * sizeof(ASTNode *));
    ASTNode **bodies = malloc(cap * sizeof(ASTNode *));
    int branch_count = 0;

    skip_newlines(p);
    conds[branch_count]  = parse_expr(p);
    if (p->had_error) { free(conds); free(bodies); return ast_node_new(NODE_NULL_LIT, line); }
    skip_newlines(p);
    bodies[branch_count] = parse_block(p);
    branch_count++;

    while (check(p, TOKEN_ELIF)) {
        advance(p);
        if (branch_count >= cap) { cap *= 2; conds = realloc(conds, cap * sizeof(ASTNode *)); bodies = realloc(bodies, cap * sizeof(ASTNode *)); }
        skip_newlines(p);
        conds[branch_count]  = parse_expr(p);
        skip_newlines(p);
        bodies[branch_count] = parse_block(p);
        branch_count++;
    }

    ASTNode *else_body = NULL;
    if (check(p, TOKEN_ELSE)) {
        advance(p);
        skip_newlines(p);
        else_body = parse_block(p);
    }

    ASTNode *n = ast_node_new(NODE_IF, line);
    n->if_stmt.conds        = conds;
    n->if_stmt.bodies       = bodies;
    n->if_stmt.branch_count = branch_count;
    n->if_stmt.else_body    = else_body;
    return n;
}

static ASTNode *parse_while(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_WHILE, "expected 'while'");
    skip_newlines(p);
    ASTNode *cond = parse_expr(p);
    skip_newlines(p);
    ASTNode *body = parse_block(p);
    ASTNode *n = ast_node_new(NODE_WHILE, line);
    n->while_stmt.cond = cond;
    n->while_stmt.body = body;
    return n;
}

static ASTNode *parse_for(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_FOR, "expected 'for'");
    skip_newlines(p);
    if (!check(p, TOKEN_IDENT)) { error_at(p, "expected variable name in for loop"); return ast_node_new(NODE_NULL_LIT, line); }
    char *var = strdup(p->current->value);
    advance(p);
    expect(p, TOKEN_IN, "expected 'in' in for loop");
    skip_newlines(p);
    ASTNode *iter = parse_expr(p);
    skip_newlines(p);
    ASTNode *body = parse_block(p);
    ASTNode *n = ast_node_new(NODE_FOR_IN, line);
    n->for_in.var  = var;
    n->for_in.iter = iter;
    n->for_in.body = body;
    return n;
}

/* repeat N { body }
   repeat while cond { body }
   repeat until cond { body } */
static ASTNode *parse_repeat(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_REPEAT, "expected 'repeat'");
    skip_newlines(p);

    ASTNode *n = ast_node_new(NODE_REPEAT, line);

    if (check(p, TOKEN_WHILE)) {
        /* repeat while cond { } */
        advance(p);
        skip_newlines(p);
        n->repeat_stmt.count = NULL;
        n->repeat_stmt.cond  = parse_expr(p);
        n->repeat_stmt.until = false;
    } else if (check(p, TOKEN_UNTIL)) {
        /* repeat until cond { } */
        advance(p);
        skip_newlines(p);
        n->repeat_stmt.count = NULL;
        n->repeat_stmt.cond  = parse_expr(p);
        n->repeat_stmt.until = true;
    } else {
        /* repeat N { } */
        n->repeat_stmt.count = parse_expr(p);
        n->repeat_stmt.cond  = NULL;
        n->repeat_stmt.until = false;
    }

    skip_newlines(p);
    n->repeat_stmt.body = parse_block(p);
    return n;
}

/* try { ... } catch varname { ... } */
static ASTNode *parse_try_catch(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_TRY, "expected 'try'");
    skip_newlines(p);
    ASTNode *try_body = parse_block(p);

    char    *catch_var  = NULL;
    ASTNode *catch_body = NULL;
    ASTNode *finally_body = NULL;

    if (check(p, TOKEN_CATCH)) {
        advance(p);
        skip_newlines(p);
        /* optional variable name to bind the error */
        if (check(p, TOKEN_IDENT)) {
            catch_var = strdup(p->current->value);
            advance(p);
        }
        skip_newlines(p);
        catch_body = parse_block(p);
    }

    /* optional finally block */
    if (check(p, TOKEN_IDENT) && strcmp(p->current->value, "finally") == 0) {
        advance(p);
        skip_newlines(p);
        finally_body = parse_block(p);
    }

    ASTNode *n = ast_node_new(NODE_TRY_CATCH, line);
    n->try_catch.try_body     = try_body;
    n->try_catch.catch_var    = catch_var;
    n->try_catch.catch_body   = catch_body;
    n->try_catch.finally_body = finally_body;
    return n;
}

/* throw expr */
static ASTNode *parse_throw(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_THROW, "expected 'throw'");
    ASTNode *val = parse_expr(p);
    ASTNode *n = ast_node_new(NODE_THROW, line);
    n->throw_stmt.value = val;
    return n;
}

/* match val {
     when pattern { body }
     when pattern { body }
     else { body }
   } */
static ASTNode *parse_match(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_MATCH, "expected 'match'");
    skip_newlines(p);
    ASTNode *value = parse_expr(p);
    skip_newlines(p);
    expect(p, TOKEN_LBRACE, "expected '{' after match expression");
    skip_newlines(p);

    int cap = 8, count = 0;
    ASTNode **patterns = malloc(cap * sizeof(ASTNode *));
    ASTNode **bodies   = malloc(cap * sizeof(ASTNode *));
    ASTNode  *else_body = NULL;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        skip_newlines(p);
        if (check(p, TOKEN_RBRACE)) break;

        if (check(p, TOKEN_ELSE)) {
            advance(p);
            skip_newlines(p);
            else_body = parse_block(p);
            skip_newlines(p);
            continue;
        }

        if (!check(p, TOKEN_WHEN)) {
            error_at(p, "expected 'when' or 'else' in match block");
            ast_node_free(value);
            free(patterns); free(bodies);
            return ast_node_new(NODE_NULL_LIT, line);
        }
        advance(p); /* consume 'when' */
        skip_newlines(p);

        /* pattern can be a range or single value */
        ASTNode *pat = parse_expr(p);
        skip_newlines(p);

        if (count >= cap) {
            cap *= 2;
            patterns = realloc(patterns, cap * sizeof(ASTNode *));
            bodies   = realloc(bodies,   cap * sizeof(ASTNode *));
        }
        patterns[count] = pat;
        bodies[count]   = parse_block(p);
        count++;
        skip_newlines(p);
        consume_stmt_end(p);
    }
    expect(p, TOKEN_RBRACE, "expected '}' to close match block");

    ASTNode *n = ast_node_new(NODE_MATCH, line);
    n->match_stmt.value     = value;
    n->match_stmt.patterns  = patterns;
    n->match_stmt.bodies    = bodies;
    n->match_stmt.count     = count;
    n->match_stmt.else_body = else_body;
    return n;
}

static ASTNode *parse_func(Parser *p) {
    int line = p->current->line;
    expect(p, TOKEN_FUNC, "expected 'func'");
    if (!check(p, TOKEN_IDENT)) { error_at(p, "expected function name"); return ast_node_new(NODE_NULL_LIT, line); }
    char *name = strdup(p->current->value);
    advance(p);
    expect(p, TOKEN_LPAREN, "expected '(' after function name");

    int cap = 8, param_count = 0;
    Param *params = malloc(cap * sizeof(Param));

    skip_newlines(p);
    while (!check(p, TOKEN_RPAREN) && !check(p, TOKEN_EOF)) {
        if (param_count >= cap) { cap *= 2; params = realloc(params, cap * sizeof(Param)); }
        params[param_count].default_val = NULL;

        /* optional type hint: str name, int age */
        char *type_hint = NULL;
        if ((check(p, TOKEN_STR_KW) || check(p, TOKEN_INT_KW) ||
             check(p, TOKEN_FLOAT_KW) || check(p, TOKEN_BOOL_KW)) &&
            check_peek(p, TOKEN_IDENT)) {
            type_hint = strdup(p->current->value);
            advance(p);
        }
        if (!check(p, TOKEN_IDENT)) {
            error_at(p, "expected parameter name");
            free(name); free(params); return ast_node_new(NODE_NULL_LIT, line);
        }
        params[param_count].name      = strdup(p->current->value);
        params[param_count].type_hint = type_hint;
        param_count++;
        advance(p);

        /* optional default value: param = expr */
        if (check(p, TOKEN_EQ)) {
            advance(p);
            params[param_count - 1].default_val = parse_expr(p);
            if (p->had_error) { free(name); free(params); return ast_node_new(NODE_NULL_LIT, line); }
        }

        skip_newlines(p);
        if (!match(p, TOKEN_COMMA)) break;
        skip_newlines(p);
    }
    expect(p, TOKEN_RPAREN, "expected ')' after parameters");

    /* optional return type hint -> ... */
    if (check(p, TOKEN_ARROW)) { advance(p); advance(p); }

    skip_newlines(p);
    ASTNode *body = parse_block(p);

    ASTNode *n = ast_node_new(NODE_FUNC_DECL, line);
    n->func_decl.name        = name;
    n->func_decl.params      = params;
    n->func_decl.param_count = param_count;
    n->func_decl.body        = body;
    return n;
}

static ASTNode *parse_block(Parser *p) {
    int line = p->current->line;
    skip_newlines(p);
    expect(p, TOKEN_LBRACE, "expected '{'");
    skip_newlines(p);

    int cap = 8;
    ASTNode *block = ast_node_new(NODE_BLOCK, line);
    block->block.stmts = malloc(cap * sizeof(ASTNode *));
    block->block.count = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        skip_newlines(p);
        if (check(p, TOKEN_RBRACE)) break;
        ASTNode *stmt = parse_stmt(p);
        if (p->had_error) { ast_node_free(block); return ast_node_new(NODE_NULL_LIT, line); }
        if (block->block.count >= cap) { cap *= 2; block->block.stmts = realloc(block->block.stmts, cap * sizeof(ASTNode *)); }
        block->block.stmts[block->block.count++] = stmt;
        consume_stmt_end(p);
    }
    expect(p, TOKEN_RBRACE, "expected '}'");
    return block;
}

static ASTNode *parse_return(Parser *p) {
    int line = p->current->line;
    advance(p);
    ASTNode *val = NULL;
    if (!check(p, TOKEN_SEMICOLON) && !check(p, TOKEN_NEWLINE) &&
        !check(p, TOKEN_RBRACE)    && !check(p, TOKEN_EOF)) {
        val = parse_expr(p);
    }
    ASTNode *n = ast_node_new(NODE_RETURN, line);
    n->ret.value = val;
    return n;
}

/* parse import statement:
   import "path"
   import "path" as alias
   from "path" import symbol */
static ASTNode *parse_import_stmt(Parser *p) {
    int line = p->current->line;

    if (check(p, TOKEN_FROM)) {
        /* from "path" import symbol */
        advance(p);
        if (!check(p, TOKEN_STRING_LIT) && !check(p, TOKEN_IDENT)) {
            error_at(p, "expected path or module name after 'from'");
            return ast_node_new(NODE_NULL_LIT, line);
        }
        char *path = strdup(p->current->value);
        advance(p);
        expect(p, TOKEN_IMPORT, "expected 'import' after from path");
        if (!check(p, TOKEN_IDENT) && !is_name_token(p->current->type)) {
            error_at(p, "expected symbol name after 'import'");
            free(path);
            return ast_node_new(NODE_NULL_LIT, line);
        }
        char *symbol = strdup(p->current->value);
        advance(p);
        char *alias = NULL;
        if (check(p, TOKEN_AS)) {
            advance(p);
            if (!check(p, TOKEN_IDENT)) { error_at(p, "expected alias after 'as'"); free(path); free(symbol); return ast_node_new(NODE_NULL_LIT, line); }
            alias = strdup(p->current->value);
            advance(p);
        }
        ASTNode *n = ast_node_new(NODE_IMPORT, line);
        n->import_stmt.path   = path;
        n->import_stmt.symbol = symbol;
        n->import_stmt.alias  = alias;
        return n;
    }

    /* import "path" [as alias] */
    advance(p); /* consume 'import' */
    if (!check(p, TOKEN_STRING_LIT) && !check(p, TOKEN_IDENT)) {
        error_at(p, "expected string path after 'import'");
        return ast_node_new(NODE_NULL_LIT, line);
    }
    char *path = strdup(p->current->value);
    advance(p);
    char *alias = NULL;
    if (check(p, TOKEN_AS)) {
        advance(p);
        if (!check(p, TOKEN_IDENT)) { error_at(p, "expected alias after 'as'"); free(path); return ast_node_new(NODE_NULL_LIT, line); }
        alias = strdup(p->current->value);
        advance(p);
    }
    ASTNode *n = ast_node_new(NODE_IMPORT, line);
    n->import_stmt.path   = path;
    n->import_stmt.symbol = NULL;
    n->import_stmt.alias  = alias;
    return n;
}

static ASTNode *parse_stmt(Parser *p) {
    skip_newlines(p);
    int line = p->current->line;

    if (check(p, TOKEN_LET) || check(p, TOKEN_CONST))
        return parse_var_decl(p);
    if (check(p, TOKEN_IF))
        return parse_if(p);
    if (check(p, TOKEN_WHILE))
        return parse_while(p);
    if (check(p, TOKEN_FOR))
        return parse_for(p);
    if (check(p, TOKEN_FUNC))
        return parse_func(p);
    if (check(p, TOKEN_RETURN))
        return parse_return(p);
    if (check(p, TOKEN_BREAK)) {
        advance(p);
        return ast_node_new(NODE_BREAK, line);
    }
    if (check(p, TOKEN_CONTINUE)) {
        advance(p);
        return ast_node_new(NODE_CONTINUE, line);
    }
    if (check(p, TOKEN_IMPORT) || check(p, TOKEN_FROM))
        return parse_import_stmt(p);
    if (check(p, TOKEN_REPEAT))
        return parse_repeat(p);
    if (check(p, TOKEN_TRY))
        return parse_try_catch(p);
    if (check(p, TOKEN_THROW))
        return parse_throw(p);
    if (check(p, TOKEN_MATCH))
        return parse_match(p);
    if (check(p, TOKEN_LBRACE))
        return parse_block(p);

    /* expression statement */
    ASTNode *expr = parse_expr(p);
    ASTNode *n    = ast_node_new(NODE_EXPR_STMT, line);
    n->expr_stmt.expr = expr;
    return n;
}

/* ================================================================== top-level */

ASTNode *parser_parse_source(const char *source, char *errbuf, int errlen) {
    Parser *p = parser_new(source);
    ASTNode *prog = parser_parse(p);
    if (p->had_error) {
        if (errbuf && errlen > 0)
            snprintf(errbuf, errlen, "%s", p->error_msg);
        ast_node_free(prog);
        parser_free(p);
        return NULL;
    }
    parser_free(p);
    return prog;
}

ASTNode *parser_parse(Parser *p) {
    ASTNode *program = ast_node_new(NODE_PROGRAM, 1);
    int cap = 16;
    program->block.stmts = malloc(cap * sizeof(ASTNode *));
    program->block.count = 0;

    skip_newlines(p);
    while (!check(p, TOKEN_EOF)) {
        if (p->had_error) break;
        skip_newlines(p);
        if (check(p, TOKEN_EOF)) break;

        ASTNode *stmt = parse_stmt(p);
        if (p->had_error) { ast_node_free(program); return ast_node_new(NODE_NULL_LIT, 0); }

        if (program->block.count >= cap) {
            cap *= 2;
            program->block.stmts = realloc(program->block.stmts, cap * sizeof(ASTNode *));
        }
        program->block.stmts[program->block.count++] = stmt;
        consume_stmt_end(p);
    }
    return program;
}
