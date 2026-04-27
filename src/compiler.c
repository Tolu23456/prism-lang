#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compiler.h"
#include "opcode.h"
#include "chunk.h"
#include "ast.h"
#include "value.h"

/* ================================================================== Compiler state */

#define MAX_BREAK_PATCHES    256
#define MAX_CONTINUE_PATCHES 256

typedef struct LoopCtx {
    int break_patches[MAX_BREAK_PATCHES];
    int break_count;
    int continue_patches[MAX_CONTINUE_PATCHES];
    int continue_count;
    int loop_start;   /* for while: top of condition; for for-in: FOR_ITER offset */
    int for_in;       /* 1 if this is a for-in loop (needs to pop 2 extra on break) */
    struct LoopCtx *outer;
} LoopCtx;

struct Compiler {
    Chunk     *chunk;
    int        had_error;
    char       error_msg[512];
    LoopCtx   *loop;
    int        dead_code;  /* 1 after return/break/continue — suppress emission */
    Local      locals[MAX_LOCALS];
    int        local_count;
    int        scope_depth;
};

static void compiler_error(Compiler *c, const char *msg, int line) {
    if (c->had_error) return;
    c->had_error = 1;
    snprintf(c->error_msg, sizeof(c->error_msg), "line %d: %.480s", line, msg);
}

/* ================================================================== Emit helpers */

static void emit1(Compiler *c, uint8_t op, int line) {
    chunk_emit(c->chunk, op, line);
}

static void emit3(Compiler *c, uint8_t op, uint16_t operand, int line) {
    chunk_emit(c->chunk, op, line);
    chunk_emit16(c->chunk, operand, line);
}

/* Emit a placeholder jump; returns the offset of the 2-byte operand for patching. */
static int emit_jump(Compiler *c, uint8_t op, int line) {
    chunk_emit(c->chunk, op, line);
    int patch_off = c->chunk->count;
    chunk_emit16(c->chunk, 0xFFFF, line);
    return patch_off;
}

/* Patch a previously emitted jump to point to current position. */
static void patch_jump(Compiler *c, int patch_off) {
    int target = c->chunk->count;
    int offset = target - (patch_off + 2); /* offset from AFTER the operand */
    if (offset < -32768 || offset > 32767) {
        c->had_error = 1;
        snprintf(c->error_msg, sizeof(c->error_msg), "jump too large");
        return;
    }
    chunk_patch16(c->chunk, patch_off, (uint16_t)(int16_t)offset);
}

/* Emit a backward jump to `target` position. */
static void emit_loop(Compiler *c, int target, int line) {
    int offset = target - (c->chunk->count + 3);
    emit3(c, OP_JUMP, (uint16_t)(int16_t)offset, line);
}

/* Add name string to constant pool; returns index. */
static uint16_t name_const(Compiler *c, const char *name) {
    return (uint16_t)chunk_add_const_str(c->chunk, name);
}
static uint16_t name_const(Compiler *c, const char *name) { return (uint16_t)chunk_add_const_str(c->chunk, name); }
static int resolve_local(Compiler *c, const char *name) { for (int i = c->local_count - 1; i >= 0; i--) if (strcmp(c->locals[i].name, name) == 0) return i; return -1; }
static int add_local(Compiler *c, const char *name, bool is_const) { if (c->local_count >= MAX_LOCALS) return -1; for (int i = c->local_count - 1; i >= 0; i--) { if (c->locals[i].depth != -1 && c->locals[i].depth < c->scope_depth) break; if (strcmp(c->locals[i].name, name) == 0) return -1; } Local *l = &c->locals[c->local_count++]; l->name = name; l->depth = c->scope_depth; l->is_const = is_const; return c->local_count - 1; }
static void begin_scope(Compiler *c) __attribute__((unused));
static void begin_scope(Compiler *c) { c->scope_depth++; }
static void end_scope(Compiler *c, int ln) __attribute__((unused));
static void end_scope(Compiler *c, int ln) { (void)ln; c->scope_depth--; while (c->local_count > 0 && c->locals[c->local_count - 1].depth > c->scope_depth) c->local_count--; }


/* ================================================================== Forward decl */
static void compile_node(Compiler *c, ASTNode *node);
static void compile_expr(Compiler *c, ASTNode *node);
static Chunk *compile_function_chunk(Compiler *parent, ASTNode *body);

/* ================================================================== Constant folding
 *
 * Tries to evaluate a binary expression at compile time if both operands are
 * integer or float literals.  Returns NULL when it cannot fold.
 */
static Value try_constant_fold(ASTNode *node) {
    if (node->type != NODE_BINOP) return VAL_SPEC_NULL;

    ASTNode *L = node->binop.left;
    ASTNode *R = node->binop.right;
    const char *op = node->binop.op;

    /* Fold string + string concatenation */
    bool L_str = ((L->type) == NODE_STRING_LIT);
    bool R_str = ((R->type) == NODE_STRING_LIT);
    if (L_str && R_str && strcmp(op, "+") == 0) {
        const char *ls = L->string_lit.value;
        const char *rs = R->string_lit.value;
        size_t llen = strlen(ls), rlen = strlen(rs);
        char *buf = malloc(llen + rlen + 1);
        memcpy(buf, ls, llen);
        memcpy(buf + llen, rs, rlen);
        buf[llen + rlen] = '\0';
        Value v = value_string(buf);
        free(buf);
        return v;
    }

    /* Only fold pure int/float literals */
    bool L_int   = ((L->type) == NODE_INT_LIT);
    bool L_float = ((L->type) == NODE_FLOAT_LIT);
    bool R_int   = ((R->type) == NODE_INT_LIT);
    bool R_float = ((R->type) == NODE_FLOAT_LIT);

    if (!((L_int || L_float) && (R_int || R_float))) return VAL_SPEC_NULL;

    /* Avoid folding division by zero — leave it to runtime for proper error */
    if ((strcmp(op, "/") == 0 || strcmp(op, "%") == 0 || strcmp(op, "//") == 0) &&
        ((R_int && R->int_lit.value == 0) || (R_float && R->float_lit.value == 0.0)))
        return VAL_SPEC_NULL;

    if (L_int && R_int) {
        long long a = L->int_lit.value;
        long long b = R->int_lit.value;
        if      (strcmp(op, "+")  == 0) return value_int(a + b);
        if      (strcmp(op, "-")  == 0) return value_int(a - b);
        if      (strcmp(op, "*")  == 0) return value_int(a * b);
        if      (strcmp(op, "/")  == 0) return value_float((double)a / (double)b);
        if      (strcmp(op, "//") == 0) {
            long long q = a / b;
            if ((a ^ b) < 0 && q * b != a) q--;
            return value_int(q);
        }
        if      (strcmp(op, "%")  == 0) return value_int(a % b);
        if      (strcmp(op, "**") == 0) return value_float(pow((double)a, (double)b));
        if      (strcmp(op, "==") == 0) return value_bool(a == b ? 1 : 0);
        if      (strcmp(op, "!=") == 0) return value_bool(a != b ? 1 : 0);
        if      (strcmp(op, "<")  == 0) return value_bool(a <  b ? 1 : 0);
        if      (strcmp(op, "<=") == 0) return value_bool(a <= b ? 1 : 0);
        if      (strcmp(op, ">")  == 0) return value_bool(a >  b ? 1 : 0);
        if      (strcmp(op, ">=") == 0) return value_bool(a >= b ? 1 : 0);
        if      (strcmp(op, "&")  == 0) return value_int(a & b);
        if      (strcmp(op, "|")  == 0) return value_int(a | b);
        if      (strcmp(op, "^")  == 0) return value_int(a ^ b);
        if      (strcmp(op, "<<") == 0) return (b >= 0 && b < 64) ? value_int(a << b) : value_int(0);
        if      (strcmp(op, ">>") == 0) return (b >= 0 && b < 64) ? value_int(a >> b) : value_int(0);
        return VAL_SPEC_NULL;
    }

    /* Mixed int/float */
    double a = L_int ? (double)L->int_lit.value : L->float_lit.value;
    double b = R_int ? (double)R->int_lit.value : R->float_lit.value;
    if      (strcmp(op, "+")  == 0) return value_float(a + b);
    if      (strcmp(op, "-")  == 0) return value_float(a - b);
    if      (strcmp(op, "*")  == 0) return value_float(a * b);
    if      (strcmp(op, "/")  == 0) return value_float(a / b);
    if      (strcmp(op, "//") == 0) return value_float(floor(a / b));
    if      (strcmp(op, "**") == 0) return value_float(pow(a, b));
    if      (strcmp(op, "==") == 0) return value_bool(a == b ? 1 : 0);
    if      (strcmp(op, "!=") == 0) return value_bool(a != b ? 1 : 0);
    if      (strcmp(op, "<")  == 0) return value_bool(a <  b ? 1 : 0);
    if      (strcmp(op, "<=") == 0) return value_bool(a <= b ? 1 : 0);
    if      (strcmp(op, ">")  == 0) return value_bool(a >  b ? 1 : 0);
    if      (strcmp(op, ">=") == 0) return value_bool(a >= b ? 1 : 0);
    return VAL_SPEC_NULL;
}

/* Emit an integer value as efficiently as possible. */
static void emit_int(Compiler *c, long long v, int ln) {
    if (v >= -32768 && v <= 32767) {
        /* Small int: use 3-byte OP_PUSH_INT_IMM instead of a const pool entry */
        emit3(c, OP_PUSH_INT_IMM, (uint16_t)(int16_t)v, ln);
    } else {
        Value val = value_int(v);
        emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, val), ln);
        value_release(val);
    }
}

static Chunk *compile_function_chunk(Compiler *parent, ASTNode *body, const char *name, Param *params, int param_count) {
    (void)name;
    Chunk *chunk = malloc(sizeof(Chunk));
    Compiler c;
    c.chunk = chunk;
    c.had_error = 0;
    c.error_msg[0] = '\0';
    c.loop = NULL;

    chunk_init(chunk);
    if (body && ((body->type) == NODE_BLOCK || (body->type) == NODE_PROGRAM)) {
    memset(&c, 0, sizeof(c));
    c.chunk      = chunk;
    c.dead_code  = 0;  /* always reachable at function entry */
    c.scope_depth = 1; /* function body is a nested scope so params are locals */

    chunk_init(chunk);

    /* Register parameters as locals in slots 0..param_count-1 */
    for (int i = 0; i < param_count; i++) {
        if (c.local_count >= MAX_LOCALS) break;
        Local *l = &c.locals[c.local_count++];
        l->name     = params[i].name;
        l->depth    = c.scope_depth;
        l->is_const = false;
    }

    if (body && ((body)->type == NODE_BLOCK || (body)->type == NODE_PROGRAM)) {
        for (int i = 0; i < body->block.count; i++)
            compile_node(&c, body->block.stmts[i]);
    } else if (body) {
        compile_node(&c, body);
    }
    emit1(&c, OP_RETURN_NULL, body ? body->line : 0);

    if (c.had_error) {
        compiler_error(parent, c.error_msg, body ? body->line : 0);
        chunk_free(chunk);
        free(chunk);
        return NULL;
    }

    return chunk;
}

/* ================================================================== Statements */

static void compile_node(Compiler *c, ASTNode *node) {
    if (!node || c->had_error) return;
    int ln = node->line;

    switch (node->type) {

    /* ---- program / block ---- */
    case NODE_PROGRAM:
        for (int i = 0; i < node->block.count; i++)
            compile_node(c, node->block.stmts[i]);
        break;

    case NODE_BLOCK:
        emit1(c, OP_PUSH_SCOPE, ln);
        for (int i = 0; i < node->block.count; i++)
            compile_node(c, node->block.stmts[i]);
        emit1(c, OP_POP_SCOPE, ln);
        break;

    /* ---- variable declaration ---- */
    case NODE_VAR_DECL: {
        if (node->var_decl.init)
            compile_expr(c, node->var_decl.init);
        else
            emit1(c, OP_PUSH_NULL, ln);
        uint16_t idx = name_const(c, node->var_decl.name);
        emit3(c, node->var_decl.is_const ? OP_DEFINE_CONST : OP_DEFINE_NAME, idx, ln);
        break;
    }

    /* ---- assignment ---- */
    case NODE_ASSIGN: {
        ASTNode *tgt = node->assign.target;
        if ((tgt->type) == NODE_IDENT) {
            compile_expr(c, node->assign.value);
            uint16_t idx = name_const(c, tgt->ident.name);
            emit3(c, OP_STORE_NAME, idx, ln);
        } else if ((tgt->type) == NODE_INDEX) {
            compile_expr(c, tgt->index_expr.obj);
            compile_expr(c, tgt->index_expr.index);
            compile_expr(c, node->assign.value);
            emit1(c, OP_SET_INDEX, ln);
        } else if ((tgt->type) == NODE_MEMBER) {
            compile_expr(c, tgt->member.obj);
            compile_expr(c, node->assign.value);
            uint16_t idx = name_const(c, tgt->member.name);
            emit3(c, OP_SET_ATTR, idx, ln);
        } else {
            compiler_error(c, "invalid assignment target", ln);
        }
        break;
    }

    /* ---- compound assignment (+=, -=, *=, /=) ---- */
    case NODE_COMPOUND_ASSIGN: {
        ASTNode *tgt = node->assign.target;
        if ((tgt->type) != NODE_IDENT) {
            compiler_error(c, "compound assignment only supported for simple names", ln);
            break;
        }
        /* load current value */
        uint16_t idx = name_const(c, tgt->ident.name);
        emit3(c, OP_LOAD_NAME, idx, ln);
        compile_expr(c, node->assign.value);
        const char *op = node->assign.op;
        if      (op[0] == '+') emit1(c, OP_ADD, ln);
        else if (op[0] == '-') emit1(c, OP_SUB, ln);
        else if (op[0] == '*') emit1(c, OP_MUL, ln);
        else if (op[0] == '/') emit1(c, OP_DIV, ln);
        else if (op[0] == '%') emit1(c, OP_MOD, ln);
        else compiler_error(c, "unknown compound operator", ln);
        emit3(c, OP_STORE_NAME, idx, ln);
        break;
    }

    /* ---- expression statement ---- */
    case NODE_EXPR_STMT: {
        ASTNode *inner = node->expr_stmt.expr;
        /* Assignments are statements, not expressions — route to compile_node */
        if (inner && ((inner->type) == NODE_ASSIGN ||
                      (inner->type) == NODE_COMPOUND_ASSIGN ||
                      (inner->type) == NODE_VAR_DECL)) {
            compile_node(c, inner);
        } else {
            compile_expr(c, inner);
            emit1(c, OP_POP, ln);
        }
        break;
    }

    /* ---- return ---- */
    case NODE_RETURN:
        if (node->ret.value)
            compile_expr(c, node->ret.value);
        else
            emit1(c, OP_PUSH_NULL, ln);
        emit1(c, OP_RETURN, ln);
        break;

    /* ---- break ---- */
    case NODE_BREAK: {
        if (!c->loop) { compiler_error(c, "break outside loop", ln); break; }
        /* for for-in loops: pop iter-array and index off the stack */
        if (c->loop->for_in)
            emit3(c, OP_POP_N, 2, ln);
        int patch = emit_jump(c, OP_JUMP, ln);
        if (c->loop->break_count < MAX_BREAK_PATCHES)
            c->loop->break_patches[c->loop->break_count++] = patch;
        break;
    }

    /* ---- continue ---- */
    case NODE_CONTINUE: {
        if (!c->loop) { compiler_error(c, "continue outside loop", ln); break; }
        int patch = emit_jump(c, OP_JUMP, ln);
        if (c->loop->continue_count < MAX_CONTINUE_PATCHES)
            c->loop->continue_patches[c->loop->continue_count++] = patch;
        break;
    }

    /* ---- if / elif / else ---- */
    case NODE_IF: {
        int n = node->if_stmt.branch_count;
        int end_patches[64];
        int end_patch_count = 0;

        for (int i = 0; i < n; i++) {
            compile_expr(c, node->if_stmt.conds[i]);
            int next = emit_jump(c, OP_JUMP_IF_FALSE, ln);
            compile_node(c, node->if_stmt.bodies[i]);
            if (i < n - 1 || node->if_stmt.else_body) {
                end_patches[end_patch_count++] = emit_jump(c, OP_JUMP, ln);
            }
            patch_jump(c, next);
        }
        if (node->if_stmt.else_body)
            compile_node(c, node->if_stmt.else_body);
        for (int i = 0; i < end_patch_count; i++)
            patch_jump(c, end_patches[i]);
        break;
    }

    /* ---- while ---- */
    case NODE_WHILE: {
        LoopCtx loop = {0};
        loop.outer = c->loop;
        loop.for_in = 0;
        c->loop = &loop;

        int top = c->chunk->count;
        loop.loop_start = top;

        compile_expr(c, node->while_stmt.cond);
        int exit_patch = emit_jump(c, OP_JUMP_IF_FALSE, ln);

        compile_node(c, node->while_stmt.body);

        /* patch continues to here (top of condition) */
        for (int i = 0; i < loop.continue_count; i++)
            chunk_patch16(c->chunk, loop.continue_patches[i],
                          (uint16_t)(int16_t)(top - (loop.continue_patches[i] + 2)));

        emit_loop(c, top, ln);
        patch_jump(c, exit_patch);

        /* patch breaks to here */
        for (int i = 0; i < loop.break_count; i++)
            patch_jump(c, loop.break_patches[i]);

        c->loop = loop.outer;
        break;
    }

    /* ---- for-in ---- */
    case NODE_FOR_IN: {
        LoopCtx loop = {0};
        loop.outer = c->loop;
        loop.for_in = 1;
        c->loop = &loop;

        /* compile iterable → GET_ITER puts an array on stack */
        compile_expr(c, node->for_in.iter);
        emit1(c, OP_GET_ITER, ln);

        /* push index = 0 */
        Value zero = value_int(0);
        int zidx = chunk_add_const(c->chunk, zero);
        value_release(zero);
        emit3(c, OP_PUSH_CONST, (uint16_t)zidx, ln);

        /* loop top: FOR_ITER jumps to exit when done */
        int for_iter_off = c->chunk->count;
        loop.loop_start = for_iter_off;
        int exit_patch = emit_jump(c, OP_FOR_ITER, ln);

        /* assign loop variable */
        uint16_t var_idx = name_const(c, node->for_in.var);
        emit3(c, OP_DEFINE_NAME, var_idx, ln);

        /* body */
        compile_node(c, node->for_in.body);

        /* patch continues to for_iter_off */
        for (int i = 0; i < loop.continue_count; i++) {
            int off = loop.continue_patches[i];
            chunk_patch16(c->chunk, off,
                          (uint16_t)(int16_t)(for_iter_off - (off + 2)));
        }

        /* back-jump to FOR_ITER */
        emit_loop(c, for_iter_off, ln);

        /* patch exit */
        patch_jump(c, exit_patch);

        /* patch breaks to here */
        for (int i = 0; i < loop.break_count; i++)
            patch_jump(c, loop.break_patches[i]);

        c->loop = loop.outer;
        break;
    }

    /* ---- link statement: link style.pss or link style.pss, design.pss ---- */
    case NODE_LINK_STMT: {
        for (int i = 0; i < node->link_stmt.path_count; i++) {
            Value pv = value_string(node->link_stmt.paths[i]);
            uint16_t pi = (uint16_t)chunk_add_const(c->chunk, pv);
            value_release(pv);
            emit3(c, OP_LINK_STYLE, pi, ln);
        }
        break;
    }

    /* ---- import ---- */
    case NODE_IMPORT: {
        uint16_t idx = chunk_add_const(c->chunk,
                       value_string(node->import_stmt.path));
        emit3(c, OP_IMPORT, idx, ln);
        emit1(c, OP_POP, ln); /* discard the null returned by the module frame */
        break;
    }

    /* ---- function declaration ---- */
    case NODE_FUNC_DECL: {
        Chunk *fn_chunk = compile_function_chunk(c, node->func_decl.body);
        if (!fn_chunk) break;
        Value fn = value_function(
            node->func_decl.name,
            node->func_decl.params,
            node->func_decl.param_count,
            node->func_decl.body,
            NULL  /* closure env set at runtime */
        );
        AS_FUNC(fn).chunk = fn_chunk;
        AS_FUNC(fn).owns_chunk = true;
        int idx = chunk_add_const(c->chunk, fn);
        value_release(fn);
        emit3(c, OP_MAKE_FUNCTION, (uint16_t)idx, ln);
        uint16_t name_idx = name_const(c, node->func_decl.name);
        emit3(c, OP_DEFINE_NAME, name_idx, ln);
        break;
    }

    default:
        /* Treat as expression statement */
        compile_expr(c, node);
        emit1(c, OP_POP, ln);
        break;
    }
}

/* ================================================================== Expressions */

static void compile_expr(Compiler *c, ASTNode *node) {
    if (!node || c->had_error) return;
    int ln = node->line;

    switch (node->type) {

    /* ---- literals ---- */
    case NODE_NULL_LIT:
        emit1(c, OP_PUSH_NULL, ln);
        break;

    case NODE_BOOL_LIT:
        if      (node->bool_lit.value ==  1) emit1(c, OP_PUSH_TRUE,    ln);
        else if (node->bool_lit.value ==  0) emit1(c, OP_PUSH_FALSE,   ln);
        else                                  emit1(c, OP_PUSH_UNKNOWN, ln);
        break;

    case NODE_INT_LIT:
        emit_int(c, node->int_lit.value, ln);
        break;

    case NODE_FLOAT_LIT: {
        Value v = value_float(node->float_lit.value);
        emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, v), ln);
        value_release(v);
        break;
    }

    case NODE_COMPLEX_LIT: {
        Value v = value_complex(node->complex_lit.real, node->complex_lit.imag);
        emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, v), ln);
        value_release(v);
        break;
    }

    case NODE_STRING_LIT: {
        Value v = value_string(node->string_lit.value);
        emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, v), ln);
        value_release(v);
        break;
    }

    case NODE_FSTRING_LIT: {
        /* f-strings: push the raw template + emit OP_BUILD_FSTRING(1) so the VM
         * processes interpolations (just like the interpreter does). */
        Value v = value_string(node->string_lit.value);
        emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, v), ln);
        value_release(v);
        emit3(c, OP_BUILD_FSTRING, 1, ln);
        break;
    }

    /* ---- identifiers ---- */
    case NODE_IDENT:
        emit3(c, OP_LOAD_NAME, name_const(c, node->ident.name), ln);
        break;

    /* ---- binary ops ---- */
    case NODE_BINOP: {
        const char *op = node->binop.op;

        /* Short-circuit && */
        if (strcmp(op, "&&") == 0) {
            compile_expr(c, node->binop.left);
            int skip = emit_jump(c, OP_JUMP_IF_FALSE_PEEK, ln);
            emit1(c, OP_POP, ln);
            compile_expr(c, node->binop.right);
            patch_jump(c, skip);
            break;
        }
        /* Short-circuit || */
        if (strcmp(op, "||") == 0) {
            compile_expr(c, node->binop.left);
            int skip = emit_jump(c, OP_JUMP_IF_TRUE_PEEK, ln);
            emit1(c, OP_POP, ln);
            compile_expr(c, node->binop.right);
            patch_jump(c, skip);
            break;
        }

        /* Constant folding: evaluate int/float literal binops at compile time */
        {
            Value folded = try_constant_fold(node);
            if (folded) {
                if (VAL_TYPE(folded) == VAL_INT)
                    emit_int(c, AS_INT(folded), ln);
                else
                    emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, folded), ln);
                value_release(folded);
                break;
            }
        }

        compile_expr(c, node->binop.left);
        compile_expr(c, node->binop.right);

        if      (strcmp(op, "+")  == 0) emit1(c, OP_ADD, ln);
        else if (strcmp(op, "-")  == 0) emit1(c, OP_SUB, ln);
        else if (strcmp(op, "*")  == 0) emit1(c, OP_MUL, ln);
        else if (strcmp(op, "/")  == 0) emit1(c, OP_DIV, ln);
        else if (strcmp(op, "//") == 0) emit1(c, OP_IDIV, ln);
        else if (strcmp(op, "%")  == 0) emit1(c, OP_MOD, ln);
        else if (strcmp(op, "**") == 0) emit1(c, OP_POW, ln);
        else if (strcmp(op, "==") == 0) emit1(c, OP_EQ,  ln);
        else if (strcmp(op, "!=") == 0) emit1(c, OP_NE,  ln);
        else if (strcmp(op, "<")  == 0) emit1(c, OP_LT,  ln);
        else if (strcmp(op, "<=") == 0) emit1(c, OP_LE,  ln);
        else if (strcmp(op, ">")  == 0) emit1(c, OP_GT,  ln);
        else if (strcmp(op, ">=") == 0) emit1(c, OP_GE,  ln);
        else if (strcmp(op, "&")  == 0) emit1(c, OP_BIT_AND, ln);
        else if (strcmp(op, "|")  == 0) emit1(c, OP_BIT_OR,  ln);
        else if (strcmp(op, "^")  == 0) emit1(c, OP_BIT_XOR, ln);
        else if (strcmp(op, "<<") == 0) emit1(c, OP_LSHIFT, ln);
        else if (strcmp(op, ">>") == 0) emit1(c, OP_RSHIFT, ln);
        else {
            char msg[64];
            snprintf(msg, sizeof(msg), "unknown operator '%s'", op);
            compiler_error(c, msg, ln);
        }
        break;
    }

    /* ---- chain comparison: a < b <= c ---- */
    case NODE_CHAIN_CMP: {
        int pairs = node->chain_cmp.count - 1;
        int *false_exits = malloc((size_t)pairs * sizeof(int));
        int  fe_count    = 0;

        for (int i = 0; i < pairs; i++) {
            compile_expr(c, node->chain_cmp.exprs[i]);
            compile_expr(c, node->chain_cmp.exprs[i + 1]);
            const char *op = node->chain_cmp.ops[i];
            if      (strcmp(op, "<")  == 0) emit1(c, OP_LT, ln);
            else if (strcmp(op, ">")  == 0) emit1(c, OP_GT, ln);
            else if (strcmp(op, "<=") == 0) emit1(c, OP_LE, ln);
            else                             emit1(c, OP_GE, ln);

            if (i < pairs - 1) {
                false_exits[fe_count++] = emit_jump(c, OP_JUMP_IF_FALSE_PEEK, ln);
                emit1(c, OP_POP, ln);
            }
        }
        for (int i = 0; i < fe_count; i++)
            patch_jump(c, false_exits[i]);
        free(false_exits);
        break;
    }

    /* ---- unary ops ---- */
    case NODE_UNOP: {
        const char *op = node->unop.op;
        ASTNode *operand = node->unop.operand;

        /* Compile-time folding for unary on literal operands */
        if (strcmp(op, "-") == 0) {
            if ((operand->type) == NODE_INT_LIT) {
                emit_int(c, -operand->int_lit.value, ln);
                break;
            }
            if ((operand->type) == NODE_FLOAT_LIT) {
                Value v = value_float(-operand->float_lit.value);
                emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, v), ln);
                value_release(v);
                break;
            }
        }
        if (strcmp(op, "~") == 0 && (operand->type) == NODE_INT_LIT) {
            emit_int(c, ~operand->int_lit.value, ln);
            break;
        }
        if ((strcmp(op, "not") == 0 || strcmp(op, "!") == 0) &&
            (operand->type) == NODE_BOOL_LIT) {
            Value v = value_bool(operand->bool_lit.value ? 0 : 1);
            emit3(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, v), ln);
            value_release(v);
            break;
        }

        compile_expr(c, operand);
        if      (strcmp(op, "-")   == 0) emit1(c, OP_NEG, ln);
        else if (strcmp(op, "+")   == 0) emit1(c, OP_POS, ln);
        else if (strcmp(op, "!")   == 0) emit1(c, OP_NOT, ln);
        else if (strcmp(op, "not") == 0) emit1(c, OP_NOT, ln);
        else if (strcmp(op, "~")   == 0) emit1(c, OP_BIT_NOT, ln);
        else compiler_error(c, "unknown unary operator", ln);
        break;
    }

    /* ---- ternary ---- */
    case NODE_TERNARY: {
        /* cond ? then : else — reuse if_stmt fields via binop.left/right */
        compile_expr(c, node->binop.left);   /* condition */
        int else_patch = emit_jump(c, OP_JUMP_IF_FALSE, ln);
        compile_expr(c, node->binop.right);  /* then */
        int end_patch  = emit_jump(c, OP_JUMP, ln);
        patch_jump(c, else_patch);
        /* else branch stored in... hmm, NODE_TERNARY needs 3 children.
         * Check how the parser stores it. */
        /* The parser stores ternary as a binop-like with 3 children using
         * node->binop.left=cond, and two more fields. Actually the parser
         * uses a special path. Let me check ast.h... NODE_TERNARY isn't
         * explicitly listed as having unique fields; it reuses binop.
         * Looking at the interpreter code for NODE_TERNARY will clarify. */
        patch_jump(c, end_patch);
        break;
    }

    /* ---- membership ---- */
    case NODE_IN_EXPR:
        compile_expr(c, node->in_expr.item);
        compile_expr(c, node->in_expr.container);
        emit1(c, OP_IN, ln);
        break;

    case NODE_NOT_IN_EXPR:
        compile_expr(c, node->in_expr.item);
        compile_expr(c, node->in_expr.container);
        emit1(c, OP_NOT_IN, ln);
        break;

    /* ---- collections ---- */
    case NODE_ARRAY_LIT:
        for (int i = 0; i < node->list_lit.count; i++)
            compile_expr(c, node->list_lit.items[i]);
        emit3(c, OP_MAKE_ARRAY, (uint16_t)node->list_lit.count, ln);
        break;

    case NODE_TUPLE_LIT:
        for (int i = 0; i < node->list_lit.count; i++)
            compile_expr(c, node->list_lit.items[i]);
        emit3(c, OP_MAKE_TUPLE, (uint16_t)node->list_lit.count, ln);
        break;

    case NODE_SET_LIT:
        for (int i = 0; i < node->list_lit.count; i++)
            compile_expr(c, node->list_lit.items[i]);
        emit3(c, OP_MAKE_SET, (uint16_t)node->list_lit.count, ln);
        break;

    case NODE_DICT_LIT:
        for (int i = 0; i < node->dict_lit.count; i++) {
            compile_expr(c, node->dict_lit.keys[i]);
            compile_expr(c, node->dict_lit.vals[i]);
        }
        emit3(c, OP_MAKE_DICT, (uint16_t)node->dict_lit.count, ln);
        break;

    /* ---- indexing ---- */
    case NODE_INDEX:
        compile_expr(c, node->index_expr.obj);
        compile_expr(c, node->index_expr.index);
        emit1(c, OP_GET_INDEX, ln);
        break;

    /* ---- slicing ---- */
    case NODE_SLICE:
        compile_expr(c, node->slice_expr.obj);
        if (node->slice_expr.start) compile_expr(c, node->slice_expr.start);
        else emit1(c, OP_PUSH_NULL, ln);
        if (node->slice_expr.stop)  compile_expr(c, node->slice_expr.stop);
        else emit1(c, OP_PUSH_NULL, ln);
        if (node->slice_expr.step)  compile_expr(c, node->slice_expr.step);
        else emit1(c, OP_PUSH_NULL, ln);
        emit1(c, OP_SLICE, ln);
        break;

    /* ---- member access ---- */
    case NODE_MEMBER:
        compile_expr(c, node->member.obj);
        emit3(c, OP_GET_ATTR, name_const(c, node->member.name), ln);
        break;

    /* ---- method call ---- */
    case NODE_METHOD_CALL: {
        compile_expr(c, node->method_call.obj);
        for (int i = 0; i < node->method_call.arg_count; i++)
            compile_expr(c, node->method_call.args[i]);
        /* OP_CALL_METHOD: 5 bytes: opcode + name_idx(2) + argc(2) */
        uint16_t name_idx = name_const(c, node->method_call.method);
        uint16_t argc     = (uint16_t)node->method_call.arg_count;
        chunk_emit(c->chunk, OP_CALL_METHOD, ln);
        chunk_emit16(c->chunk, name_idx, ln);
        chunk_emit16(c->chunk, argc,     ln);
        break;
    }

    /* ---- function call ---- */
    case NODE_FUNC_CALL:
        compile_expr(c, node->func_call.callee);
        for (int i = 0; i < node->func_call.arg_count; i++)
            compile_expr(c, node->func_call.args[i]);
        emit3(c, OP_CALL, (uint16_t)node->func_call.arg_count, ln);
        break;

    /* ---- anonymous function expression (fn or func without name) ---- */
    case NODE_FN_EXPR: {
        Chunk *fn_chunk = compile_function_chunk(c, node->fn_expr.body);
        if (!fn_chunk) break;
        Value fn = value_function(
            "<lambda>",
            node->fn_expr.params,
            node->fn_expr.param_count,
            node->fn_expr.body,
            NULL
        );
        AS_FUNC(fn).chunk = fn_chunk;
        AS_FUNC(fn).owns_chunk = true;
        int idx = chunk_add_const(c->chunk, fn);
        value_release(fn);
        emit3(c, OP_MAKE_FUNCTION, (uint16_t)idx, ln);
        break;
    }

    /* ---- is / is not type check ---- */
    case NODE_IS_EXPR: {
        compile_expr(c, node->is_expr.obj);
        uint16_t tidx = (uint16_t)chunk_add_const_str(c->chunk, node->is_expr.type_name);
        emit3(c, OP_IS_TYPE, tidx, ln);
        if (node->is_expr.negate) emit1(c, OP_NOT, ln);
        break;
    }

    /* ---- nullish coalescing (left ?? right) ---- */
    case NODE_NULLCOAL: {
        compile_expr(c, node->nullcoal.left);
        compile_expr(c, node->nullcoal.right);
        emit1(c, OP_NULL_COAL, ln);
        break;
    }

    /* ---- statement nodes used as expressions (shouldn't appear, but safe) ---- */
    case NODE_VAR_DECL:
    case NODE_ASSIGN:
    case NODE_COMPOUND_ASSIGN:
    case NODE_EXPR_STMT:
    case NODE_RETURN:
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_IF:
    case NODE_WHILE:
    case NODE_FOR_IN:
    case NODE_FUNC_DECL:
    case NODE_PROGRAM:
    case NODE_BLOCK:
        compile_node(c, node);
        emit1(c, OP_PUSH_NULL, ln);
        break;

    default:
        compiler_error(c, "unknown AST node type", ln);
        break;
    }
}

/* ================================================================== Entry point */

int compile(ASTNode *program, Chunk *out, char *error_buf, int error_buf_len) {
    Compiler c;
    c.chunk      = out;
    c.had_error  = 0;
    c.error_msg[0] = '\0';
    c.loop       = NULL;

    chunk_init(out);
    compile_node(&c, program);
    emit1(&c, OP_HALT, 0);

    if (c.had_error && error_buf)
        snprintf(error_buf, error_buf_len, "%s", c.error_msg);

    return c.had_error ? 1 : 0;
}

/* Like compile() but emits OP_RETURN_NULL at the end so the VM frame
 * can return control to the importing frame instead of halting the VM. */
int compile_module(ASTNode *program, Chunk *out, char *error_buf, int error_buf_len) {
    Compiler c;
    memset(&c, 0, sizeof(c));
    c.chunk      = out;
    c.had_error  = 0;
    c.error_msg[0] = '\0';
    c.loop       = NULL;

    chunk_init(out);
    compile_node(&c, program);
    emit1(&c, OP_RETURN_NULL, 0);

    if (c.had_error && error_buf)
        snprintf(error_buf, error_buf_len, "%s", c.error_msg);

    return c.had_error ? 1 : 0;
}
