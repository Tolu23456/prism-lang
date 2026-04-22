#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "interpreter.h"
#include "gc.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    gc_init(gc_global());
    FILE *f = fopen(argv[argc-1], "r");
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *src = malloc(sz + 1); fread(src, 1, sz, f); src[sz] = 0; fclose(f);
    Parser *p = parser_new(src); ASTNode *prog = parser_parse(p);
    Interpreter *interp = interpreter_new();
    interpreter_run(interp, prog);
    return 0;
}
