# Changelog

## v0.5.0 — Zero Memory Leaks + Parser Bug Fixes

### Fixed
- **GC: nested function chunk constants not marked (use-after-free)**: `gc_mark_value` for `VAL_FUNCTION` only visited the closure environment; string/int literals inside the function's bytecode chunk (`func.chunk`) were invisible to the mark phase and freed by the sweep while the chunk still held live `Value*` pointers. Added `gc_mark_chunk(gc, value->func.chunk)` to `gc_mark_value` in `src/gc.c`. Confirmed clean by ASan (`ASAN_OPTIONS=detect_leaks=0 ./prism-san` exits 0).
- **GC: `fread` unused-result warning**: `(void)fread(...)` casts do not suppress GCC's `__attribute__((warn_unused_result))`; replaced in `src/main.c`, `src/interpreter.c`, and `src/vm.c` with `{ size_t _nr = fread(...); (void)_nr; }`.
- **GC: shutdown sweep live-count inaccurate**: the "from N live objects" report read the already-decremented `gc->stats.live_objects` counter; now walks `gc->objects` to get the true pre-sweep count.
- **Parser: `elif` in multi-line blocks**: added `skip_newlines()` before the `while (check(p, TOKEN_ELIF))` loop and after each `elif` body in `parse_if()`. Previously `elif` was only recognised when on the same line as the preceding `}`.
- **Parser: `**` right-associativity**: changed `parse_power()` to recurse with `parse_power(p)` instead of `parse_unary(p)`, making exponentiation correctly right-associative (`2 ** 3 ** 2` = 512).
- **Value: set equality**: added a `VAL_SET` case to `value_equals()` in `src/value.c` that checks element membership in both directions; previously two sets with identical elements compared as unequal (pointer comparison fallthrough).

### Added
- **GC: sweep enabled by default**: `gc_init` sets `sweep_enabled = true`; `PRISM_GC_SWEEP=0` env var disables it. The `--gc-sweep` CLI flag is now a documented no-op.
- **GC: temporary root stack**: `GC_ROOT_STACK_MAX=4096` root stack in `PrismGC`; `gc_push_root`/`gc_pop_root` called in `gc_collect_minor`, `gc_collect_major`, `interpreter_run`, and `interpreter_free` so in-flight values are never invisible to the mark phase.
- **GC: shutdown sweep + leak report**: `gc_shutdown` runs a final `gc_collect_major` after all Prism execution finishes; objects surviving the sweep are logged as confirmed leaks and reclaimed by `gc_reclaim_remaining`. Enable with `PRISM_GC_STATS=1`.
- **Sanitizer build**: `make sanitize` target produces `prism-san` with `-fsanitize=address,undefined -fno-omit-frame-pointer`. Run with `ASAN_OPTIONS=detect_leaks=0 ./prism-san file.pr`.
- **Edge case test suite (`edgecase/`)**: 14 `.pr` files covering arrays, closures, control flow, dicts, error handling, functions, match, numbers, operators, sets, strings, tuples, types, and variables. All 14 pass on the current build.

## v0.4.0 — JIT Compiler + C Transpiler

### Added
- **JIT compiler (`src/jit.h`, `src/jit.c`)**: full trace-based JIT for hot integer loops, enabled with `--jit` or `--jit-verbose`.
  - **Hot-loop detection**: `OP_JUMP` backward jumps are counted per bytecode offset; once a loop back-edge fires ≥ 200 times it becomes a JIT candidate.
  - **Linear IR**: flat `JIRInstr` array with opcodes `JIR_LOAD_INT`, `JIR_STORE_INT`, `JIR_ADD_INT`, `JIR_SUB_INT`, `JIR_MUL_INT`, `JIR_DIV_INT`, `JIR_MOD_INT`, `JIR_CMP_*`, `JIR_GUARD_INT`, `JIR_LOOP_BACK`, `JIR_LOOP_EXIT`. 32-register file (slots 0–15 named vars, 16–31 temporaries).
  - **Trace recorder**: on hot entry the recorder walks `chunk->code` from loop header to back-edge, translating every bytecode instruction to IR and emitting `JIR_GUARD_INT` type checks for `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`.
  - **x86-64 native code generator**: emits raw machine code into `mmap(PROT_EXEC)` buffers. Handles integer load/store, all five arithmetic ops, six comparison ops, loop exit, and function prologue/epilogue. `JitFn = int (*)(long long *regs)` returns `JIT_EXIT_LOOP_DONE` (0) or `JIT_EXIT_GUARD_FAIL` (1).
  - **ARM64 / AArch64 code generator**: parallel backend for Apple Silicon and ARM Linux; same IR, separate emit pass using 32-bit fixed-width ARM64 instructions.
  - **JIT code cache**: `JIT.cache[JIT_CACHE_CAP=64]` hash table keyed on bytecode loop-header offset; compiled traces are reused on subsequent hot iterations without re-recording or re-compiling.
  - **`--jit-verbose`**: prints the IR of every compiled trace and a summary of traces compiled, executed, and guard exits at shutdown.
- **LLVM IR text emitter (`jit_emit_llvm_ir`)**: translates a `JitTrace`'s IR to valid LLVM IR (`.ll` format) without requiring the LLVM library at runtime. Exposed via `--emit-llvm <file.pm>` — runs the program once to compile hot traces, then dumps their LLVM IR to stdout.
- **C transpiler (`src/transpiler.h`, `src/transpiler.c`)**: translates a Prism AST to a self-contained, compilable C source file. Supports `int`/`float`/`bool`/`string` via a tagged `PV` union, all arithmetic and comparison operators, `if`/`elif`/`else`, `while`, `for`-range, function definitions, `print`, `len`, `str`, `int`, `float`, `abs`, `min`, `max`, `type`. Arrays, dicts, sets, and f-strings emit `/* TODO */` stubs. Exposed via `--emit-c <file.pm>` which writes the C to stdout.
- **VM integration (`src/vm.c`)**: `OP_JUMP` backward-jump handler calls `jit_on_backward_jump`; if a compiled trace is returned, calls `jit_execute`; on `JIT_EXIT_LOOP_DONE` advances `frame->ip` past the loop; on `JIT_EXIT_GUARD_FAIL` falls back transparently to normal interpretation.
- **`--jit` / `--jit-verbose` / `--emit-c` / `--emit-llvm` CLI flags** (`src/main.c`): parsed in `configure_gc_from_args`; `run_source_vm` enables JIT on the VM when `--jit` is set; `--emit-c` and `--emit-llvm` short-circuit normal execution.
- **Makefile**: `src/jit.c` and `src/transpiler.c` added to `SRCS`; `src/jit.h` and `src/transpiler.h` added to `HEADERS` so all objects rebuild correctly when headers change.

### Changed
- `vm_new` initialises `vm->jit = NULL` and `vm->jit_verbose = false`; JIT is opt-in via `--jit`.
- `vm_free` calls `jit_print_stats` (if verbose) then `jit_free` before tearing down the GC and environment.

## v0.3.0 — O(1) Core Operations

### Added
- **String interning (`gc_intern_cstr`)**: added `gc_intern_cstr(gc, str)` to `gc.h`/`gc.c` which stores immortal canonical copies of strings in a hash table; all identifier tokens and dict-key string literals are now interned at lex/parse time via `TOKEN_IDENT` handling in `lexer.c`.
- **Doubly-linked GC object list**: added `gc_prev` pointer to `Value` so that `gc_untrack_value` can splice out any node in O(1) rather than walking the list; both minor and major sweep passes maintain backward links correctly during collection.
- **Open-address hash map `Env`**: replaced `Env`'s flat parallel arrays with a pointer-keyed open-address hash map (`EnvSlot` in `interpreter.h`); `env_get`, `env_set`, and `env_assign` are all O(1) average-case; `env_rehash` doubles capacity at 75 % load.
- **O(1) method dispatch table in the VM**: added `s_method_table` (pointer-keyed open-address hash) to `vm.c`; `method_table_init` is called from `vm_new` and interns every built-in method name string; `vm_resolve_method_id` and slow-path dispatch now do a single hash lookup instead of a strcmp chain.

### Changed
- `value_equals` in `value.c`: immortal (interned) string comparison now uses `a == b` pointer equality — O(1) regardless of string length.
- `token_free` in `lexer.c` skips freeing the string value for tokens marked `interned = true`.
- `gc_mark_env` updated to iterate over hash map slots instead of the old flat arrays.

## v0.2.0 — Cross-platform build hardening

### Added
- **`--version` / `-v` flag**: prints the Prism version, build date and time
  (`__DATE__ __TIME__`), and whether X11 GUI support was compiled in.
- **`PRISM_VERSION` macro** defined in `main.c`; used in both the `--version`
  output and the REPL banner so the version string is a single source of truth.

### Fixed
- **X11 build failure** (`fatal error: X11/Xlib.h: No such file or directory`):
  `interpreter.c` and `vm.c` previously included `xgui.h` unconditionally at the
  top of the file. On systems without X11 dev headers, `pkg-config` returns
  nothing so `-DHAVE_X11` is never set, yet the bare `#include` still tried to
  pull in X11 types and caused a hard compile error. Both includes are now
  wrapped in `#ifdef HAVE_X11`.
- **`xgui_*` runtime "not defined" error** on no-X11 builds: when compiled
  without X11 the xgui builtin block was silently dropped, leaving all twelve
  `xgui_*` names unregistered in the interpreter and VM. A single stub
  (`bi_xgui_no_x11` / `vm_bi_xgui_no_x11`) is now registered for each name
  when `HAVE_X11` is absent; calling any xgui function prints a clear message:
  _"xgui: X11 support was not compiled in. Install libX11-dev / xorg-dev and
  recompile."_
- **`GC` type name collision with X11** (`conflicting types for 'GC'`): X11's
  `Xlib.h` defines `GC` as `struct _XGC *` (a Graphics Context handle). Prism's
  garbage collector also used `GC` as its top-level typedef. When both headers
  were present in the same translation unit the compiler reported a type clash.
  Prism's GC type has been renamed `PrismGC` throughout `gc.h`, `gc.c`,
  `interpreter.h`, `vm.h`, and `main.c`. All public GC function signatures now
  use `PrismGC *` and the internal singleton is `static PrismGC g_gc`.
- **`gui_native.h` / `gui_native.c` version mismatch**: an older copy of
  `gui_native.h` included `<X11/Xlib.h>` unconditionally and defined `GuiWindow`
  with an `X11 GC gc` field instead of the `sock_fd` / `port` pair used by the
  HTTP-server back-end. The canonical header (no X11 dependency, `sock_fd` +
  `port` fields) is now documented clearly so local copies can be updated.

### Changed
- REPL startup banner now reads the version from `PRISM_VERSION` instead of a
  hard-coded string.
- `Makefile` gains `PREFIX` / `BINDIR` variables (default `PREFIX=/usr/local`)
  and two new targets:
  - `make install` — builds `prism` then copies it to `$(BINDIR)/prism` with
    `install -m 755`. Supports `DESTDIR` for staged installs
    (e.g. `make install DESTDIR=/tmp/pkg`).
  - `make uninstall` — removes `$(BINDIR)/prism`.
  Both are listed in `.PHONY`.

---

## Unreleased

### Added
- **Allocation site tracking**: `--mem-report` now shows a "Top allocation sites"
  table at the end of the memory report.  For each Prism source line that
  triggered a tracked Value allocation, the GC records the file path, line
  number, total count, and dominant type.  The table is sorted by count and
  shows the top 10 hotspots (with a count of remaining sites).  Allocation
  sites are collected by a global `(file, line)` pair that the interpreter sets
  via `gc_set_alloc_site()` at the top of every `eval_node` dispatch and the VM
  sets at the top of every bytecode dispatch loop iteration.  Function call
  frames in the VM inherit the source filename from the calling frame so
  allocations inside user-defined functions are attributed correctly.
  Pre-execution allocations (constant pool values created at compile time)
  are labelled `<compile-time>` in the report.

- **Immortal singleton cache**: `null`, `true`, `false`, `unknown`, and all integers
  −5–255 are now pre-allocated once and reused as immortal `Value*` pointers.
  Immortal values bypass reference counting and are never touched by the GC,
  eliminating thousands of alloc/free cycles for the most common values.
- **String interning** (`gc_intern_string`, `value_string_intern`): an FNV-1a
  open-chaining hash table in the GC stores one immortal `Value*` per unique
  string (up to 128 bytes).  Identical strings share a pointer; duplicate
  allocations are counted as `intern_bytes_saved` in the stats.
- **Generational GC** (young/old split): every newly tracked `Value` starts as
  `GC_GEN_YOUNG`.  A minor GC collects only young objects — reachable ones are
  promoted to `GC_GEN_OLD`, unreachable ones are freed.  A major GC sweeps
  all generations.  A major GC fires automatically after 8 minor GCs
  (configurable via `gc->major_interval`).
- **Adaptive GC policy** (`GC_POLICY_ADAPTIVE`): after each collection the
  survival rate (survivors / total before sweep) is fed into an exponential
  moving average (α = 0.30).  The collection threshold is raised when objects
  tend to survive (long-lived program) and lowered when they die quickly (many
  temporaries), clamped between 64 KB and 64 MB.
- **Workload-aware GC hints** (`gc_set_workload`): the runtime now detects four
  workload modes — `script` (adaptive default), `repl` (low-latency, 256 KB
  threshold), `gui` (low-latency, 512 KB threshold), and `bench` (throughput,
  8 MB threshold) — and seeds the adaptive tuner accordingly.
- **Memory diagnostics** (`--mem-report`): prints a full human-readable memory
  report at shutdown covering per-type allocation breakdown with byte estimates,
  generational summary (young/old live counts, promotions), intern table stats
  (strings shared, bytes saved), adaptive metrics (survival EMA, current
  threshold), immortal singleton count, and qualitative health indicators
  (live/alloc ratio, array growth warnings).
- **Language-level `memory` module**: Prism programs can now call
  `memory.stats()`, `memory.collect()`, `memory.limit("512mb")`, and
  `memory.profile()` from both the VM and tree-walking interpreter paths.
  `memory.stats()` and `memory.profile()` return a dictionary of live runtime
  counters, `memory.collect()` forces a rooted major collection, and
  `memory.limit()` updates the adaptive collection threshold from human-readable
  byte strings.
- Added `--gc-policy=adaptive` to the CLI argument parser.
- `formatter.c` added to the Makefile so `--format` and `--format-write` now
  link correctly.

### Changed
- `value_retain` and `value_release` short-circuit immediately for immortal
  values (`gc_immortal = 1`), removing ref-count overhead for all common values.
- `gc_collect_sweep` is now an alias for `gc_collect_major` (kept for API
  compatibility); callers should prefer `gc_collect_minor` / `gc_collect_major`
  directly.
- `gc_print_stats` output now includes workload, minor/major collection counts,
  generational live counts, promotion count, survival EMA, and intern stats
  alongside the existing per-type breakdown.
- Updated `todo.md` to mark string interning, immortal singletons, generational
  GC, adaptive policy, workload hints, and `--mem-report` as completed.

---

## Previous — AGC scaffold + VM completion

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
