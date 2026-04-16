# Changelog

## Unreleased

### Added
- Completed the core Prism VM path: user-defined functions now carry compiled
  bytecode chunks and execute through VM call frames instead of falling back to
  the tree-walking interpreter.
- Added `--emit-bytecode` to serialize compiled chunks to `.pmc` bytecode cache
  files.
- Added `--bench` to compare tree-walker and VM execution time for a source file.
- Added guarded x86-64 inline assembly fast paths for VM integer `+`, `-`, `*`,
  `&`, `|`, and `^` dispatch, with portable C fallbacks for other architectures.
- Added VM inline caches for dictionary-backed attribute access and method-call
  resolution, caching per-bytecode-site dictionary slots and receiver-type method IDs.
- Added a built-in Prism source formatter exposed with `--format` and
  `--format-write`.
- Reworked dictionaries to use a native hash index for key lookup while preserving
  dense insertion-order entries for iteration, keys, values, and items.
- Added richer diagnostics with source-line context and VM runtime stack traces.
- Added PGUI, a Prism-native GTK3-style GUI toolkit exposed through `pgui_*`
  built-ins and implemented in the C core without linking GTK, external modules,
  third-party libraries, or language bindings.
- GUI support via built-in functions: `gui_window()`, `gui_label()`, `gui_button()`,
  `gui_input()`, and `gui_run()`. Calling `gui_run()` generates a styled `prism_gui.html`
  file that can be opened in any browser.
- Added `examples/gui_demo.pm` showing a simple GUI program.
- Added the first Prism AGC runtime scaffold with tracked `Value` allocations,
  root-audit hooks for interpreter environments, VM stacks, call frames, and bytecode
  chunks, per-type memory statistics, policy modes, and GC debug flags.
- Added shutdown-time AGC reclamation for remaining tracked runtime values after
  normal reference-count cleanup, giving Prism a safe first bridge toward cycle cleanup.
- Added GC runtime flags: `--gc-stats`, `--gc-log`, `--gc-stress`, and
  `--gc-sweep`, plus `--gc-policy=balanced|throughput|low-latency|debug|stress`.
- Began AGC layer 2 with an opt-in mark/sweep reclamation path that frees
  unmarked tracked runtime values at safe collection audit points.
- Updated `pipeline.md` and `todo.md` to reflect AGC layer 1 completion and
  layer 2 sweep work in progress.
- Added `pipeline.md` documenting the long-term Adaptive Memory Engine roadmap.

### Fixed
- Assigning to an undeclared variable (e.g. `a = 5` without `let a`) now raises a
  clear runtime error: `variable 'a' is not declared; use 'let a = ...' to declare it`.
  Previously this silently created the variable.

### Changed
- Updated README usage notes for VM bytecode output and benchmark mode.
- Updated `todo.md` to mark the completed core VM roadmap items.
- Updated `todo.md` to mark inline caching complete.
- Updated `todo.md` with detailed VM roadmap, GUI roadmap, and standard library plan.
- Updated `README.md` to list GUI support.

## 2026-04-15 — Initial release
- Wrote complete tree-walking interpreter in C (~4 100 lines).
- All core data types: int, float, complex, string, bool (tri-state), array, dict, set, tuple.
- f-strings, verbatim strings `@"..."`, triple-quoted strings.
- Functions with typed parameters, closures, recursion.
- Control flow: if/elif/else, while, for, break, continue.
- Slicing for strings, arrays, and tuples.
- Hex `0xFF` and binary `0b1010` literals.
- Full set operations: `|` union, `&` intersection, `-` difference, `^` symmetric difference.
- Built-ins: output, input, len, bool, int, float, str, set, type.
- Sublime Text syntax file `Prism.sublime-syntax`.
- Added `todo.md`, `CHANGELOG.md`, `replit.md`, `.gitattributes`.
