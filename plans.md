# Prism — Master Plans

All planned and in-progress work for the Prism language and runtime.
Items from `todo.md` and `tracker.md` are consolidated here.
New items added by the user are marked with `[NEW]`.

---

## Status Legend
- [ ] Not started
- [~] In progress
- [x] Complete

---

## 1. GUI — xgui Scroll Overhaul [NEW]

The current scroll implementation is minimal and feels poor in practice.
All of the following must be addressed together.

- [ ] **Fix fixed scroll step** — 60 px per wheel tick regardless of content size; should scale with content height
- [ ] **Scroll momentum / flick physics** — spinning the wheel fast should build momentum and coast to a stop (friction decay), not step discretely
- [ ] **Rubber-band overscroll** — overshooting the top/bottom should produce a natural elastic snap-back, not a hard clamp
- [ ] **Unified feel between wheel and scrollbar drag** — dragging the thumb currently teleports (bypasses spring); both input methods should go through the same physics
- [ ] **Expose scroll wheel events to Prism code** — `xgui_scroll_delta()` so user programs can react to scrolling
- [ ] **Expose key-press (not just key-hold) events** — `xgui_key_pressed(char)` for single-frame key-down detection

---

## 2. GUI — Missing Widgets [NEW]

Identified by comparing Prism xgui against Dear ImGui, Qt/PyQt6, tkinter, wxPython, and raygui.

- [ ] **`xgui_radio`** — radio button group (mutually exclusive options), like Qt `QRadioButton` / ImGui `RadioButton`
- [ ] **`xgui_menu_bar` / `xgui_menu` / `xgui_menu_item`** — top-of-window dropdown menus, like Qt `QMenuBar` / tkinter `Menu`
- [ ] **`xgui_popup` / `xgui_modal`** — blocking overlay dialog with optional title and buttons, like ImGui `BeginPopupModal` / Qt `QDialog`
- [ ] **`xgui_context_menu`** — right-click popup list, like ImGui `BeginPopupContextItem` / Qt `QMenu`
- [ ] **`xgui_table`** — multi-column data grid with optional headers and row selection, like ImGui `BeginTable` / Qt `QTableWidget`
- [ ] **`xgui_tree` / `xgui_tree_node`** — collapsible hierarchy nodes, like ImGui `TreeNode` / Qt `QTreeWidget`
- [ ] **`xgui_collapsing`** — collapsible section header (disclosure triangle), like ImGui `CollapsingHeader`
- [ ] **`xgui_color_picker`** — HSV/RGB color wheel + hex input, like ImGui `ColorPicker4` / Qt `QColorDialog`
- [ ] **`xgui_image`** — display an image file (PNG/PPM/BMP) from disk, like ImGui `Image` / Qt `QLabel` with pixmap
- [ ] **`xgui_spinbox`** — integer or float stepper with +/- buttons, like ImGui `InputInt` / Qt `QSpinBox`
- [ ] **`xgui_scroll_area`** — independently scrollable sub-panel within a window, like ImGui `BeginChild` / Qt `QScrollArea`
- [ ] **`xgui_drag_float` / `xgui_drag_int`** — click-drag to change value, like ImGui `DragFloat` / `DragInt`
- [ ] **`xgui_status_bar`** — fixed bottom-of-window text bar, like Qt `QStatusBar` / raygui `StatusBar`
- [ ] **`xgui_splitter`** — resizable divider between two panels (horizontal or vertical), like Qt `QSplitter`

---

## 3. GC / Memory — Correctness & Speed

### Correctness (near-term)
- [ ] **Iterative mark phase (explicit worklist)** [NEW] — replace recursive `gc_mark_value` with an explicit `Value *worklist[]` stack; deep nesting (arrays of arrays, closure chains) currently risks C stack overflow on the mark walk
- [ ] **Wire string interning at all call-sites** — `gc_intern_string` exists but compiler and interpreter mostly call `strdup` directly; dict keys and identifier strings should all go through `value_string_intern()`
- [ ] **Drop ref-counting, rely on pure tracing GC** — every `env_set` calls `value_retain`/`value_release` AND updates the GC list; remove ref-counting entirely and let generational mark-sweep be the sole ownership mechanism
- [ ] **Promote major GC trigger to threshold-based** — currently a major collection runs every 8 minors regardless of heap size; should trigger when old-gen exceeds a configurable byte limit
- [ ] **Expose young/old generation counters in `--gc-stats`**
- [ ] **Add cycle-focused tests/examples** — arrays, dicts, closures, and objects that form reference cycles
- [ ] **Wire `gc_push_root`/`gc_pop_root` per value-creating opcode in `vm_run`**
- [ ] **True stress-mode collection on every allocation** — `PRISM_GC_STRESS=1` calls `gc_collect_audit` inside `gc_track_value` on every allocation
- [ ] **LeakSanitizer alternative for ptrace-restricted environments**

### Performance (Rust-level speed)
- [ ] **Bump-pointer slab allocator for young generation** [NEW] — pre-allocate a large contiguous slab (e.g. 4 MB) and advance a pointer; allocation becomes a single pointer increment; slab reclaimed wholesale during minor GC
- [ ] **Compacting / moving collector** [NEW] — after a major collection, copy live old-gen objects into a fresh contiguous region; eliminates heap fragmentation and makes traversal cache-friendly
- [ ] **Escape analysis at compile time** — analyse which values never outlive the call; allocate those on the C stack or VM local frame
- [ ] **Stack allocation for short-lived VM values** — temporaries in `a + b * c` allocated on VM value stack as raw `Value` structs, not heap pointers
- [ ] **Region / arena allocation by lifetime** — group allocations sharing the same lifetime into a single arena; free entire arena on function return
- [ ] **Thread-local allocation buffers (TLABs)** — when multithreading is added, give each thread its own private slab
- [ ] **Finaliser queue** — for objects holding external resources (file handles, sockets, GPU buffers)

---

## 4. JIT / Native Code

- [ ] **Strengthen JIT for simple integer loops first** [NEW] — make hot loops with integer locals, arithmetic, comparisons, and back-edges compile reliably to native code so Prism can outperform Python on numeric loop benchmarks

---

## 5. VM Performance

- [ ] **Make VM/bytecode execution the default path for normal source runs** — avoid tree-walking AST execution for production-style runs; parse/compile once, then execute bytecode
- [ ] **NaN-boxing for scalar values** [NEW] — encode integers, floats, bools, and null directly into a 64-bit `uint64_t`; no `malloc` for scalars at all; trades code complexity for a **30–60%** speedup on arithmetic-heavy programs
- [ ] **Specialized integer bytecode instructions** — hot opcodes such as `ADD_INT`, `LT_INT`, `INC_LOCAL_INT`, and integer-specific conditional jumps to avoid repeated dynamic type checks
- [ ] **Compact call frames for faster function calls** — avoid allocating heavyweight environments for every call when locals can live in indexed VM slots
- [ ] **Merge slow/cached method dispatch paths** — unify `vm_dispatch_method_slow` and cached paths so every method call goes through `vm_resolve_method_id` → `vm_dispatch_method_cached`

---

## 6. Compiler

- [ ] **32-bit jump offsets** — `patch_jump` currently encodes target as signed `int16_t` (±32767 bytes); large functions will silently corrupt jump targets; widen to 32-bit
- [ ] **Dead code elimination after `return`** — stop emitting bytecode for statements following a `return` inside a function
- [ ] **Deduplicate constant pool entries** — verify `chunk_add_const_str` deduplicates; every use of the same variable name should add one entry, not one per reference

---

## 7. Parser

- [ ] **Panic-mode error recovery** [NEW] — `p->had_error = 1` currently stops parsing immediately, reporting only the first error; add synchronization: on a parse error, skip to the next statement boundary (newline or `;`) and resume, collecting multiple errors per run
- [ ] **Column tracking in tokens** [NEW] — the lexer records `line` but not column; storing byte offset of each token would enable `^`-style underline error messages pointing to the exact token, not just the line
- [ ] **Token arena / ring buffer** — `lexer_next` mallocs every `Token` and `strdup`s its value; the parser only ever needs `current` and `peek` live simultaneously; a fixed ring buffer of 2–4 reusable slots eliminates all per-token allocation
- [ ] **Expand lookahead buffer** — currently LL(2); a 3–4 token lookahead would cleanly resolve ambiguous grammar points (e.g. dict vs. set brace literal) without special-case peeking logic

---

## 8. Interpreter (tree-walker)

- [ ] **Hash map per `Env`** — replace flat parallel arrays with a small open-address hash map; `env_get`/`env_set`/`env_assign` go from O(n) strcmp scan to O(1)
- [ ] **Intern-keyed `Env`** — once interning is wired at all call-sites, `Env` keys become interned pointers and comparison becomes `==` instead of `strcmp`
- [ ] **Pre-parse f-string templates** — currently re-scans the raw `{...}` template string on every execution; pre-parse at parse time into a segment list stored in the AST node

---

## 9. Standard Library

- [ ] **Native `fs` module** [NEW] — `read_file`, `write_file`, path operations (lib version exists; native C implementation needed)
- [ ] **Native `os` module** [NEW] — env vars, exit, args (lib version exists; native C implementation needed)
- [ ] **Native `net` module** [NEW] — basic HTTP/TCP/UDP (lib version exists; native C implementation needed)
- [ ] **Native `json` module** — parse JSON into Prism dicts/arrays and serialize back

---

## 10. Quality / REPL

- [ ] **REPL history** [NEW] — arrow-key navigation through previous commands
- [ ] **REPL multiline input** [NEW] — continuation prompt (`...`) when a block is not yet closed; submit on blank line or matching brace
- [ ] **Better edge-case error messages** — division by zero, index out of bounds with value in message
- [ ] **`make release` packaging** — strip debug symbols, compile with `-O2`, package into `.tar.gz`

---

## 11. GUI Roadmap (legacy items from todo.md)

- [ ] `gui_image(path)` for displaying images (non-xgui path)
- [ ] `gui_layout_row()` / `gui_layout_col()` for layout control (non-xgui path)
- [ ] `gui_on_click(button_id, func)` event handler API (non-xgui path)
- [ ] Interactive HTML output with JS event wiring

---

## Build Constraint (from todo.md)

> Everything must be built from scratch in Prism itself or in C as part of the interpreter core.  
> Do **not** rely on or wrap external C modules, third-party libraries, or language bindings.  
> If a feature needs a standard library, implement it natively.
