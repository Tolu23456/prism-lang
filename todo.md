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

## Next Steps — GC / Memory Management (near-term correctness + speed)
- [ ] **Make sweep the default collection mode**: remove the `--gc-sweep` opt-in requirement; `gc_collect_audit` should always sweep after marking. Most impactful correctness fix — in the current default mode cycles are never collected.
- [ ] **Doubly-linked object list → O(1) untrack**: add a `gc_prev` pointer to `Value` so `gc_untrack_value` can splice out a node without walking the entire list. Currently every `value_release` that hits zero refcount is an O(n) scan.
- [ ] **Iterative mark phase (explicit worklist)**: replace the recursive `gc_mark_value` with an explicit `Value *worklist[]` stack. Deep nesting (arrays of arrays, closure chains) currently risks a C stack overflow on the mark walk.
- [ ] **Wire string interning at all call-sites**: `gc_intern_string` exists but the compiler and interpreter mostly call `strdup` directly. Dict keys and identifier strings should all go through `value_string_intern()` — pointer equality then replaces `strcmp` everywhere.
- [ ] **Drop ref-counting, rely on pure tracing GC**: every `env_set` calls `value_retain`/`value_release` AND updates the GC list — double bookkeeping on every assignment. Long-term: remove ref-counting entirely and let the generational mark-sweep be the sole ownership mechanism.
- [ ] **Add temporary root stack for expression/interpreter values**: in-flight values created mid-expression have no GC root, so a collection triggered during evaluation can free them. A small push/pop root stack fixes this.
- [ ] **Promote major GC trigger to threshold-based**: currently a major collection runs every 8 minors regardless of heap size. Should trigger when old-gen exceeds a configurable byte limit instead.
- [ ] **Expose young/old generation counters in `--gc-stats`**: already tracked internally in `gc->young_count` / `gc->old_count`; just needs to be printed clearly.
- [ ] **Add cycle-focused tests/examples**: arrays, dicts, closures, and objects that form reference cycles — to verify sweep actually reclaims them once it is the default.
- [ ] **Add AST arena allocator**: replace per-node `calloc` in the parser with a bump-pointer arena. The entire AST is freed at once after compilation, so individual frees are wasteful and cache-unfriendly.

## Next Steps — GC / Memory Management (Rust-level performance)
These changes are prerequisites for reaching near-native speed. They go beyond correctness and into fundamental allocator and lifetime design.

- [ ] **Bump-pointer slab allocator for the young generation**: instead of calling `malloc` for every new `Value`, pre-allocate a large contiguous slab (e.g. 4 MB) and advance a pointer. Allocation becomes a single pointer increment — the same cost as stack allocation. The slab is reclaimed wholesale during a minor GC.
- [ ] **Compacting / moving collector**: after a major collection, copy live old-gen objects into a fresh contiguous region. Eliminates heap fragmentation and makes object traversal cache-friendly (live objects are packed together). Required before the slab allocator can be recycled efficiently.
- [ ] **Escape analysis at compile time**: analyse which values created inside a function never outlive the call (i.e. they do not escape into closures, arrays, or return values). Allocate those directly on the C stack or VM local frame — they never touch the GC at all. This is what Rust's ownership system does implicitly; adding it to Prism requires a dataflow pass in the compiler.
- [ ] **Stack allocation for short-lived VM values**: values proven not to escape a single bytecode instruction sequence (e.g. temporaries in `a + b * c`) can be allocated on the VM's value stack as raw `Value` structs rather than as `Value *` heap pointers. Zero allocation, zero GC pressure.
- [ ] **Precise rooting — eliminate conservative scanning entirely**: ensure every GC root is explicitly registered (VM stack, call frame locals, live Env chains). Conservative scanning (treating every word as a potential pointer) causes false retention and prevents moving collection. Prism is mostly precise already; audit and complete it.
- [ ] **Region / arena allocation by lifetime**: group allocations that share the same lifetime (e.g. all values created during one function call) into a single arena. Free the entire arena on function return instead of tracking each object individually. Used by ML Kit and some Rust arenas. Ideal for short-lived closures and list comprehensions.
- [ ] **Thread-local allocation buffers (TLABs)**: when multithreading is added, give each thread its own private slab for allocation to avoid lock contention on the shared heap. Each thread bumps its own pointer; only when the slab is exhausted does it request a new one from the global allocator.
- [ ] **Finaliser queue**: for objects that hold external resources (file handles, sockets, GPU buffers), add a finaliser queue processed at the end of each GC cycle instead of relying on ref-counting to trigger cleanup.

## Next Steps — JIT / Native Code (to reach Rust-level speed)
Interpretation overhead is the fundamental ceiling. These items describe the roadmap to cross it. They are large projects — each is weeks to months of work.

- [ ] **Hot-loop detection (profiling counter)**: instrument the VM's backward-jump instruction (`OP_JUMP` with negative offset) with a counter. When a loop body executes more than a threshold (e.g. 1000 iterations), mark it as a JIT candidate. This is the entry point for all trace-based JIT work.
- [ ] **Linear IR (intermediate representation)**: define a simple, flat instruction set between bytecode and machine code — operations like `IR_ADD_INT`, `IR_LOAD_LOCAL`, `IR_GUARD_INT`. The trace recorder fills this IR; the code generator consumes it. Keeping the IR simple (no SSA required initially) makes the first JIT tractable.
- [ ] **Trace recorder**: when a hot loop is detected, switch from executing bytecode to recording every operation into the IR. Stop recording at loop back-edge (forming a complete loop trace) or on an exit condition (branch, call, type mismatch).
- [ ] **Type guard emission**: JIT-compiled traces assume value types (e.g. "both operands are integers"). Emit a guard check before each typed operation; if the guard fails at runtime, exit the JIT trace and fall back to the interpreter. Guards are what make a dynamically-typed language safe to JIT.
- [ ] **x86-64 native code generator**: walk the recorded IR and emit raw x86-64 machine code into a mmap'd executable buffer. Start with the most common operations: integer arithmetic, local variable load/store, comparisons, and loop back-edges. Expected gain: **5–20× speedup** on hot numeric loops vs. the interpreter.
- [ ] **JIT code cache**: store compiled traces keyed by bytecode offset and entry type signature. On the next iteration of the same loop with the same types, execute the cached native code directly — no re-recording, no re-compiling.
- [ ] **ARM64 / AArch64 code generator**: second backend targeting Apple Silicon and ARM Linux (Raspberry Pi, Chromebooks). Shares the same IR and recorder; only the final code-gen step is platform-specific.
- [ ] **Inline hot function calls in JIT traces**: if a called function is small and always returns the same type, inline it into the current trace. Eliminates call frame setup and return overhead for short functions called in tight loops.
- [ ] **LLVM IR backend (AOT path)**: alternative to hand-written JIT — translate Prism bytecode or AST to LLVM IR and call the LLVM optimizer. LLVM handles register allocation, instruction selection, and all optimisation passes. Output is a native binary or shared library. Requires type annotations or full type inference to be effective since LLVM needs typed IR. This is how Crystal, Nim, and Zig achieve near-C speed.
- [ ] **C transpiler (simpler AOT path)**: translate Prism source to a self-contained `.c` file and compile it with GCC or Clang. Simpler than the LLVM backend; gets most of the same optimisations for free. Can be implemented entirely in Prism's existing compiler infrastructure by adding a C-emit backend alongside the bytecode backend.

## Next Steps — VM Performance
- [ ] **Computed-goto dispatch** (`goto *dispatch_table[opcode]`): replaces the central `switch` with per-opcode direct branch targets. Each opcode jumps straight to the next without bouncing through a shared switch point. Guard with `#ifdef __GNUC__` so it falls back to `switch` on MSVC. Expected gain: **10–25%** on most workloads.
- [ ] **Direct `uint8_t *ip` pointer**: replace the integer index `frame->ip` with a raw pointer into `chunk->code`. Eliminates a base-pointer add on every instruction fetch. Expected gain: **2–5%**.
- [ ] **Strip push/pop bounds checks in release builds**: wrap `vm_push`/`vm_pop` overflow/underflow checks in `#ifndef NDEBUG` so they compile away when building with `-DNDEBUG`. Expected gain: **2–5%** on stack-heavy code.
- [ ] **Local variable slots** (flat `Value *locals[]` per call frame): the compiler already knows which names are local to a function — emit `OP_LOAD_LOCAL n` / `OP_STORE_LOCAL n` that index a flat array instead of calling `env_get` (hash lookup + `strcmp` chain). Expected gain: **20–40%** for function-heavy code.
- [ ] **Merge slow/cached method dispatch paths**: `vm_dispatch_method_slow` runs a full second `strcmp` chain even after a cache miss. Unify both paths so every method call goes through `vm_resolve_method_id` → `vm_dispatch_method_cached`, with the slow path only for truly unknown methods on class instances.
- [ ] **NaN-boxing for scalar values**: encode integers, floats, bools, and null directly into a 64-bit `uint64_t` — no `malloc` for scalars at all. Trades code complexity for a **30–60%** speedup on arithmetic-heavy programs.

## Next Steps — Compiler
- [ ] **Variable classification pre-pass**: add a scope-analysis pass before code generation that classifies every variable as local, upvalue, or global. Emit `OP_LOAD_LOCAL n` / `OP_STORE_LOCAL n` for locals (flat array index) instead of `OP_LOAD_NAME` (string lookup). This is the prerequisite for VM local variable slots.
- [ ] **Constant folding**: if both operands of a binary expression are constant literals at compile time, evaluate the result once and emit a single `OP_PUSH_CONST`. Eliminates `PUSH 2`, `PUSH 3`, `ADD` → single `PUSH 5`.
- [ ] **32-bit jump offsets**: `patch_jump` encodes the target as a signed `int16_t` (±32767 bytes). Large functions will silently corrupt jump targets. Widen to 32-bit — either always, or via a `OP_JUMP_WIDE` variant.
- [ ] **Dead code elimination after `return`**: the compiler currently emits bytecode for statements that follow a `return` inside a function. Stop emitting once `OP_RETURN` / `OP_RETURN_NULL` has been emitted for the current block.
- [ ] **Deduplicate constant pool entries**: verify `chunk_add_const_str` deduplicates — every use of the same variable name (e.g. `"x"` in a loop body) should add one entry, not one per reference.

## Next Steps — Interpreter (tree-walker)
- [ ] **Hash map per `Env`**: replace the flat parallel arrays (`keys[]`, `values[]`) with a small open-address hash map. Variable lookup (`env_get`, `env_set`, `env_assign`) goes from O(n) strcmp scan to O(1). Expected gain: **30–50%** faster tree-walker on variable-heavy code.
- [ ] **Intern-keyed `Env`**: once string interning is wired at all call-sites, `Env` keys can be interned pointers — comparison becomes pointer equality (`==`) instead of `strcmp`, removing the last string comparison cost from variable lookup.
- [ ] **Pre-parse f-string templates**: the interpreter currently re-scans the raw `{...}` template string on every execution of that expression. Pre-parse the template at parse time into a `[literal, expr_node, literal, ...]` segment list stored in the AST node so runtime evaluation is a direct walk with no string scanning.

## Next Steps — Parser
- [ ] **Token arena / ring buffer**: `lexer_next` mallocs every `Token` and `strdup`s its value. The parser only ever needs `current` and `peek` live simultaneously — a fixed ring buffer of 2–4 reusable token slots eliminates all per-token allocation.
- [ ] **Panic-mode error recovery**: `p->had_error = 1` stops parsing immediately, so only the first error is ever reported. Add synchronization: on a parse error, skip forward to the next statement boundary (newline or `;`) and resume, collecting multiple errors per run.
- [ ] **Column tracking in tokens**: the lexer records `line` but not column. Storing the byte offset of each token would enable `^`-style underline error messages pointing to the exact token, not just the line.
- [ ] **Expand lookahead buffer**: the parser uses `current` + `peek` (LL(2)). A small 3–4 token lookahead buffer would let ambiguous grammar points (e.g. dict vs. set brace literal) be resolved cleanly without special-case peeking logic.

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
