#ifndef TRANSPILER_H
#define TRANSPILER_H

#include <stdio.h>
#include "ast.h"

/*
 * C transpiler: translate a Prism AST to a standalone C source file.
 *
 * Usage:
 *   transpile_to_c(program_ast, "my_program.pr", stdout);
 *
 * The generated C file can be compiled with:
 *   cc -O2 -lm output.c -o output
 *
 * Supported: ints, floats, booleans, basic arithmetic, if/while/for loops,
 *            functions, output(), input(), range(), basic math builtins.
 * Unsupported (emitted as runtime-error stubs): arrays, dicts, sets, tuples,
 *              string interpolation, closures, classes, import.
 */
void transpile_to_c(ASTNode *program, const char *source_name, FILE *out);

#endif /* TRANSPILER_H */
