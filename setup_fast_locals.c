#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

char *replace(const char *text, const char *search, const char *repl) {
    char *p = strstr(text, search);
    if (!p) return (char*)text;
    size_t prefix_len = p - text;
    size_t search_len = strlen(search);
    size_t repl_len = strlen(repl);
    size_t suffix_len = strlen(p + search_len);
    char *res = malloc(prefix_len + repl_len + suffix_len + 1);
    memcpy(res, text, prefix_len);
    memcpy(res + prefix_len, repl, repl_len);
    memcpy(res + prefix_len + repl_len, p + search_len, suffix_len + 1);
    return res;
}

int main() {
    // compiler.c
    char *c = read_file("src/compiler.c");
    c = replace(c, "typedef struct LoopCtx {", "typedef struct {\n    const char *name;\n    int depth;\n    bool is_const;\n} Local;\n\n#define MAX_LOCALS 256\n\ntypedef struct LoopCtx {");
    c = replace(c, "LoopCtx   *loop;\n};", "LoopCtx   *loop; Local locals[MAX_LOCALS]; int local_count; int scope_depth; struct Compiler *outer; };");
    c = replace(c, "static uint16_t name_const(Compiler *c, const char *name) {\n    return (uint16_t)chunk_add_const_str(c->chunk, name);\n}",
        "static uint16_t name_const(Compiler *c, const char *name) { return (uint16_t)chunk_add_const_str(c->chunk, name); }\n"
        "static int resolve_local(Compiler *c, const char *name) { for (int i = c->local_count - 1; i >= 0; i--) if (strcmp(c->locals[i].name, name) == 0) return i; return -1; }\n"
        "static int add_local(Compiler *c, const char *name, bool is_const) { if (c->local_count >= MAX_LOCALS) return -1; for (int i = c->local_count - 1; i >= 0; i--) { if (c->locals[i].depth != -1 && c->locals[i].depth < c->scope_depth) break; if (strcmp(c->locals[i].name, name) == 0) return -1; } Local *l = &c->locals[c->local_count++]; l->name = name; l->depth = c->scope_depth; l->is_const = is_const; return c->local_count - 1; }\n"
        "static void begin_scope(Compiler *c) { c->scope_depth++; }\n"
        "static void end_scope(Compiler *c, int ln) { (void)ln; c->scope_depth--; while (c->local_count > 0 && c->locals[c->local_count - 1].depth > c->scope_depth) c->local_count--; }\n");
    c = replace(c, "static Chunk *compile_function_chunk(Compiler *parent, ASTNode *body);", "static Chunk *compile_function_chunk(Compiler *parent, ASTNode *body, const char *name, Param *params, int param_count);");
    c = replace(c, "static Chunk *compile_function_chunk(Compiler *parent, ASTNode *body) {\n    Chunk *chunk = malloc(sizeof(Chunk));\n    Compiler c;\n    c.chunk = chunk;\n    c.had_error = 0;\n    c.error_msg[0] = '\0';\n    c.loop = NULL;",
        "static Chunk *compile_function_chunk(Compiler *parent, ASTNode *body, const char *name, Param *params, int param_count) {\n    (void)name; Chunk *chunk = malloc(sizeof(Chunk));\n    Compiler c; memset(&c, 0, sizeof(c));\n    c.chunk = chunk; c.had_error = 0; c.error_msg[0] = '\0'; c.loop = NULL; c.outer = parent; c.scope_depth = 0; chunk_init(chunk);\n    for (int i = 0; i < param_count; i++) add_local(&c, params[i].name, false);");
    c = replace(c, "case NODE_BLOCK:\n        emit1(c, OP_PUSH_SCOPE, ln);\n        for (int i = 0; i < node->block.count; i++)\n            compile_node(c, node->block.stmts[i]);\n        emit1(c, OP_POP_SCOPE, ln);\n        break;",
        "case NODE_BLOCK: begin_scope(c); for (int i = 0; i < node->block.count; i++) compile_node(c, node->block.stmts[i]); end_scope(c, ln); break;");
    c = replace(c, "uint16_t idx = name_const(c, node->var_decl.name);\n        emit3(c, node->var_decl.is_const ? OP_DEFINE_CONST : OP_DEFINE_NAME, idx, ln);",
        "if (c->scope_depth > 0) { int slot = add_local(c, node->var_decl.name, node->var_decl.is_const); if (slot != -1) emit3(c, OP_DEFINE_LOCAL, (uint16_t)slot, ln); else compiler_error(c, \"already defined\", ln); } else { uint16_t idx = name_const(c, node->var_decl.name); emit3(c, node->var_decl.is_const ? OP_DEFINE_CONST : OP_DEFINE_NAME, idx, ln); }");
    c = replace(c, "if (tgt->type == NODE_IDENT) {\n            compile_expr(c, node->assign.value);\n            uint16_t idx = name_const(c, tgt->ident.name);\n            emit3(c, OP_STORE_NAME, idx, ln);\n        }",
        "if (tgt->type == NODE_IDENT) { compile_expr(c, node->assign.value); int slot = resolve_local(c, tgt->ident.name); if (slot != -1) emit3(c, OP_STORE_LOCAL, (uint16_t)slot, ln); else { uint16_t idx = name_const(c, tgt->ident.name); emit3(c, OP_STORE_NAME, idx, ln); } }");
    c = replace(c, "uint16_t idx = name_const(c, tgt->ident.name);\n        emit3(c, OP_LOAD_NAME, idx, ln);", "int slot = resolve_local(c, tgt->ident.name); if (slot != -1) emit3(c, OP_LOAD_LOCAL, (uint16_t)slot, ln); else { uint16_t idx = name_const(c, tgt->ident.name); emit3(c, OP_LOAD_NAME, idx, ln); }");
    c = replace(c, "emit3(c, OP_STORE_NAME, idx, ln);\n        break;", "if (slot != -1) { if (c->locals[slot].is_const) compiler_error(c, \"const\", ln); emit3(c, OP_STORE_LOCAL, (uint16_t)slot, ln); } else { uint16_t idx = name_const(c, tgt->ident.name); emit3(c, OP_STORE_NAME, idx, ln); }\n        break;");
    c = replace(c, "Chunk *fn_chunk = compile_function_chunk(c, node->func_decl.body);", "Chunk *fn_chunk = compile_function_chunk(c, node->func_decl.body, node->func_decl.name, node->func_decl.params, node->func_decl.param_count);");
    c = replace(c, "case NODE_IDENT:\n        emit3(c, OP_LOAD_NAME, name_const(c, node->ident.name), ln);\n        break;", "case NODE_IDENT: { int slot = resolve_local(c, node->ident.name); if (slot != -1) emit3(c, OP_LOAD_LOCAL, (uint16_t)slot, ln); else emit3(c, OP_LOAD_NAME, name_const(c, node->ident.name), ln); break; }");
    c = replace(c, "Chunk *fn_chunk = compile_function_chunk(c, node->fn_expr.body);", "Chunk *fn_chunk = compile_function_chunk(c, node->fn_expr.body, \"<lambda>\", node->fn_expr.params, node->fn_expr.param_count);");
    c = replace(c, "Compiler c;\n    c.chunk      = out;\n    c.had_error  = 0;\n    c.error_msg[0] = '\0';\n    c.loop       = NULL;", "Compiler c; memset(&c, 0, sizeof(c)); c.chunk = out; c.had_error = 0; c.error_msg[0] = '\0'; c.loop = NULL; c.scope_depth = 0;");
    write_file("src/compiler.c", c);

    // vm.c
    char *v = read_file("src/vm.c");
    v = replace(v, "Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);\n                for (int i = 0; i < callee->func.param_count; i++) {\n                    Value *arg = (i < argc) ? args[i] : value_null();\n                    env_set(fn_env, callee->func.params[i].name, arg, false);\n                    if (i >= argc) value_release(arg);\n                }\n                for (int i = 0; i < argc; i++) value_release(args[i]);\n                if (argc > VM_CALL_STACK_BUF) free(args);\n\n                CallFrame *new_frame = &vm->frames[vm->frame_count++];\n                new_frame->chunk = callee->func.chunk;\n                if (!new_frame->chunk->source_file)\n                    new_frame->chunk->source_file = frame->chunk->source_file;\n                new_frame->ip = 0;\n                new_frame->stack_base = vm->stack_top;\n                new_frame->env = fn_env;\n                new_frame->root_env = fn_env;\n                new_frame->owns_env   = 1;\n                new_frame->owns_chunk = 0;\n                new_frame->local_count = 0;\n                memset(new_frame->locals, 0, sizeof(new_frame->locals));",
        "Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);\n"
        "                CallFrame *new_frame = &vm->frames[vm->frame_count++];\n"
        "                new_frame->chunk = callee->func.chunk; if (!new_frame->chunk->source_file) new_frame->chunk->source_file = frame->chunk->source_file;\n"
        "                new_frame->ip = 0; new_frame->stack_base = vm->stack_top; new_frame->env = fn_env; new_frame->root_env = fn_env;\n"
        "                new_frame->owns_env = 1; new_frame->owns_chunk = 0; new_frame->local_count = callee->func.param_count; memset(new_frame->locals, 0, sizeof(new_frame->locals));\n"
        "                for (int i = 0; i < callee->func.param_count; i++) {\n"
        "                    Value *arg = (i < argc) ? args[i] : value_null();\n"
        "                    env_set(fn_env, callee->func.params[i].name, arg, false);\n"
        "                    new_frame->locals[i] = value_retain(arg);\n"
        "                    if (i >= argc) value_release(arg);\n"
        "                }\n"
        "                for (int i = 0; i < argc; i++) value_release(args[i]); if (argc > VM_CALL_STACK_BUF) free(args);");
    v = replace(v, "lbl_OP_DEFINE_LOCAL:\n        case OP_DEFINE_LOCAL: {\n            uint16_t slot = READ_U16();\n            Value *v = POP();\n            if (PRISM_LIKELY(slot < VM_LOCALS_MAX)) {\n                value_release(frame->locals[slot]);\n                frame->locals[slot] = v;\n            } else { value_release(v); }\n            DISPATCH();\n        }",
        "lbl_OP_DEFINE_LOCAL: case OP_DEFINE_LOCAL: {\n"
        "            uint16_t slot = READ_U16(); Value *v = POP();\n"
        "            if (PRISM_LIKELY(slot < VM_LOCALS_MAX)) {\n"
        "                if (frame->locals[slot]) value_release(frame->locals[slot]);\n"
        "                frame->locals[slot] = v; if (slot >= frame->local_count) frame->local_count = slot + 1;\n"
        "            } else { value_release(v); } DISPATCH(); }");
    v = replace(v, "            } else if (callee->type == VAL_FUNCTION && callee->func.chunk\n                       && callee->func.chunk == frame->chunk) {\n                /* True TCO: same function — update locals in-place, reset ip */\n                Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);\n                for (int i = 0; i < callee->func.param_count; i++) {\n                    Value *arg2 = (i < argc) ? args[i] : value_null();\n                    env_set(fn_env, callee->func.params[i].name, arg2, false);\n                    if (i >= argc) value_release(arg2);\n                }\n                for (int i = 0; i < argc; i++) value_release(args[i]);\n                if (argc > VM_CALL_STACK_BUF) free(args);\n                /* Release old locals */\n                for (int _li = 0; _li < frame->local_count; _li++) {\n                    value_release(frame->locals[_li]);\n                    frame->locals[_li] = NULL;\n                }\n                frame->local_count = 0;",
        "            } else if (callee->type == VAL_FUNCTION && callee->func.chunk && callee->func.chunk == frame->chunk) {\n"
        "                Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);\n"
        "                Value *new_locals[VM_LOCALS_MAX]; memset(new_locals, 0, sizeof(new_locals));\n"
        "                for (int i = 0; i < callee->func.param_count; i++) {\n"
        "                    Value *arg2 = (i < argc) ? args[i] : value_null();\n"
        "                    env_set(fn_env, callee->func.params[i].name, arg2, false);\n"
        "                    new_locals[i] = value_retain(arg2); if (i >= argc) value_release(arg2);\n"
        "                }\n"
        "                for (int i = 0; i < argc; i++) value_release(args[i]); if (argc > VM_CALL_STACK_BUF) free(args);\n"
        "                for (int _li = 0; _li < frame->local_count; _li++) if (frame->locals[_li]) value_release(frame->locals[_li]);\n"
        "                memcpy(frame->locals, new_locals, sizeof(new_locals)); frame->local_count = callee->func.param_count;");
    v = replace(v, "                } else {\n                    Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);\n                    for (int i = 0; i < callee->func.param_count; i++) {\n                        Value *arg2 = (i < argc) ? args[i] : value_null();\n                        env_set(fn_env, callee->func.params[i].name, arg2, false);\n                        if (i >= argc) value_release(arg2);\n                    }\n                    for (int i = 0; i < argc; i++) value_release(args[i]);\n                    if (argc > VM_CALL_STACK_BUF) free(args);\n                    CallFrame *new_frame = &vm->frames[vm->frame_count++];\n                    new_frame->chunk = callee->func.chunk;\n                    new_frame->ip = 0; new_frame->stack_base = vm->stack_top;\n                    new_frame->env = fn_env; new_frame->root_env = fn_env;\n                    new_frame->owns_env = 1; new_frame->owns_chunk = 0;\n                    new_frame->local_count = 0;\n                    memset(new_frame->locals, 0, sizeof(new_frame->locals));",
        "                } else {\n                    Env *fn_env = env_new(callee->func.closure ? callee->func.closure : vm->globals);\n                    CallFrame *new_frame = &vm->frames[vm->frame_count++];\n                    new_frame->chunk = callee->func.chunk; new_frame->ip = 0; new_frame->stack_base = vm->stack_top; new_frame->env = fn_env; new_frame->root_env = fn_env;\n                    new_frame->owns_env = 1; new_frame->owns_chunk = 0; new_frame->local_count = callee->func.param_count; memset(new_frame->locals, 0, sizeof(new_frame->locals));\n                    for (int i = 0; i < callee->func.param_count; i++) {\n                        Value *arg2 = (i < argc) ? args[i] : value_null();\n                        env_set(fn_env, callee->func.params[i].name, arg2, false);\n                        new_frame->locals[i] = value_retain(arg2); if (i >= argc) value_release(arg2);\n                    }\n                    for (int i = 0; i < argc; i++) value_release(args[i]); if (argc > VM_CALL_STACK_BUF) free(args);");
    write_file("src/vm.c", v);

    return 0;
}
