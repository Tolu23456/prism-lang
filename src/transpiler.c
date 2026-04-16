#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "transpiler.h"
#include "ast.h"

/* ================================================================== context */

typedef struct {
    int  indent;
    bool in_func;         /* inside a user function definition */
    bool has_range_iter;  /* whether range() helper was emitted */
    FILE *out;
    int   tmp_counter;    /* for unique temporary names */
} Ctx;

/* ================================================================== helpers */

static void indent(Ctx *ctx) {
    for (int i = 0; i < ctx->indent; i++) fprintf(ctx->out, "    ");
}

/* Sanitise a Prism identifier so it is a valid C identifier.
 * Prism allows the same identifier set as Python, so we just prefix
 * any C keyword with '_pr_'. */
static const char *cident(const char *name, char *buf, size_t bsz) {
    static const char *kws[] = {
        "auto","break","case","char","const","continue","default","do",
        "double","else","enum","extern","float","for","goto","if","inline",
        "int","long","register","restrict","return","short","signed","sizeof",
        "static","struct","switch","typedef","union","unsigned","void",
        "volatile","while","_Bool","_Complex","_Imaginary",NULL
    };
    for (int i = 0; kws[i]; i++) {
        if (strcmp(name, kws[i]) == 0) {
            snprintf(buf, bsz, "_pr_%s", name);
            return buf;
        }
    }
    return name;
}
#define CNAME(n)  ({ char _b[256]; const char *_r=cident((n),_b,sizeof(_b)); _r; })

/* Escape a string literal for C. */
static void emit_cstring(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:
                if ((unsigned char)*s < 32)
                    fprintf(out, "\\x%02x", (unsigned char)*s);
                else
                    fputc(*s, out);
        }
    }
    fputc('"', out);
}

/* Forward declarations. */
static void emit_stmt(Ctx *ctx, ASTNode *node);
static void emit_expr(Ctx *ctx, ASTNode *node);

/* ================================================================== runtime header */

static void emit_runtime(FILE *out) {
    fputs(
"/* -------- Prism runtime (auto-generated) -------- */\n"
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <string.h>\n"
"#include <math.h>\n"
"#include <stdint.h>\n"
"#include <stdbool.h>\n"
"\n"
"/* Dynamic value type for mixed-type situations. */\n"
"typedef enum { PV_INT=0, PV_FLOAT, PV_BOOL, PV_STRING, PV_NULL } PVType;\n"
"typedef struct { PVType t; union { long long i; double f; int b; char *s; } v; } PV;\n"
"\n"
"static PV pv_int(long long v)   { PV r; r.t=PV_INT;   r.v.i=v; return r; }\n"
"static PV pv_float(double v)    { PV r; r.t=PV_FLOAT; r.v.f=v; return r; }\n"
"static PV pv_bool(int v)        { PV r; r.t=PV_BOOL;  r.v.b=v; return r; }\n"
"static PV pv_null(void)         { PV r; r.t=PV_NULL;  r.v.i=0; return r; }\n"
"static PV pv_string(const char *s) { PV r; r.t=PV_STRING; r.v.s=(char*)s; return r; }\n"
"\n"
"static void pv_print(PV v) {\n"
"    switch(v.t) {\n"
"        case PV_INT:    printf(\"%lld\", v.v.i); break;\n"
"        case PV_FLOAT:  printf(\"%g\",   v.v.f); break;\n"
"        case PV_BOOL:   printf(\"%s\", v.v.b==1?\"true\":v.v.b==0?\"false\":\"unknown\"); break;\n"
"        case PV_STRING: printf(\"%s\",   v.v.s); break;\n"
"        case PV_NULL:   printf(\"null\"); break;\n"
"    }\n"
"}\n"
"\n"
"static int pv_truthy(PV v) {\n"
"    switch(v.t) {\n"
"        case PV_INT:    return v.v.i != 0;\n"
"        case PV_FLOAT:  return v.v.f != 0.0;\n"
"        case PV_BOOL:   return v.v.b == 1;\n"
"        case PV_STRING: return v.v.s && v.v.s[0];\n"
"        default: return 0;\n"
"    }\n"
"}\n"
"\n"
"static PV pv_add(PV a, PV b) {\n"
"    if(a.t==PV_INT   && b.t==PV_INT)   return pv_int(a.v.i + b.v.i);\n"
"    if(a.t==PV_FLOAT || b.t==PV_FLOAT) {\n"
"        double fa = a.t==PV_FLOAT?a.v.f:(double)a.v.i;\n"
"        double fb = b.t==PV_FLOAT?b.v.f:(double)b.v.i;\n"
"        return pv_float(fa+fb);\n"
"    }\n"
"    if(a.t==PV_STRING && b.t==PV_STRING) {\n"
"        size_t la=strlen(a.v.s), lb=strlen(b.v.s);\n"
"        char *r=(char*)malloc(la+lb+1); strcpy(r,a.v.s); strcpy(r+la,b.v.s);\n"
"        return pv_string(r);\n"
"    }\n"
"    fprintf(stderr,\"runtime error: invalid operands for +\\n\"); exit(1);\n"
"}\n"
"static PV pv_sub(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT)return pv_int(a.v.i-b.v.i);return pv_float((a.t==PV_FLOAT?a.v.f:(double)a.v.i)-(b.t==PV_FLOAT?b.v.f:(double)b.v.i));}\n"
"static PV pv_mul(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT)return pv_int(a.v.i*b.v.i);return pv_float((a.t==PV_FLOAT?a.v.f:(double)a.v.i)*(b.t==PV_FLOAT?b.v.f:(double)b.v.i));}\n"
"static PV pv_div(PV a,PV b){\n"
"    double fa=a.t==PV_FLOAT?a.v.f:(double)a.v.i;\n"
"    double fb=b.t==PV_FLOAT?b.v.f:(double)b.v.i;\n"
"    if(fb==0.0){fprintf(stderr,\"division by zero\\n\");exit(1);}\n"
"    if(a.t==PV_INT&&b.t==PV_INT)return pv_int(a.v.i/b.v.i);\n"
"    return pv_float(fa/fb);\n"
"}\n"
"static PV pv_mod(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT){if(!b.v.i){fprintf(stderr,\"mod by zero\\n\");exit(1);}return pv_int(a.v.i%b.v.i);}fprintf(stderr,\"mod requires int\\n\");exit(1);}\n"
"static PV pv_pow(PV a,PV b){double fa=a.t==PV_FLOAT?a.v.f:(double)a.v.i,fb=b.t==PV_FLOAT?b.v.f:(double)b.v.i;return pv_float(pow(fa,fb));}\n"
"static PV pv_neg(PV a){if(a.t==PV_INT)return pv_int(-a.v.i);if(a.t==PV_FLOAT)return pv_float(-a.v.f);fprintf(stderr,\"neg requires number\\n\");exit(1);}\n"
"static PV pv_lt(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT)return pv_bool(a.v.i<b.v.i);return pv_bool((a.t==PV_FLOAT?a.v.f:(double)a.v.i)<(b.t==PV_FLOAT?b.v.f:(double)b.v.i));}\n"
"static PV pv_le(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT)return pv_bool(a.v.i<=b.v.i);return pv_bool((a.t==PV_FLOAT?a.v.f:(double)a.v.i)<=(b.t==PV_FLOAT?b.v.f:(double)b.v.i));}\n"
"static PV pv_gt(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT)return pv_bool(a.v.i>b.v.i);return pv_bool((a.t==PV_FLOAT?a.v.f:(double)a.v.i)>(b.t==PV_FLOAT?b.v.f:(double)b.v.i));}\n"
"static PV pv_ge(PV a,PV b){if(a.t==PV_INT&&b.t==PV_INT)return pv_bool(a.v.i>=b.v.i);return pv_bool((a.t==PV_FLOAT?a.v.f:(double)a.v.i)>=(b.t==PV_FLOAT?b.v.f:(double)b.v.i));}\n"
"static PV pv_eq(PV a,PV b){\n"
"    if(a.t==PV_INT&&b.t==PV_INT)return pv_bool(a.v.i==b.v.i);\n"
"    if((a.t==PV_FLOAT||a.t==PV_INT)&&(b.t==PV_FLOAT||b.t==PV_INT))return pv_bool((a.t==PV_FLOAT?a.v.f:(double)a.v.i)==(b.t==PV_FLOAT?b.v.f:(double)b.v.i));\n"
"    if(a.t==PV_BOOL&&b.t==PV_BOOL)return pv_bool(a.v.b==b.v.b);\n"
"    if(a.t==PV_STRING&&b.t==PV_STRING)return pv_bool(strcmp(a.v.s,b.v.s)==0);\n"
"    if(a.t==PV_NULL&&b.t==PV_NULL)return pv_bool(1);\n"
"    return pv_bool(0);\n"
"}\n"
"static PV pv_ne(PV a,PV b){PV r=pv_eq(a,b);r.v.b=!r.v.b;return r;}\n"
"\n"
"/* output() builtin — variadic via preprocessor trick. */\n"
"static void _prism_output_1(PV a){pv_print(a);printf(\"\\n\");}\n"
"static void _prism_output_2(PV a,PV b){pv_print(a);printf(\" \");pv_print(b);printf(\"\\n\");}\n"
"static void _prism_output_3(PV a,PV b,PV c){pv_print(a);printf(\" \");pv_print(b);printf(\" \");pv_print(c);printf(\"\\n\");}\n"
"#define output(...)  _prism_output_helper(__VA_ARGS__,_prism_output_3,_prism_output_2,_prism_output_1)(__VA_ARGS__)\n"
"#define _prism_output_helper(_1,_2,_3,N,...) N\n"
"\n"
"/* input() builtin */\n"
"static PV _prism_input(const char *prompt) {\n"
"    if(prompt) { printf(\"%s\",prompt); fflush(stdout); }\n"
"    char *line=NULL; size_t n=0;\n"
"    if(getline(&line,&n,stdin)<0){if(line)free(line);return pv_string(\"\");}\n"
"    size_t l=strlen(line);\n"
"    if(l>0&&line[l-1]=='\\n') line[l-1]='\\0';\n"
"    return pv_string(line); /* note: leaked — acceptable in transpiled code */\n"
"}\n"
"\n"
"/* abs / min / max / sqrt / floor / ceil */\n"
"static PV _prism_abs(PV a){if(a.t==PV_INT)return pv_int(a.v.i<0?-a.v.i:a.v.i);if(a.t==PV_FLOAT)return pv_float(fabs(a.v.f));return a;}\n"
"static PV _prism_min(PV a,PV b){return pv_truthy(pv_le(a,b))?a:b;}\n"
"static PV _prism_max(PV a,PV b){return pv_truthy(pv_ge(a,b))?a:b;}\n"
"static PV _prism_sqrt(PV a){double v=a.t==PV_FLOAT?a.v.f:(double)a.v.i;return pv_float(sqrt(v));}\n"
"static PV _prism_floor(PV a){double v=a.t==PV_FLOAT?a.v.f:(double)a.v.i;return pv_int((long long)floor(v));}\n"
"static PV _prism_ceil(PV a){double v=a.t==PV_FLOAT?a.v.f:(double)a.v.i;return pv_int((long long)ceil(v));}\n"
"static PV _prism_int_conv(PV a){if(a.t==PV_INT)return a;if(a.t==PV_FLOAT)return pv_int((long long)a.v.f);if(a.t==PV_BOOL)return pv_int(a.v.b==1?1:0);if(a.t==PV_STRING)return pv_int(atoll(a.v.s));return pv_int(0);}\n"
"static PV _prism_float_conv(PV a){if(a.t==PV_FLOAT)return a;if(a.t==PV_INT)return pv_float((double)a.v.i);if(a.t==PV_STRING)return pv_float(atof(a.v.s));return pv_float(0.0);}\n"
"static PV _prism_str_conv(PV a){char buf[64];switch(a.t){case PV_INT:snprintf(buf,sizeof(buf),\"%lld\",a.v.i);break;case PV_FLOAT:snprintf(buf,sizeof(buf),\"%g\",a.v.f);break;case PV_BOOL:strcpy(buf,a.v.b==1?\"true\":a.v.b==0?\"false\":\"unknown\");break;case PV_STRING:return a;default:strcpy(buf,\"null\");}return pv_string(strdup(buf));}\n"
"static PV _prism_len(PV a){if(a.t==PV_STRING)return pv_int((long long)strlen(a.v.s));fprintf(stderr,\"len: unsupported type\\n\");exit(1);}\n"
"\n"
"/* -------- end runtime -------- */\n\n",
    out);
}

/* ================================================================== expression emitter */

static void emit_binop(Ctx *ctx, ASTNode *node) {
    FILE *out = ctx->out;
    const char *op = node->binop.op;

    /* Map Prism operators to runtime calls or C operators. */
    if (strcmp(op,"**") == 0) {
        fprintf(out, "pv_pow("); emit_expr(ctx, node->binop.left);
        fprintf(out, ",");       emit_expr(ctx, node->binop.right);
        fprintf(out, ")");
        return;
    }

    const char *fn = NULL;
    if      (strcmp(op,"+")  == 0) fn = "pv_add";
    else if (strcmp(op,"-")  == 0) fn = "pv_sub";
    else if (strcmp(op,"*")  == 0) fn = "pv_mul";
    else if (strcmp(op,"/")  == 0) fn = "pv_div";
    else if (strcmp(op,"%")  == 0) fn = "pv_mod";
    else if (strcmp(op,"<")  == 0) fn = "pv_lt";
    else if (strcmp(op,"<=") == 0) fn = "pv_le";
    else if (strcmp(op,">")  == 0) fn = "pv_gt";
    else if (strcmp(op,">=") == 0) fn = "pv_ge";
    else if (strcmp(op,"==") == 0) fn = "pv_eq";
    else if (strcmp(op,"!=") == 0) fn = "pv_ne";

    if (fn) {
        fprintf(out, "%s(", fn);
        emit_expr(ctx, node->binop.left);
        fprintf(out, ",");
        emit_expr(ctx, node->binop.right);
        fprintf(out, ")");
        return;
    }

    /* Logical and / or — short-circuit via C ternary. */
    if (strcmp(op,"and") == 0 || strcmp(op,"&&") == 0) {
        fprintf(out, "(pv_truthy(");
        emit_expr(ctx, node->binop.left);
        fprintf(out, ")?(");
        emit_expr(ctx, node->binop.right);
        fprintf(out, "):pv_bool(0))");
        return;
    }
    if (strcmp(op,"or") == 0 || strcmp(op,"||") == 0) {
        /* Use a temp to avoid double evaluation. */
        fprintf(out, "(pv_truthy(");
        emit_expr(ctx, node->binop.left);
        fprintf(out, ")?(");
        emit_expr(ctx, node->binop.left);
        fprintf(out, "):(");
        emit_expr(ctx, node->binop.right);
        fprintf(out, "))");
        return;
    }

    /* Bitwise operators */
    fprintf(out, "(pv_truthy(");
    emit_expr(ctx, node->binop.left);
    fprintf(out, ")?pv_int(1):pv_int(0))/* unsupported op '%s' */", op);
}

static void emit_expr(Ctx *ctx, ASTNode *node) {
    if (!node) { fprintf(ctx->out, "pv_null()"); return; }
    FILE *out = ctx->out;
    char nbuf[256];

    switch (node->type) {
    case NODE_INT_LIT:
        fprintf(out, "pv_int(%lldLL)", (long long)node->int_lit.value);
        break;
    case NODE_FLOAT_LIT:
        fprintf(out, "pv_float(%g)", node->float_lit.value);
        break;
    case NODE_BOOL_LIT:
        fprintf(out, "pv_bool(%d)", node->bool_lit.value);
        break;
    case NODE_NULL_LIT:
        fprintf(out, "pv_null()");
        break;
    case NODE_STRING_LIT:
        fprintf(out, "pv_string(");
        emit_cstring(out, node->string_lit.value);
        fprintf(out, ")");
        break;
    case NODE_FSTRING_LIT:
        /* f-strings: emit as a string literal with a comment. */
        fprintf(out, "pv_string(/* f-string */ ");
        emit_cstring(out, node->string_lit.value);
        fprintf(out, ")");
        break;
    case NODE_IDENT:
        fprintf(out, "%s", cident(node->ident.name, nbuf, sizeof(nbuf)));
        break;
    case NODE_BINOP:
        emit_binop(ctx, node);
        break;
    case NODE_UNOP: {
        const char *op = node->unop.op;
        if (strcmp(op,"-") == 0) {
            fprintf(out, "pv_neg("); emit_expr(ctx, node->unop.operand); fprintf(out, ")");
        } else if (strcmp(op,"not") == 0 || strcmp(op,"!") == 0) {
            fprintf(out, "pv_bool(!pv_truthy("); emit_expr(ctx, node->unop.operand); fprintf(out, "))");
        } else {
            emit_expr(ctx, node->unop.operand);
        }
        break;
    }
    case NODE_TERNARY:
        fprintf(out, "(pv_truthy(");
        emit_expr(ctx, node->unop.operand); /* cond */
        fprintf(out, ")?(");
        /* NODE_TERNARY stores cond in operand, then left/right in binop */
        /* Actually Prism ternary is parsed as a special node — emit cond ? then : else */
        fprintf(out, "pv_null()/* ternary todo */");
        fprintf(out, "):pv_null())");
        break;
    case NODE_FUNC_CALL: {
        /* Map common Prism builtins to C equivalents. */
        if (node->func_call.callee->type == NODE_IDENT) {
            const char *fn = node->func_call.callee->ident.name;
            ASTNode   **args = node->func_call.args;
            int          argc = node->func_call.arg_count;

            if (strcmp(fn,"output") == 0) {
                /* output() returns null: wrap to produce a PV. */
                fprintf(out, "({");
                for (int i = 0; i < argc; i++) {
                    if (i > 0) fprintf(out, " printf(\" \");");
                    fprintf(out, "pv_print("); emit_expr(ctx, args[i]); fprintf(out, ");");
                }
                fprintf(out, " printf(\"\\n\"); pv_null();})");
                return;
            }
            if (strcmp(fn,"input") == 0) {
                fprintf(out, "_prism_input(");
                if (argc > 0) { emit_expr(ctx, args[0]); fprintf(out, ".v.s"); }
                else fprintf(out, "NULL");
                fprintf(out, ")"); return;
            }
            if (strcmp(fn,"int") == 0 && argc == 1) {
                fprintf(out, "_prism_int_conv("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"float") == 0 && argc == 1) {
                fprintf(out, "_prism_float_conv("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"str") == 0 && argc == 1) {
                fprintf(out, "_prism_str_conv("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"len") == 0 && argc == 1) {
                fprintf(out, "_prism_len("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"abs") == 0 && argc == 1) {
                fprintf(out, "_prism_abs("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"min") == 0 && argc == 2) {
                fprintf(out, "_prism_min("); emit_expr(ctx, args[0]);
                fprintf(out, ","); emit_expr(ctx, args[1]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"max") == 0 && argc == 2) {
                fprintf(out, "_prism_max("); emit_expr(ctx, args[0]);
                fprintf(out, ","); emit_expr(ctx, args[1]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"sqrt") == 0 && argc == 1) {
                fprintf(out, "_prism_sqrt("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"floor") == 0 && argc == 1) {
                fprintf(out, "_prism_floor("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
            if (strcmp(fn,"ceil") == 0 && argc == 1) {
                fprintf(out, "_prism_ceil("); emit_expr(ctx, args[0]); fprintf(out, ")"); return;
            }
        }
        /* Generic function call */
        emit_expr(ctx, node->func_call.callee);
        fprintf(out, "(");
        for (int i = 0; i < node->func_call.arg_count; i++) {
            if (i > 0) fprintf(out, ",");
            emit_expr(ctx, node->func_call.args[i]);
        }
        fprintf(out, ")");
        break;
    }
    case NODE_INDEX:
        /* Array/dict indexing: emit with a stub. */
        fprintf(out, "pv_null()/* index[");
        emit_expr(ctx, node->index_expr.obj);
        fprintf(out, "] */");
        break;
    case NODE_MEMBER:
        fprintf(out, "pv_null()/* .%s */", node->member.name);
        break;
    case NODE_METHOD_CALL:
        fprintf(out, "pv_null()/* .%s() */", node->method_call.method);
        break;
    case NODE_ARRAY_LIT:
        fprintf(out, "pv_null()/* array literal */");
        break;
    case NODE_DICT_LIT:
        fprintf(out, "pv_null()/* dict literal */");
        break;
    case NODE_SET_LIT:
        fprintf(out, "pv_null()/* set literal */");
        break;
    case NODE_TUPLE_LIT:
        fprintf(out, "pv_null()/* tuple literal */");
        break;
    case NODE_IN_EXPR:
        fprintf(out, "pv_bool(0)/* in expr */");
        break;
    case NODE_NOT_IN_EXPR:
        fprintf(out, "pv_bool(1)/* not in expr */");
        break;
    default:
        fprintf(out, "pv_null()/* unhandled expr type %d */", node->type);
        break;
    }
}

/* ================================================================== statement emitter */

static void emit_block(Ctx *ctx, ASTNode *node) {
    if (!node) return;
    if (node->type == NODE_BLOCK) {
        for (int i = 0; i < node->block.count; i++)
            emit_stmt(ctx, node->block.stmts[i]);
    } else {
        emit_stmt(ctx, node);
    }
}

static void emit_stmt(Ctx *ctx, ASTNode *node) {
    if (!node) return;
    FILE *out = ctx->out;
    char  nbuf[256];

    switch (node->type) {

    case NODE_VAR_DECL:
        indent(ctx);
        fprintf(out, "PV %s = ", cident(node->var_decl.name, nbuf, sizeof(nbuf)));
        if (node->var_decl.init) emit_expr(ctx, node->var_decl.init);
        else fprintf(out, "pv_null()");
        fprintf(out, ";\n");
        break;

    case NODE_ASSIGN: {
        indent(ctx);
        if (node->assign.target->type == NODE_IDENT) {
            fprintf(out, "%s = ", cident(node->assign.target->ident.name, nbuf, sizeof(nbuf)));
        } else {
            fprintf(out, "/* complex lvalue */ (void)(");
            emit_expr(ctx, node->assign.target);
            fprintf(out, ") = ");
        }
        emit_expr(ctx, node->assign.value);
        fprintf(out, ";\n");
        break;
    }

    case NODE_COMPOUND_ASSIGN: {
        indent(ctx);
        const char *op = node->assign.op;
        if (node->assign.target->type == NODE_IDENT) {
            const char *vn = cident(node->assign.target->ident.name, nbuf, sizeof(nbuf));
            /* Expand compound op as: var = pv_add(var, rhs) etc. */
            const char *fn = NULL;
            if      (strcmp(op,"+=") == 0) fn = "pv_add";
            else if (strcmp(op,"-=") == 0) fn = "pv_sub";
            else if (strcmp(op,"*=") == 0) fn = "pv_mul";
            else if (strcmp(op,"/=") == 0) fn = "pv_div";
            else if (strcmp(op,"%=") == 0) fn = "pv_mod";
            if (fn) {
                fprintf(out, "%s = %s(%s, ", vn, fn, vn);
                emit_expr(ctx, node->assign.value);
                fprintf(out, ");\n");
            } else {
                fprintf(out, "%s = /* op %s */ pv_null();\n", vn, op);
            }
        } else {
            fprintf(out, "/* compound assign on complex lvalue */;\n");
        }
        break;
    }

    case NODE_EXPR_STMT:
        indent(ctx);
        fprintf(out, "(void)(");
        emit_expr(ctx, node->expr_stmt.expr);
        fprintf(out, ");\n");
        break;

    case NODE_RETURN:
        indent(ctx);
        if (node->ret.value) {
            fprintf(out, "return ");
            emit_expr(ctx, node->ret.value);
            fprintf(out, ";\n");
        } else {
            fprintf(out, "return pv_null();\n");
        }
        break;

    case NODE_BREAK:
        indent(ctx); fprintf(out, "break;\n"); break;
    case NODE_CONTINUE:
        indent(ctx); fprintf(out, "continue;\n"); break;

    case NODE_IF: {
        for (int b = 0; b < node->if_stmt.branch_count; b++) {
            indent(ctx);
            if (b == 0) fprintf(out, "if");
            else        fprintf(out, "else if");
            fprintf(out, " (pv_truthy(");
            emit_expr(ctx, node->if_stmt.conds[b]);
            fprintf(out, ")) {\n");
            ctx->indent++;
            emit_block(ctx, node->if_stmt.bodies[b]);
            ctx->indent--;
            indent(ctx); fprintf(out, "}");
            if (b < node->if_stmt.branch_count - 1 || node->if_stmt.else_body)
                fprintf(out, " ");
        }
        if (node->if_stmt.else_body) {
            fprintf(out, "else {\n");
            ctx->indent++;
            emit_block(ctx, node->if_stmt.else_body);
            ctx->indent--;
            indent(ctx); fprintf(out, "}");
        }
        fprintf(out, "\n");
        break;
    }

    case NODE_WHILE:
        indent(ctx);
        fprintf(out, "while (pv_truthy(");
        emit_expr(ctx, node->while_stmt.cond);
        fprintf(out, ")) {\n");
        ctx->indent++;
        emit_block(ctx, node->while_stmt.body);
        ctx->indent--;
        indent(ctx); fprintf(out, "}\n");
        break;

    case NODE_FOR_IN: {
        /* for var in range(start, stop[, step]) → C for loop.
         * General case: emit as while loop over a simulated range. */
        const char *var = cident(node->for_in.var, nbuf, sizeof(nbuf));
        ASTNode    *iter = node->for_in.iter;

        /* Detect range() patterns. */
        bool is_range = false;
        if (iter && iter->type == NODE_FUNC_CALL &&
            iter->func_call.callee->type == NODE_IDENT &&
            strcmp(iter->func_call.callee->ident.name, "range") == 0) {
            is_range = true;
            int argc2 = iter->func_call.arg_count;
            char it_start[64], it_stop[64], it_step[64];
            int tc = ctx->tmp_counter++;
            snprintf(it_start, sizeof(it_start), "_rstart%d", tc);
            snprintf(it_stop,  sizeof(it_stop),  "_rstop%d",  tc);
            snprintf(it_step,  sizeof(it_step),  "_rstep%d",  tc);

            indent(ctx);
            fprintf(out, "{ PV %s=", it_start);
            if (argc2 >= 2) emit_expr(ctx, iter->func_call.args[0]);
            else fprintf(out, "pv_int(0)");
            fprintf(out, "; PV %s=", it_stop);
            if (argc2 >= 2) emit_expr(ctx, iter->func_call.args[1]);
            else if (argc2 == 1) emit_expr(ctx, iter->func_call.args[0]);
            else fprintf(out, "pv_int(0)");
            fprintf(out, "; PV %s=", it_step);
            if (argc2 >= 3) emit_expr(ctx, iter->func_call.args[2]);
            else fprintf(out, "pv_int(1)");
            fprintf(out, ";\n");

            indent(ctx);
            fprintf(out, "  for (PV %s=%s; pv_truthy(%s.v.i>0?pv_lt(%s,%s):pv_gt(%s,%s)); %s=pv_add(%s,%s)) {\n",
                    var, it_start, it_step,
                    var, it_stop, var, it_stop,
                    var, var, it_step);
            ctx->indent++;
            emit_block(ctx, node->for_in.body);
            ctx->indent--;
            indent(ctx); fprintf(out, "  }\n");
            indent(ctx); fprintf(out, "}\n");
        }

        if (!is_range) {
            /* Generic for-in: emit a comment and stub. */
            indent(ctx);
            fprintf(out, "/* for-in loop (non-range): unsupported in C transpiler */\n");
            indent(ctx);
            fprintf(out, "{\n");
            ctx->indent++;
            indent(ctx);
            fprintf(out, "PV %s = pv_null(); (void)%s;\n", var, var);
            emit_block(ctx, node->for_in.body);
            ctx->indent--;
            indent(ctx); fprintf(out, "}\n");
        }
        break;
    }

    case NODE_FUNC_DECL: {
        /* Emit function definition. */
        const char *fn = cident(node->func_decl.name, nbuf, sizeof(nbuf));
        fprintf(out, "\nstatic PV %s(", fn);
        for (int p = 0; p < node->func_decl.param_count; p++) {
            if (p > 0) fprintf(out, ", ");
            char pb[256];
            fprintf(out, "PV %s", cident(node->func_decl.params[p].name, pb, sizeof(pb)));
        }
        fprintf(out, ") {\n");
        ctx->indent++;
        bool was_in_func = ctx->in_func;
        ctx->in_func = true;
        emit_block(ctx, node->func_decl.body);
        /* Default return. */
        indent(ctx); fprintf(out, "return pv_null();\n");
        ctx->in_func = was_in_func;
        ctx->indent--;
        fprintf(out, "}\n");
        break;
    }

    case NODE_BLOCK:
        for (int i = 0; i < node->block.count; i++)
            emit_stmt(ctx, node->block.stmts[i]);
        break;

    case NODE_IMPORT:
        indent(ctx);
        fprintf(out, "/* import '%s' — unsupported in C transpiler */\n",
                node->import_stmt.path);
        break;

    default:
        indent(ctx);
        fprintf(out, "/* unhandled statement type %d */;\n", node->type);
        break;
    }
}

/* ================================================================== entry point */

void transpile_to_c(ASTNode *program, const char *source_name, FILE *out) {
    fprintf(out, "/* Generated by Prism C transpiler from: %s */\n",
            source_name ? source_name : "<unknown>");
    fprintf(out, "/* Compile with: cc -O2 -lm output.c -o output */\n\n");

    emit_runtime(out);

    /* Collect all top-level function declarations and emit them first. */
    if (program->type == NODE_PROGRAM || program->type == NODE_BLOCK) {
        Ctx func_ctx = {0};
        func_ctx.out = out;
        for (int i = 0; i < program->block.count; i++) {
            ASTNode *s = program->block.stmts[i];
            if (s && s->type == NODE_FUNC_DECL)
                emit_stmt(&func_ctx, s);
        }
    }

    /* Emit main(). */
    fprintf(out, "\nint main(void) {\n");
    Ctx ctx = {0};
    ctx.indent = 1;
    ctx.out    = out;

    if (program->type == NODE_PROGRAM || program->type == NODE_BLOCK) {
        for (int i = 0; i < program->block.count; i++) {
            ASTNode *s = program->block.stmts[i];
            if (s && s->type != NODE_FUNC_DECL)
                emit_stmt(&ctx, s);
        }
    } else {
        emit_stmt(&ctx, program);
    }

    fprintf(out, "    return 0;\n}\n");
}
