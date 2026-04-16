#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "chunk.h"

typedef struct Compiler Compiler;

/* Compile a parsed AST program into a Chunk.
 * Returns 1 on error, 0 on success. */
int compile(ASTNode *program, Chunk *out, char *error_buf, int error_buf_len);

#endif /* COMPILER_H */
