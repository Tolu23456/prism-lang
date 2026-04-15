# Changelog

## Unreleased

### Added
- GUI support via built-in functions: `gui_window()`, `gui_label()`, `gui_button()`,
  `gui_input()`, and `gui_run()`. Calling `gui_run()` generates a styled `prism_gui.html`
  file that can be opened in any browser.
- Added `examples/gui_demo.pm` showing a simple GUI program.

### Fixed
- Assigning to an undeclared variable (e.g. `a = 5` without `let a`) now raises a
  clear runtime error: `variable 'a' is not declared; use 'let a = ...' to declare it`.
  Previously this silently created the variable.

### Changed
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
