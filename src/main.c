#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "value.h"
#include "interpreter.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "gui_native.h"
#include "gc.h"

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

static bool opt_emit_bytecode = false;
static bool opt_bench         = false;

static void bytecode_path_for_source(const char *filename, char *out, size_t out_len) {
    snprintf(out, out_len, "%s", filename ? filename : "out.pm");
    char *dot = strrchr(out, '.');
    if (dot) snprintf(dot, out_len - (size_t)(dot - out), ".pmc");
    else     strncat(out, ".pmc", out_len - strlen(out) - 1);
}

/* ------------------------------------------------------------------ VM script runner */

static int run_source_vm(const char *source, const char *filename) {
    Parser  *parser  = parser_new(source);
    ASTNode *program = parser_parse(parser);

    if (parser->had_error) {
        fprintf(stderr, "[%s] Parse error: %s\n", filename, parser->error_msg);
        parser_free(parser);
        if (program) ast_node_free(program);
        return 1;
    }

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

    if (opt_emit_bytecode) {
        char out_path[512];
        bytecode_path_for_source(filename, out_path, sizeof(out_path));
        if (chunk_write_bytecode(&chunk, out_path)) {
            fprintf(stderr, "[%s] Bytecode error: could not write '%s'\n", filename, out_path);
            chunk_free(&chunk);
            ast_node_free(program);
            parser_free(parser);
            return 1;
        }
        printf("[prism] bytecode written to %s\n", out_path);
    }

    VM *vm = vm_new();
    chunk.source_file = filename;
    gui_register_builtins(vm->globals);

    vm_run(vm, &chunk);
    gc_collect_audit(vm->gc, vm->globals, vm, &chunk);

    int exit_code = 0;
    if (vm->had_error) {
        fprintf(stderr, "[%s] Runtime error: %s\n", filename, vm->error_msg);
        exit_code = 1;
    }

    vm_free(vm);
    chunk_free(&chunk);
    ast_node_free(program);
    parser_free(parser);

    return exit_code;
}

static int run_source_tree(const char *source, const char *filename) {
    Parser  *parser  = parser_new(source);
    ASTNode *program = parser_parse(parser);

    if (parser->had_error) {
        fprintf(stderr, "[%s] Parse error: %s\n", filename, parser->error_msg);
        parser_free(parser);
        if (program) ast_node_free(program);
        return 1;
    }

    Interpreter *interp = interpreter_new();
    interp->filename = filename;
    gui_register_builtins(interp->globals);
    Value *result = interpreter_eval(interp, program, interp->globals);
    if (result) value_release(result);

    int exit_code = 0;
    if (interp->had_error) {
        fprintf(stderr, "[%s] Runtime error: %s\n", filename, interp->error_msg);
        exit_code = 1;
    }

    interpreter_free(interp);
    ast_node_free(program);
    parser_free(parser);
    return exit_code;
}

static int run_benchmark(const char *source, const char *filename) {
    /* benchmark mode — hint workload so GC uses throughput settings */
    gc_set_workload(gc_global(), GC_WORKLOAD_BENCH);

    clock_t t0 = clock();
    int tree_code = run_source_tree(source, filename);
    clock_t t1 = clock();
    int vm_code = run_source_vm(source, filename);
    clock_t t2 = clock();

    double tree_ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    double vm_ms   = 1000.0 * (double)(t2 - t1) / (double)CLOCKS_PER_SEC;
    printf("[prism] benchmark tree-walker: %.3f ms\n", tree_ms);
    printf("[prism] benchmark VM:          %.3f ms\n", vm_ms);
    if (vm_ms > 0.0)
        printf("[prism] benchmark tree/vm ratio: %.2fx\n", tree_ms / vm_ms);
    return tree_code || vm_code;
}

/* ------------------------------------------------------------------ REPL */

static void run_repl(void) {
    printf("Prism 0.2.0 - Interactive Mode (type 'exit' or Ctrl-D to quit)\n\n");

    /* REPL workload: prioritise responsiveness */
    gc_set_workload(gc_global(), GC_WORKLOAD_REPL);

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

/* ------------------------------------------------------------------ argument parsing */

static const char *configure_gc_from_args(int argc, char **argv) {
    GC *gc = gc_global();
    gc_configure_from_env(gc);

    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gc-stats") == 0) {
            gc->stats_on_shutdown = true;
        } else if (strcmp(argv[i], "--gc-log") == 0) {
            gc->log_enabled = true;
        } else if (strcmp(argv[i], "--gc-stress") == 0) {
            gc_set_policy(gc, GC_POLICY_STRESS);
            gc->stress_enabled    = true;
            gc->sweep_enabled     = true;
            gc->log_enabled       = true;
            gc->stats_on_shutdown = true;
        } else if (strcmp(argv[i], "--gc-sweep") == 0) {
            gc->sweep_enabled = true;
        } else if (strcmp(argv[i], "--mem-report") == 0) {
            gc->mem_report_enabled = true;
            gc->sweep_enabled      = true;  /* enable sweep so we have real stats */
        } else if (strncmp(argv[i], "--gc-policy=", 12) == 0) {
            const char *policy = argv[i] + 12;
            if (strcmp(policy, "throughput") == 0) {
                gc_set_policy(gc, GC_POLICY_THROUGHPUT);
            } else if (strcmp(policy, "low-latency") == 0) {
                gc_set_policy(gc, GC_POLICY_LOW_LATENCY);
            } else if (strcmp(policy, "debug") == 0) {
                gc_set_policy(gc, GC_POLICY_DEBUG);
                gc->log_enabled       = true;
                gc->stats_on_shutdown = true;
            } else if (strcmp(policy, "stress") == 0) {
                gc_set_policy(gc, GC_POLICY_STRESS);
                gc->stress_enabled    = true;
                gc->sweep_enabled     = true;
                gc->log_enabled       = true;
                gc->stats_on_shutdown = true;
            } else if (strcmp(policy, "adaptive") == 0) {
                gc_set_policy(gc, GC_POLICY_ADAPTIVE);
            } else {
                gc_set_policy(gc, GC_POLICY_BALANCED);
            }
        } else if (strcmp(argv[i], "--emit-bytecode") == 0) {
            opt_emit_bytecode = true;
        } else if (strcmp(argv[i], "--bench") == 0) {
            opt_bench = true;
        } else if (!path) {
            path = argv[i];
        }
    }

    return path;
}

/* ------------------------------------------------------------------ formatter */
#include "formatter.h"

/* ------------------------------------------------------------------ entry point */

int main(int argc, char **argv) {
    /* Initialise immortal singleton cache before anything else */
    value_immortals_init();

    /* Check for formatter flags before GC setup (no GC needed for format-only) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 || strcmp(argv[i], "--format-write") == 0) {
            bool write_back = strcmp(argv[i], "--format-write") == 0;
            if (i + 1 < argc) {
                char errbuf[512] = {0};
                int rc = prism_format_file(argv[i + 1], write_back ? 1 : 0, errbuf, sizeof(errbuf));
                if (errbuf[0]) fprintf(stderr, "Format error: %s\n", errbuf);
                value_immortals_free();
                gc_shutdown(gc_global());
                return rc;
            }
            fprintf(stderr, "Usage: prism %s <file.pm>\n", argv[i]);
            value_immortals_free();
            return 1;
        }
    }

    const char *path = configure_gc_from_args(argc, argv);

    if (argc == 1) {
        run_repl();
        value_immortals_free();
        gc_shutdown(gc_global());
        return 0;
    }

    if (path) {
        const char *dot = strrchr(path, '.');
        if (!dot || strcmp(dot, ".pm") != 0)
            fprintf(stderr, "Warning: '%s' does not have .pm extension\n", path);

        /* script workload: use adaptive policy */
        gc_set_workload(gc_global(), GC_WORKLOAD_SCRIPT);

        char *src = read_file(path);
        if (!src) {
            value_immortals_free();
            gc_shutdown(gc_global());
            return 1;
        }
        int code = opt_bench ? run_benchmark(src, path) : run_source_vm(src, path);
        free(src);
        value_immortals_free();
        gc_shutdown(gc_global());
        return code;
    }

    fprintf(stderr,
        "Usage: prism [options] [file.pm]\n"
        "Options:\n"
        "  --emit-bytecode          write compiled .pmc bytecode file\n"
        "  --bench                  compare tree-walker vs VM speed\n"
        "  --format <file>          print formatted source\n"
        "  --format-write <file>    format source file in place\n"
        "  --gc-stats               print GC statistics at shutdown\n"
        "  --gc-log                 log every GC event\n"
        "  --gc-sweep               enable mark-and-sweep collection\n"
        "  --gc-stress              stress-test the GC (implies sweep+log+stats)\n"
        "  --gc-policy=<name>       balanced|throughput|low-latency|debug|stress|adaptive\n"
        "  --mem-report             print full memory diagnostics at shutdown\n");
    value_immortals_free();
    gc_shutdown(gc_global());
    return 1;
}
