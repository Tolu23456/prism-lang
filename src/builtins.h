#ifndef BUILTINS_H
#define BUILTINS_H

#include "value.h"
#include "interpreter.h"  /* for Env */

/* Set a callback that builtin_error() and related builtins use to signal
 * catchable runtime errors.  If not set (default), errors call exit(1). */
void prism_set_builtin_throw(void (*cb)(const char *msg));

/* Register all standard-library built-in functions and math constants
 * into the given environment.  Called by both the tree-walking interpreter
 * and the bytecode VM so their built-in sets are identical. */
void prism_register_stdlib(Env *env);

#endif /* BUILTINS_H */
