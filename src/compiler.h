#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "chunk.h"

typedef struct Compiler Compiler;

/* Compile a parsed AST program into a Chunk.
 * Returns 1 on error, 0 on success. */
int compile(ASTNode *program, Chunk *out, char *error_buf, int error_buf_len);

/* Like compile() but emits OP_RETURN_NULL instead of OP_HALT at the end.
 * Used for imported modules so they return control to the importing VM frame. */
int compile_module(ASTNode *program, Chunk *out, char *error_buf, int error_buf_len);

#endif /* COMPILER_H */
