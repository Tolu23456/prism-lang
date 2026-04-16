# Prism TODO

## Completed
- [x] Tree-walking interpreter in C
- [x] All core data types (int, float, complex, string, bool, array, dict, set, tuple)
- [x] f-strings, verbatim strings, triple-quoted strings
- [x] Functions, closures, recursion
- [x] Control flow (if/elif/else, while, for, break, continue)
- [x] Undeclared variable assignment now raises a runtime error
- [x] GUI support via `gui_window`, `gui_label`, `gui_button`, `gui_input`, `gui_run`
- [x] Prism-native GTK3-style PGUI toolkit without external GTK, third-party modules, or language bindings
- [x] AGC layer 1: allocation tracking, root audits, GC stats, policy flags
- [x] AGC layer 2 started: opt-in mark/sweep reclamation path via `--gc-sweep`
- [x] Prism bytecode instruction set and constant pool
- [x] AST-to-bytecode compiler for core statements and expressions
- [x] Stack-based VM for top-level execution and user-defined function call frames
- [x] VM benchmark mode via `--bench`
- [x] x86-64 assembly fast paths for VM integer arithmetic dispatch
- [x] Bytecode cache serializer via `--emit-bytecode` writing `.pmc` files
- [x] Immortal singleton cache: `null`, `true`, `false`, `unknown`, and integers −5–255 bypass GC and ref-counting entirely
- [x] String interning via FNV-1a hash table — identical short strings share one immortal `Value*`
- [x] Generational GC: young/old generation split, minor (young-only) and major (full) collections
- [x] Promotion: reachable young objects automatically promoted to old generation after surviving a minor GC
- [x] Adaptive GC policy (`GC_POLICY_ADAPTIVE`): survival-rate EMA adjusts collection threshold automatically
- [x] Workload-aware GC hints: REPL, script, GUI, and bench modes tune initial thresholds and major-GC interval
- [x] Memory diagnostics via `--mem-report`: per-type breakdown, generational summary, intern stats, adaptive metrics, health indicators
- [x] Allocation site tracking: `--mem-report` prints top-10 allocation hotspots with source file, line number, count, and dominant type — works for both the VM and tree-walking interpreter paths
- [x] Inline caching for method/property lookup in the VM (`CALL_METHOD` site caches `VmMethodId`)
- [x] Richer error diagnostics: source-line context and VM runtime stack traces
- [x] Built-in source formatter (`--format`, `--format-write`)
- [x] Dictionaries reworked to use a native hash index with insertion-order entry array for iteration
- [x] `--version` / `-v` flag: prints version, build date, and X11 support status
- [x] `make install` / `make uninstall` targets with `PREFIX`/`DESTDIR` support
- [x] Cross-platform X11 build hardening: `#ifdef HAVE_X11` guards, `PrismGC` rename to avoid X11 `GC` clash, graceful no-X11 stubs for all `xgui_*` builtins

## Next Steps — AGC
- [ ] Add temporary root stack for expression/interpreter values
- [ ] Make sweep the default collection mode (remove opt-in requirement)
- [ ] Replace most retain/release runtime paths with AGC ownership
- [ ] Add cycle-focused tests/examples for arrays, dicts, closures, and objects
- [ ] Add AST arena allocator for parser/compiler memory
- [ ] Add actual string interning call-sites: use `value_string_intern()` for dict keys and identifier strings
- [ ] Add young/old generation counters to GC stats printed by `--gc-stats` (already tracked internally; expose clearly)
- [ ] Promote major GC trigger from "every 8 minors" to threshold-based (when old-gen exceeds a size limit)

## Next Steps — VM Performance
- [ ] **Computed-goto dispatch** (`goto *dispatch_table[opcode]`): replaces the central `switch` with per-opcode direct branch targets. Each opcode jumps straight to the next without bouncing through a shared switch point. Guard with `#ifdef __GNUC__` so it falls back to `switch` on MSVC. Expected gain: **10–25%** on most workloads.
- [ ] **Direct `uint8_t *ip` pointer**: replace the integer index `frame->ip` with a raw pointer into `chunk->code`. Eliminates a base-pointer add on every instruction fetch. Expected gain: **2–5%**.
- [ ] **Strip push/pop bounds checks in release builds**: wrap `vm_push`/`vm_pop` overflow/underflow checks in `#ifndef NDEBUG` so they compile away when building with `-DNDEBUG`. Expected gain: **2–5%** on stack-heavy code.
- [ ] **Local variable slots** (flat `Value *locals[]` per call frame): the compiler already knows which names are local to a function — emit `OP_LOAD_LOCAL n` / `OP_STORE_LOCAL n` that index a flat array instead of calling `env_get` (hash lookup + `strcmp` chain). Expected gain: **20–40%** for function-heavy code.
- [ ] **Merge slow/cached method dispatch paths**: `vm_dispatch_method_slow` runs a full second `strcmp` chain even after a cache miss. Unify both paths so every method call goes through `vm_resolve_method_id` → `vm_dispatch_method_cached`, with the slow path only for truly unknown methods on class instances.
- [ ] **NaN-boxing for scalar values**: encode integers, floats, bools, and null directly into a 64-bit `uint64_t` — no `malloc` for scalars at all. Trades code complexity for a **30–60%** speedup on arithmetic-heavy programs.

## GUI Roadmap
- [ ] Add `gui_image(path)` for displaying images
- [ ] Add `gui_layout_row()` / `gui_layout_col()` for layout control
- [ ] Add event handlers: `gui_on_click(button_id, func)`
- [ ] Interactive HTML output with JS event wiring
- [x] Build PGUI as Prism's native GTK3-style GUI layer without external GTK3 or bindings

## Standard Library
- [ ] `math` module: sin, cos, sqrt, log, pow, etc.
- [ ] `fs` module: read_file, write_file, path operations
- [ ] `os` module: env vars, exit, args
- [ ] `net` module: basic HTTP requests
- [ ] Native `json` module: parse JSON into Prism dictionaries/arrays and serialize Prism dictionaries/arrays back to JSON

## Quality
- [x] Improve error messages with source location context
- [x] Add stack traces for runtime errors
- [ ] REPL improvements: history, multiline input
- [ ] Better error handling for edge cases (division by zero, index out of bounds messages)
- [ ] `make release` target: strip debug symbols, compile with `-O2`, package into `.tar.gz`

## Instruction for the next agent
- Read this `todo.md` first, then `CHANGELOG.md`.
- Check off items as they are completed.
- Add new work under the right section — do not mix Completed with Next Steps.
- Update `CHANGELOG.md` with every meaningful change.
- **Everything must be built from scratch in Prism itself or in C as part of the interpreter core. Do not rely on or wrap external C modules, third-party libraries, or language bindings. If a feature needs a standard library, implement it natively.**
