# Prism — Master Plans

All planned and in-progress work for the Prism language and runtime.
Items from `todo.md` and `tracker.md` are consolidated here.
Items added from this session are marked with `[NEW]`.

---

## Status Legend
- [ ] Not started
- [~] In progress
- [x] Complete

---

## 1. GUI — xgui Scroll Overhaul [NEW]

The current scroll is a fixed 60 px step with no momentum, no rubber-band,
and inconsistent feel between mouse wheel and scrollbar drag.

- [ ] **Fix fixed scroll step** — scale step size proportionally to content height so short and long pages feel equally responsive
- [ ] **Scroll momentum / flick physics** — fast wheel spin builds velocity; friction decays it to a smooth stop instead of stepping discretely
- [ ] **Rubber-band overscroll** — overshooting top/bottom compresses elastically and springs back; not a hard clamp
- [ ] **Unified physics for wheel and scrollbar drag** — dragging the thumb currently teleports (bypasses spring); both input paths should flow through the same velocity/spring model
- [ ] **Expose scroll delta to Prism code** — `xgui_scroll_delta()` returns signed px scrolled this frame so user programs can react to scrolling
- [ ] **Expose key-press (not just key-hold)** — `xgui_key_pressed(char)` for single-frame key-down detection; currently only held-state is exposed

---

## 2. GUI — Missing Widgets [NEW]

Identified by comparing Prism xgui against Dear ImGui, Qt/PyQt6, tkinter, wxPython, and raygui.

- [ ] **`xgui_radio`** — radio button group (mutually exclusive options), like Qt `QRadioButton` / ImGui `RadioButton`
- [ ] **`xgui_menu_bar` / `xgui_menu` / `xgui_menu_item`** — top-of-window dropdown menus, like Qt `QMenuBar` / tkinter `Menu`
- [ ] **`xgui_popup` / `xgui_modal`** — blocking overlay dialog with optional title and action buttons, like ImGui `BeginPopupModal` / Qt `QDialog`
- [ ] **`xgui_context_menu`** — right-click popup list anchored to mouse position, like ImGui `BeginPopupContextItem` / Qt `QMenu`
- [ ] **`xgui_table`** — multi-column data grid with headers, alternating row color, and row selection, like ImGui `BeginTable` / Qt `QTableWidget`
- [ ] **`xgui_tree` / `xgui_tree_node`** — collapsible hierarchy with expand/collapse arrows, like ImGui `TreeNode` / Qt `QTreeWidget`
- [ ] **`xgui_collapsing`** — collapsible section header with disclosure triangle, like ImGui `CollapsingHeader`
- [ ] **`xgui_color_picker`** — HSV wheel + RGB sliders + hex input field, like ImGui `ColorPicker4` / Qt `QColorDialog`
- [ ] **`xgui_image`** — display an image from disk (PPM native; PNG/BMP with pure-C decoder), like ImGui `Image` / Qt `QLabel` with pixmap
- [ ] **`xgui_spinbox`** — numeric stepper with +/- buttons and direct keyboard entry, like ImGui `InputInt` / Qt `QSpinBox`
- [ ] **`xgui_scroll_area`** — independently scrollable sub-panel within the main window, like ImGui `BeginChild` / Qt `QScrollArea`
- [ ] **`xgui_drag_float` / `xgui_drag_int`** — click-drag horizontally to change a value, like ImGui `DragFloat` / `DragInt`
- [ ] **`xgui_status_bar`** — fixed bottom-of-window text strip, like Qt `QStatusBar` / raygui `StatusBar`
- [ ] **`xgui_splitter`** — resizable divider handle between two panels, like Qt `QSplitter`

---

## 3. PSS — Full Widget Coverage [NEW]

PSS currently has no style entries for 10 existing xgui widgets, meaning
they cannot be themed at all. Every widget that xgui draws must have a
corresponding `PssStyle` slot in `PssTheme` and a selector in `pss.c`.

### Existing xgui widgets with zero PSS coverage
- [ ] **`toggle` / `toggle:on` / `toggle:off`** — the pill-shaped switch (`xgui_toggle`); needs track color, thumb color, on-state accent color
- [ ] **`slider` / `slider:thumb` / `slider:track`** — the draggable range control (`xgui_slider`); needs track background, filled-portion color, thumb size and color
- [ ] **`select` / `select:open` / `select:item` / `select:item:hover`** — the dropdown selector (`xgui_select`); needs button style, open-panel background, item hover highlight
- [ ] **`chip` / `chip:hover` / `chip:removable`** — the tag chip (`xgui_chip`); needs background, border, remove-button color
- [ ] **`spinner`** — the loading indicator (`xgui_spinner`); needs arc color, track color, size constraints
- [ ] **`section`** — the section title divider (`xgui_section`); needs text style, line color, spacing
- [ ] **`group` / `group:title`** — the group box (`xgui_group_begin/end`); needs border color, radius, title text style
- [ ] **`grid`** — the grid layout container (`xgui_grid_begin/end`); needs gap size, background
- [ ] **`row`** — the row layout container (`xgui_row_begin/end`); needs gap size, alignment
- [ ] **`toast` / `toast:success` / `toast:warning` / `toast:error`** — the notification overlay (`xgui_show_toast`); needs background, text color, border-radius, shadow

### New planned widgets that also need PSS entries
- [ ] **`radio` / `radio:checked` / `radio:disabled`** — for `xgui_radio`
- [ ] **`menu-bar` / `menu-bar:item` / `menu-bar:item:hover` / `menu-bar:item:open`** — for `xgui_menu_bar`
- [ ] **`modal` / `modal:title` / `modal:body` / `modal:footer`** — for `xgui_popup` / `xgui_modal`
- [ ] **`table` / `table:header` / `table:header-cell` / `table:row` / `table:row-alt` / `table:row:hover` / `table:row:selected` / `table:cell`** — for `xgui_table`
- [ ] **`tree` / `tree:node` / `tree:node:expanded` / `tree:node:selected` / `tree:node:leaf`** — for `xgui_tree`
- [ ] **`collapsing` / `collapsing:open`** — for `xgui_collapsing`
- [ ] **`color-picker`** — for `xgui_color_picker`
- [ ] **`image`** — border, border-radius, shadow for `xgui_image`
- [ ] **`spinbox` / `spinbox:button` / `spinbox:button:hover`** — for `xgui_spinbox`
- [ ] **`scroll-area`** — background and border for `xgui_scroll_area`
- [ ] **`drag-control` / `drag-control:hover`** — for `xgui_drag_float` / `xgui_drag_int`
- [ ] **`status-bar`** — for `xgui_status_bar`
- [ ] **`splitter` / `splitter:handle` / `splitter:handle:hover`** — for `xgui_splitter`
- [ ] **`icon-button` / `icon-button:hover` / `icon-button:active`** — for `xgui_icon_button` (currently shares button style)

---

## 4. PSS — Engine Improvements [NEW]

The PSS parser/engine itself is missing many capabilities that would make
stylesheets significantly more powerful and maintainable.

### Color system
- [ ] **HSL / HSV color functions** — `hsl(hue, sat%, light%)` and `hsv(hue, sat%, val%)` in addition to `rgb()` and hex; essential for building color-coherent themes
- [ ] **Named colors** — `color: red`, `color: royalblue`, etc.; a built-in table of ~140 CSS named colors
- [ ] **Alpha channel support** — `#RRGGBBAA` 8-digit hex and `rgba(r, g, b, a)` so widget backgrounds and shadows can be semi-transparent
- [ ] **`color-mix()` function** — blend two color values, e.g. `color-mix(var(--blue), white, 20%)` for hover tints

### Layout properties
- [ ] **`gap` property** — spacing between children in `row` and `grid` layouts; currently gap is hardcoded in C
- [ ] **`align-items` / `justify-content`** — flex-like alignment for row and grid containers
- [ ] **`width: N%` / `height: N%`** — relative sizing so widgets can fill a fraction of available space
- [ ] **`overflow: hidden | scroll | visible`** — per-widget clip control; needed for scroll areas

### Visual effects
- [ ] **Multi-layer shadows** — `shadow: 0 2 4 #000, 0 8 24 #00000055`; `PssStyle` currently holds only one shadow layer
- [ ] **`background: linear-gradient(dir, color1, color2)`** — gradient fills for buttons, cards, headers
- [ ] **`background: radial-gradient(...)`** — radial fills for color pickers and decorative widgets
- [ ] **`transition: property duration easing`** — declare which style changes animate and how fast; drives the xgui animation system

### Stylesheet organization
- [ ] **`@import "other.pss"`** — include another PSS file so themes can be split into base + override files
- [ ] **`@theme dark { ... }` / `@theme light { ... }`** — named preset blocks; `xgui_set_dark()` switches active theme without reloading the file
- [ ] **`@media width > 800 { ... }`** — conditional rules based on window size for responsive layouts
- [ ] **Property inheritance** — `font-size`, `color`, and `font` on `window` propagate to children that do not set their own value
- [ ] **`!important` flag** — `color: red !important` overrides inherited and lower-priority rules
- [ ] **Selector nesting** — write `button { ... &:hover { ... } }` instead of repeating the selector name

### Value expressions
- [ ] **`calc()` arithmetic** — `padding: calc(var(--base-pad) + 4)` for derived values without duplication
- [ ] **`min()` / `max()` / `clamp()`** — `min-width: clamp(120, 20%, 300)` for adaptive sizing
- [ ] **Relative units `em` and `%`** — font-relative and parent-relative sizing alongside the existing absolute-pixel integers

---

## 5. Error Handling — Full Overhaul [NEW]

Currently `runtime_error` stores only `"line N: message"` and the parser
stops after the first error. Both need to be rebuilt properly.

### Parser errors
- [ ] **Multi-error collection** — instead of `p->had_error = 1` stopping everything, synchronize to the next statement boundary and continue; report all errors in one pass
- [ ] **Column numbers in all error messages** — lexer currently tracks only `line`; add `col` (byte offset from line start); format: `file:line:col: error: message`
- [ ] **Caret underline display** — print the source line with a `^` or `~~~` pointing at the bad token, like Rust/Clang do
- [ ] **Error codes** — assign a stable code to each error type (`E001` unexpected token, `E002` undefined variable, etc.) for documentation and tooling
- [ ] **"Did you mean?" suggestions** — on undefined identifier, check interned name table for close Levenshtein matches and suggest the nearest one
- [ ] **Warning system** — introduce `WARN_*` codes for non-fatal issues: unused variables, shadowed names, unreachable code after `return`, implicit type coercion in comparisons

### Runtime errors
- [ ] **Full stack trace in tree-walker** — the interpreter currently provides no call stack on error; unwind and print each call frame with function name and line number (VM already does this; parity needed)
- [ ] **Typed runtime errors** — instead of a flat string, distinguish `TypeError`, `IndexError`, `KeyError`, `ValueError`, `DivisionByZeroError`, `IOError` so `try/catch` can catch specific types
- [ ] **Better type mismatch messages** — `TypeError: expected int, got string "hello"` instead of just `"type mismatch"`
- [ ] **Better index-out-of-bounds messages** — `IndexError: index 7 out of range for array of length 3`
- [ ] **Better key-not-found messages** — `KeyError: "username" not found in dict`
- [ ] **Division by zero location** — currently reported as line 0; must capture the exact expression line
- [ ] **Null dereference location** — `NullError: attempted to call method on null at line N col C`

---

## 6. Language — Missing Features [NEW]

Features found in comparable languages (Python, Ruby, Swift, Kotlin) that
would significantly improve Prism's expressiveness.

### Control flow
- [ ] **`match` / `case` pattern matching** — structural matching on value, type, and guard: `match x { case 1: ... case str if str != "": ... case _: ... }`
- [ ] **`match` with guard clauses** — `case x if x > 0 and x < 100:` combined condition
- [ ] **`try / catch / finally`** — `finally` block that always runs after `try`, even on error; currently only `try/catch` exists
- [ ] **Typed `catch`** — `catch TypeError as e` to catch only a specific error type

### Destructuring
- [ ] **Array destructuring** — `let [a, b, c] = arr` and `let [head, ...tail] = arr`
- [ ] **Dict destructuring** — `let {x, y} = point` and `let {name: alias} = obj`
- [ ] **Swap shorthand** — `a, b = b, a` without a temporary variable

### Operators & expressions
- [ ] **Null coalescing `??`** — `value ?? default` returns `default` when `value` is null; avoids verbose `if` checks
- [ ] **Optional chaining `?.`** — `obj?.field`, `arr?[i]`, `fn?()` — short-circuits to null if left side is null without throwing
- [ ] **`**` exponentiation operator** — `2 ** 10` instead of `math.pow(2, 10)`
- [ ] **Bitwise shift operators `<<` / `>>`** — left and right shift; currently absent
- [ ] **Integer floor division `//`** — explicit integer-truncating divide (complement to existing `%`)
- [ ] **`in` for string substring check** — `"ello" in "hello"` returning bool (already works for arrays/dicts; extend to strings)

### Functions
- [ ] **Default parameter values** — `fn greet(name, greeting="Hello") { ... }`
- [ ] **Named / keyword arguments** — `greet(name: "Alice", greeting: "Hi")` at call site
- [ ] **Multiple return values** — `fn minmax(arr) { return (min, max) }` returning a tuple that unpacks naturally
- [ ] **Generator functions / `yield`** — `fn count_up(n) { for i in 0..n { yield i } }` producing lazy sequences

### Types & classes
- [ ] **`enum` with associated values** — `enum Color { Red, Green, Blue }` and `enum Shape { Circle(radius), Rect(w, h) }` for tagged unions
- [ ] **Interfaces / protocols** — declare a set of method signatures; classes that implement all of them satisfy the interface; enables structural polymorphism
- [ ] **Operator overloading** — `__add__`, `__sub__`, `__eq__`, `__lt__`, `__str__`, `__repr__` on class instances
- [ ] **`__repr__` / `__str__`** — custom `output()` and string interpolation rendering for class instances; currently prints `<object>`
- [ ] **`isinstance` / `is` type checking improvements** — `obj is Circle` checks both class name and interface satisfaction

### Comprehensions
- [ ] **List comprehensions** — `[x * 2 for x in arr if x > 0]`
- [ ] **Dict comprehensions** — `{k: v * 2 for k, v in d.items()}`
- [ ] **Set comprehensions** — `{x % 5 for x in range(20)}`
- [ ] **Generator expressions** — lazy `(x * 2 for x in large_arr)` without building a list

### Imports
- [ ] **`import X as Y` aliasing** — `import math as m` then use `m.sin(x)`
- [ ] **Selective imports** — `from math import sin, cos, pi` binds names directly into local scope
- [ ] **Relative imports** — `from .utils import helper` for multi-file projects

### Resource management
- [ ] **`with` statement** — `with open("file.txt") as f { ... }` auto-calls `f.close()` on exit even if an error occurs; requires `__enter__` / `__exit__` protocol on objects

---

## 7. GC / Memory — Correctness & Speed

### Correctness (near-term)
- [ ] **Iterative mark phase (explicit worklist)** [NEW] — replace recursive `gc_mark_value` with an explicit `Value *worklist[]` stack; deep nesting currently risks C stack overflow
- [ ] **Wire string interning at all call-sites** — `gc_intern_string` exists but compiler/interpreter mostly call `strdup`; all dict keys and identifiers should go through `value_string_intern()`
- [ ] **Drop ref-counting, rely on pure tracing GC** — `env_set` does both ref-counting and GC list updates; remove ref-counting entirely
- [ ] **Promote major GC trigger to threshold-based** — major collection currently runs every 8 minors regardless of heap size
- [ ] **Expose young/old generation counters in `--gc-stats`**
- [ ] **Add cycle-focused tests/examples**
- [ ] **Wire `gc_push_root`/`gc_pop_root` per value-creating opcode in `vm_run`**
- [ ] **True stress-mode collection** — `PRISM_GC_STRESS=1` calls `gc_collect_audit` on every allocation
- [ ] **LeakSanitizer alternative for ptrace-restricted environments**

### Performance (Rust-level speed)
- [ ] **Bump-pointer slab allocator for young generation** [NEW] — 4 MB contiguous slab; allocation = single pointer increment; reclaimed wholesale on minor GC
- [ ] **Compacting / moving collector** [NEW] — copy live old-gen objects into fresh contiguous region after major collection; eliminates fragmentation
- [ ] **Escape analysis at compile time** — values that never outlive a call allocated on C stack or VM local frame
- [ ] **Stack allocation for short-lived VM values** — temporaries in `a + b * c` as raw `Value` structs, not heap pointers
- [ ] **Region / arena allocation by lifetime** — group same-lifetime allocations; free entire arena on function return
- [ ] **Thread-local allocation buffers (TLABs)** — per-thread private slabs for future multithreading
- [ ] **Finaliser queue** — objects holding external resources processed at end of each GC cycle

---

## 8. JIT / Native Code

- [ ] **Strengthen JIT for simple integer loops** [NEW] — integer locals, arithmetic, comparisons, and back-edges must compile reliably to native code; Prism should beat Python on numeric benchmarks
- [ ] **JIT support for string operations** — intern-pointer equality check compiles to a single `cmp` instruction in JIT traces
- [ ] **Deoptimization / OSR exits** — graceful fallback when a JIT guard fails mid-trace; currently exits to interpreter from the top of the loop only
- [ ] **Profile-guided inlining** — track per-call-site type frequency; inline the dominant type specialization

---

## 9. VM Performance

- [ ] **Make VM/bytecode the default path** — avoid tree-walking for production runs; parse/compile once, execute bytecode
- [ ] **NaN-boxing for scalar values** [NEW] — encode int, float, bool, null into a 64-bit `uint64_t`; 30–60% speedup on arithmetic-heavy programs
- [ ] **Specialized integer bytecode instructions** — `ADD_INT`, `LT_INT`, `INC_LOCAL_INT` avoid repeated dynamic type checks
- [ ] **Compact call frames** — avoid heavyweight `Env` for every call when locals live in indexed VM slots
- [ ] **Merge slow/cached method dispatch paths** — unify both paths through `vm_resolve_method_id` → `vm_dispatch_method_cached`

---

## 10. Compiler

- [ ] **32-bit jump offsets** — `patch_jump` uses signed `int16_t`; large functions silently corrupt jump targets; widen to 32-bit
- [ ] **Dead code elimination after `return`** — stop emitting bytecode after `OP_RETURN` / `OP_RETURN_NULL`
- [ ] **Deduplicate constant pool entries** — every use of the same variable name should add one pool entry, not one per reference
- [ ] **Inlining small functions** — functions ≤ 8 opcodes with no closures inlined at call site; eliminates frame setup overhead

---

## 11. Parser

- [ ] **Panic-mode error recovery** [NEW] — skip to next statement boundary on error; collect all errors in one pass
- [ ] **Column tracking in tokens** [NEW] — add `col` to each token; enables `file:line:col:` format and caret underlines
- [ ] **Token arena / ring buffer** — eliminate per-token `malloc`; use a 2–4 slot reusable ring buffer
- [ ] **Expand lookahead to 3–4 tokens** — resolve dict-vs-set brace ambiguity and similar grammar points cleanly

---

## 12. Interpreter (tree-walker)

- [ ] **Hash map per `Env`** — O(n) strcmp scan → O(1); 30–50% speedup on variable-heavy code
- [ ] **Intern-keyed `Env`** — keys become interned pointers; comparison becomes `==` instead of `strcmp`
- [ ] **Pre-parse f-string templates** — stop re-scanning `{...}` template on every execution; parse into a segment list at parse time

---

## 13. Standard Library

- [ ] **Native `fs` module** [NEW] — `read_file`, `write_file`, path ops in C (lib version exists)
- [ ] **Native `os` module** [NEW] — env vars, exit, args in C (lib version exists)
- [ ] **Native `net` module** [NEW] — HTTP/TCP/UDP in C (lib version exists)
- [ ] **Native `json` module** — parse JSON into Prism dicts/arrays and serialize back
- [ ] **`datetime` native improvements** — timezone-aware operations, `strptime` parsing
- [ ] **`path` module** — `join`, `dirname`, `basename`, `exists`, `is_file`, `is_dir`, `glob` pattern matching
- [ ] **`thread` module** — basic threading primitives (`spawn`, `join`, `mutex`, `channel`) once TLABs land
- [ ] **`sqlite` module** — pure-C SQLite3 amalgamation bundled and exposed as a stdlib module

---

## 14. Quality / REPL

- [ ] **REPL history** [NEW] — arrow-key navigation through previous commands (readline-style, implemented in C without libreadline)
- [ ] **REPL multiline input** [NEW] — continuation prompt `...` when block is unclosed; submit on blank line or matched brace
- [ ] **Better edge-case error messages** — division by zero, index out of bounds, null dereference (see Section 5)
- [ ] **`make release` packaging** — strip debug symbols, `-O2`, package into `.tar.gz`
- [ ] **Language server protocol (LSP) stub** — `prism --lsp` speaks JSON-RPC; provides go-to-definition and hover types for editor integration
- [ ] **`prism fmt` formatter improvements** — align dict literals, normalize trailing commas, enforce consistent brace style

---

## 15. GUI Roadmap (legacy items from todo.md)

- [ ] `gui_image(path)` for displaying images (non-xgui path)
- [ ] `gui_layout_row()` / `gui_layout_col()` for layout control (non-xgui path)
- [ ] `gui_on_click(button_id, func)` event handler API (non-xgui path)
- [ ] Interactive HTML output with JS event wiring

---

## Build Constraint (from todo.md)

> Everything must be built from scratch in Prism itself or in C as part of the interpreter core.  
> Do **not** rely on or wrap external C modules, third-party libraries, or language bindings.  
> If a feature needs a standard library, implement it natively.
