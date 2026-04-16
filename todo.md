# Prism TODO

## Completed
- [x] Tree-walking interpreter in C
- [x] All core data types (int, float, complex, string, bool, array, dict, set, tuple)
- [x] f-strings, verbatim strings, triple-quoted strings
- [x] Functions, closures, recursion
- [x] Control flow (if/elif/else, while, for, break, continue)
- [x] Undeclared variable assignment now raises a runtime error
- [x] GUI support via `gui_window`, `gui_label`, `gui_button`, `gui_input`, `gui_run`
- [x] AGC layer 1: allocation tracking, root audits, GC stats, policy flags
- [x] AGC layer 2 started: opt-in mark/sweep reclamation path via `--gc-sweep`
- [x] Prism bytecode instruction set and constant pool
- [x] AST-to-bytecode compiler for core statements and expressions
- [x] Stack-based VM for top-level execution and user-defined function call frames
- [x] VM benchmark mode via `--bench`
- [x] x86-64 assembly fast paths for VM integer arithmetic dispatch
- [x] Bytecode cache serializer via `--emit-bytecode` writing `.pmc` files

## Next Steps — AGC
- [ ] Add temporary root stack for expression/interpreter values
- [ ] Make mark/sweep safe as the default collection mode
- [ ] Replace most retain/release runtime paths with AGC ownership
- [ ] Add cycle-focused tests/examples for arrays, dicts, closures, and objects
- [ ] Add AST arena allocator for parser/compiler memory
- [ ] Add string interning and immortal cached values for `true`, `false`, `unknown`, `void`
- [ ] Add young/old generational heap after sweep mode is stable

## Next Steps — VM

- [x] Design the Prism bytecode instruction set (opcodes)
- [x] Write a bytecode compiler: walk the AST and emit instructions
- [x] Build the Prism VM: a stack-based virtual machine that executes bytecode
- [x] Add a constant pool for strings and numbers
- [x] Add call frames for function calls in the VM
- [x] Profile and benchmark: compare tree-walker vs VM speed
- [x] Add inline caching for faster property/method lookup
- [x] Explore assembly (x86-64) for hot paths in the VM (e.g., arithmetic dispatch)
- [x] Add a serializer: save compiled bytecode to `.pmc` files (prism bytecode cache)

## GUI Roadmap
- [ ] Add `gui_image(path)` for displaying images
- [ ] Add `gui_layout_row()` / `gui_layout_col()` for layout control
- [ ] Add event handlers: `gui_on_click(button_id, func)`
- [ ] Interactive HTML output with JS event wiring
- [ ] Optional: native GUI via GTK3 when running outside browser

## Standard Library
- [ ] `math` module: sin, cos, sqrt, log, pow, etc.
- [ ] `fs` module: read_file, write_file, path operations
- [ ] `os` module: env vars, exit, args
- [ ] `net` module: basic HTTP requests
- [ ] `json` module: parse and serialize JSON

## Quality
- [ ] Improve error messages with source location context
- [ ] Add stack traces for runtime errors
- [ ] REPL improvements: history, multiline input
- [ ] Built-in formatter
- [ ] Real dictionaries/hash maps
- [ ] Better error handling
- [ ] Excellent error handling

## Instruction for the next agent
- Read this `todo.md` first, then `CHANGELOG.md`.
- Check off items as they are completed.
- Add new work under the right section — do not mix Completed with Next Steps.
- Update `CHANGELOG.md` with every meaningful change.
- **Everything must be built from scratch in Prism itself or in C as part of the interpreter core. Do not rely on or wrap external C modules, third-party libraries, or language bindings. If a feature needs a standard library, implement it natively.**
