#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "value.h"
#include "interpreter.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "gui_native.h"

/* ------------------------------------------------------------------ file reader */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------ VM script runner */

static int run_source_vm(const char *source, const char *filename) {
    /* Parse */
    Parser  *parser  = parser_new(source);
    ASTNode *program = parser_parse(parser);

    if (parser->had_error) {
        fprintf(stderr, "[%s] Parse error: %s\n", filename, parser->error_msg);
        parser_free(parser);
        if (program) ast_node_free(program);
        return 1;
    }

    /* Compile — compile() returns 0 on success, non-zero on error */
    Chunk  chunk;
    char   errbuf[512] = {0};
    int    cerr = compile(program, &chunk, errbuf, sizeof(errbuf));

    if (cerr) {
        fprintf(stderr, "[%s] Compile error: %s\n", filename,
                errbuf[0] ? errbuf : "(unknown compiler error)");
        chunk_free(&chunk);
        ast_node_free(program);
        parser_free(parser);
        return 1;
    }

    /* Execute — AST must stay alive while VM runs (function bodies are AST ptrs) */
    VM *vm = vm_new();
    gui_register_builtins(vm->globals);

    vm_run(vm, &chunk);

    int exit_code = 0;
    if (vm->had_error) {
        fprintf(stderr, "[%s] Runtime error: %s\n", filename, vm->error_msg);
        exit_code = 1;
    }

    vm_free(vm);
    chunk_free(&chunk);

    /* Safe to free AST now that VM is done */
    ast_node_free(program);
    parser_free(parser);

    return exit_code;
}

/* ------------------------------------------------------------------ REPL */

static void run_repl(void) {
    printf("Prism 0.2.0 - Interactive Mode (type 'exit' or Ctrl-D to quit)\n\n");

    Interpreter *interp = interpreter_new();
    gui_register_builtins(interp->globals);

    char line[4096];
    while (1) {
        printf(">>> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }

        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
        if (line[0] == '\0') continue;

        Parser  *p   = parser_new(line);
        ASTNode *ast = parser_parse(p);

        if (p->had_error) {
            fprintf(stderr, "Parse error: %s\n", p->error_msg);
        } else {
            interp->had_error  = 0;
            interp->returning  = false;
            interp->return_val = NULL;

            Value *result = interpreter_eval(interp, ast, interp->globals);
            if (interp->had_error) {
                fprintf(stderr, "Error: %s\n", interp->error_msg);
            } else if (result && result->type != VAL_NULL) {
                char *s = value_to_string(result);
                printf("%s\n", s);
                free(s);
                value_release(result);
            } else if (result) {
                value_release(result);
            }
        }

        ast_node_free(ast);
        parser_free(p);
    }

    interpreter_free(interp);
}

/* ------------------------------------------------------------------ entry point */

int main(int argc, char **argv) {
    if (argc == 1) {
        run_repl();
        return 0;
    }

    if (argc == 2) {
        const char *path = argv[1];
        const char *dot  = strrchr(path, '.');
        if (!dot || strcmp(dot, ".pm") != 0) {
            fprintf(stderr, "Warning: '%s' does not have .pm extension\n", path);
        }
        char *src = read_file(path);
        if (!src) return 1;
        int code = run_source_vm(src, path);
        free(src);
        return code;
    }

    fprintf(stderr, "Usage: prism [file.pm]\n");
    return 1;
}
