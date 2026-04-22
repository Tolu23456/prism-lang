#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "ast.h"

static ASTNode *parse_expr(Parser *p);

static Token *advance(Parser *p) {
    Token *prev = p->current;
    p->current = p->peek;
    p->peek = lexer_next(p->lexer);
    return prev;
}

static bool check(Parser *p, TokenType t) { return p->current->type == t; }
static bool check_peek(Parser *p, TokenType t) __attribute__((unused));
static bool check_peek(Parser *p, TokenType t) { return p->peek->type == t; }

/* Allow "soft keywords" and type keywords to be used as identifiers */
static bool is_name_token(TokenType t) __attribute__((unused));
static bool is_name_token(TokenType t) {
    /* Tokens that are valid as identifiers in expression or parameter context */
    switch (t) {
        case TOKEN_IDENT:
        case TOKEN_ARR_KW:   case TOKEN_SET_KW:  case TOKEN_DICT_KW:
        case TOKEN_BOOL_KW:  case TOKEN_INT_KW:  case TOKEN_FLOAT_KW:
        case TOKEN_STR_KW:   case TOKEN_TYPE_KW: case TOKEN_LEN:
        case TOKEN_OUTPUT:   case TOKEN_INPUT:   case TOKEN_SELF:
        /* contextual keywords that may appear as param names, variable names, or function names */
        case TOKEN_STEP:     case TOKEN_REPEAT:
        case TOKEN_FN:       case TOKEN_CATCH:
        case TOKEN_FROM:
            return true;
        default:
            return false;
    }
}

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { advance(p); return true; }
    return false;
}

static void skip_newlines(Parser *p) {
    while (check(p, TOKEN_NEWLINE)) advance(p);
}

static void error_at(Parser *p, const char *msg) {
    /* Suppress cascaded errors while recovering */
    if (p->panic_mode) return;
    p->panic_mode = 1;
    p->had_error  = 1;

    if (p->error_count < PARSER_MAX_ERRORS) {
        ParseError *e = &p->errors[p->error_count++];
        e->line = p->current->line;
        e->col  = p->current->col;
        const char *got = p->current->value ? p->current->value
                                             : token_type_name(p->current->type);
        snprintf(e->msg, sizeof(e->msg), "%s (got '%s')", msg, got);
        /* compat: mirror first error into flat error_msg */
        if (p->error_count == 1)
            snprintf(p->error_msg, sizeof(p->error_msg),
                     "line %d: %s", e->line, e->msg);
    }
}

/* Skip tokens until a safe recovery point (statement boundary). */
static void synchronize(Parser *p) {
    p->panic_mode = 0;
    while (!check(p, TOKEN_EOF)) {
        /* Past a newline or semicolon → safe to resume */
        if (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON)) {
            advance(p);
            return;
        }
        /* Statement-starting keywords are always safe points */
        switch (p->current->type) {
            case TOKEN_LET:    case TOKEN_CONST:  case TOKEN_FUNC:
            case TOKEN_FN:     case TOKEN_CLASS:  case TOKEN_STRUCT:
            case TOKEN_IF:     case TOKEN_WHILE:  case TOKEN_FOR:
            case TOKEN_RETURN: case TOKEN_BREAK:  case TOKEN_CONTINUE:
            case TOKEN_IMPORT: case TOKEN_FROM:   case TOKEN_LINK:
            case TOKEN_REPEAT: case TOKEN_TRY:    case TOKEN_THROW:
            case TOKEN_MATCH:
                return;
            default: break;
        }
        advance(p);
    }
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
    Parser *p    = calloc(1, sizeof(Parser));
    p->lexer     = lexer_new(source);
    p->source    = source;             /* kept for caret display */
    p->current   = lexer_next(p->lexer);
    p->peek      = lexer_next(p->lexer);
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

/* ---- primary ---- */

static ASTNode **parse_arg_list(Parser *p, int *out_count) __attribute__((unused));
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

static ASTNode *parse_array_literal(Parser *p, int line, bool use_arr_kw) __attribute__((unused));
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
static ASTNode *parse_brace_literal(Parser *p, int line) __attribute__((unused));
static ASTNode *parse_brace_literal(Parser *p, int line) {
    /* opening '{' already consumed */
    skip_newlines(p);

    /* empty braces {} — treat as empty dict (consistent with Python) */
    if (check(p, TOKEN_RBRACE)) {
        advance(p);
        ASTNode *n = ast_node_new(NODE_DICT_LIT, line);
        n->dict_lit.keys  = malloc(sizeof(ASTNode *));
        n->dict_lit.vals  = malloc(sizeof(ASTNode *));
        n->dict_lit.count = 0;
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
static ASTNode *parse_paren_or_tuple(Parser *p, int line) __attribute__((unused));
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

ASTNode *parser_parse_source(const char *source, char *errbuf, int errlen) {
    Parser *p = parser_new(source);
    ASTNode *program = parser_parse(p);
    if (p->had_error) {
        if (errbuf && errlen > 0)
            snprintf(errbuf, (size_t)errlen, "%s", p->error_msg);
        if (program) ast_node_free(program);
        parser_free(p);
        return NULL;
    }
    parser_free(p);
    return program;
}

ASTNode *parser_parse(Parser *p) {
    ASTNode *program = ast_node_new(NODE_PROGRAM, 1);
    int cap = 16;
    program->block.stmts = malloc(cap * sizeof(ASTNode *));
    program->block.count = 0;

    skip_newlines(p);
    while (!check(p, TOKEN_EOF)) {
        skip_newlines(p);
        if (check(p, TOKEN_EOF)) break;

        ASTNode *stmt = parse_stmt(p);

        /* Panic-mode recovery: synchronize to next statement boundary, then keep going */
        if (p->panic_mode) {
            ast_node_free(stmt);
            synchronize(p);
            if (p->error_count >= PARSER_MAX_ERRORS) break;
            continue;
        }

        if (program->block.count >= cap) {
            cap *= 2;
            program->block.stmts = realloc(program->block.stmts, cap * sizeof(ASTNode *));
        }
        program->block.stmts[program->block.count++] = stmt;
        consume_stmt_end(p);
    }
    return program;
}

/* ================================================================== error display */

/* Print a single source line by number (1-based) into buf.  Returns length. */
static int get_source_line(const char *src, int lineno, char *buf, int bufsz) {
    if (!src) { buf[0] = '\0'; return 0; }
    int cur = 1;
    while (*src && cur < lineno) {
        if (*src++ == '\n') cur++;
    }
    /* copy until newline or end */
    int i = 0;
    while (*src && *src != '\n' && i < bufsz - 1)
        buf[i++] = *src++;
    buf[i] = '\0';
    return i;
}

void parser_print_errors(const Parser *p, const char *filename) {
    if (!p || p->error_count == 0) return;
    const char *file = filename ? filename : "<source>";
    for (int i = 0; i < p->error_count; i++) {
        const ParseError *e = &p->errors[i];
        /* header line */
        fprintf(stderr, "\033[1;31merror\033[0m: %s:%d:%d: %s\n",
                file, e->line, e->col, e->msg);
        /* source line if available */
        if (p->source) {
            char linebuf[512];
            get_source_line(p->source, e->line, linebuf, sizeof(linebuf));
            fprintf(stderr, "  %4d | %s\n", e->line, linebuf);
            /* caret underline */
            fprintf(stderr, "       | ");
            int col = e->col > 0 ? e->col : 1;
            for (int c = 1; c < col; c++) fputc(' ', stderr);
            fprintf(stderr, "\033[1;33m^\033[0m\n");
        }
    }
    if (p->error_count >= PARSER_MAX_ERRORS)
        fprintf(stderr, "\033[33mnote\033[0m: too many errors, stopped after %d\n",
                PARSER_MAX_ERRORS);
}
