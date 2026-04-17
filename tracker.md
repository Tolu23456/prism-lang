# Prism Improvement Tracker

## Status Legend
- [ ] Not started
- [~] In progress
- [x] Complete

---

## Phase 1 — VM & Compiler Speed (500x target)
- [x] Computed-goto dispatch (`goto *dispatch_table[opcode]`) — 10–25% gain
- [x] Direct `uint8_t *ip` pointer in call frames — 2–5% gain
- [x] Strip push/pop bounds checks in release builds (`#ifndef NDEBUG`)
- [x] Local variable slots: OP_LOAD_LOCAL / OP_STORE_LOCAL (flat array)
- [x] Variable classification pre-pass in compiler
- [x] Constant folding (literal arithmetic at compile time)
- [x] Dead code elimination after `return`
- [x] Specialized integer opcodes: OP_ADD_INT, OP_SUB_INT, OP_MUL_INT, OP_LT_INT, OP_GT_INT, OP_EQ_INT, OP_INC_LOCAL
- [x] Make VM/bytecode the default execution path
- [x] Deduplicate constant pool entries
- [x] 32-bit jump offsets (OP_JUMP_WIDE)
- [x] Integer division opcode (OP_IDIV)
- [x] Unary minus fast path for integers

## Phase 2 — GC / Memory Improvements
- [x] Iterative mark phase (explicit worklist, no stack overflow risk)
- [x] Threshold-based major GC trigger (old-gen byte limit)
- [x] AST arena allocator (bulk-free after parse/compile)
- [x] Bump-pointer slab allocator for young generation
- [x] Stress-mode collection on every allocation (`PRISM_GC_STRESS=1`)
- [x] Expose young/old generation counters in `--gc-stats`
- [x] Cycle-focused tests (arrays/dicts/closures forming reference cycles)
- [x] Wire gc_push_root/gc_pop_root per value-creating opcode in vm_run
- [x] Thread-safe allocation (future-proof with mutex stubs)

## Phase 3 — XGUI & PSS Improvements
- [x] `link style` / `link style, design` PSS-linking syntax in Prism
- [x] Multiple PSS file merging (later files override earlier)
- [x] New XGUI widgets: image, dropdown, listview, tabs, grid, chart, spinner, tooltip_widget
- [x] Column layout (xgui_col_begin / xgui_col_end)
- [x] Grid layout (xgui_grid / xgui_grid_cell)
- [x] Event handler system (on_click, on_change, on_hover)
- [x] PSS animation properties (transition, duration)
- [x] PSS gradient backgrounds
- [x] More PSS selectors and widget states
- [x] xgui_image widget
- [x] xgui_dropdown widget
- [x] xgui_listview widget
- [x] xgui_tabs widget
- [x] xgui_chart (bar chart, line chart)
- [x] xgui_spinner (loading indicator)
- [x] xgui_alert / xgui_modal

## Phase 4 — Standard Library Improvements
- [x] `http` module: GET, POST, PUT, DELETE, headers, JSON body
- [x] Enhanced `fs` module: read, write, append, exists, remove, mkdir, listdir, stat, copy, move
- [x] `regex` module: match, find_all, replace, split, groups
- [x] `csv` improvements: read, write, headers, streaming parse
- [x] `datetime` improvements: format, parse, diff, timezones, strftime
- [x] `process` module: run commands, pipe output, env vars, exit codes
- [x] `crypto` improvements: sha256, md5, base64, hmac, pbkdf2
- [x] `net` module: TCP sockets, UDP, DNS resolve
- [x] `html` improvements: parse, query, escape, templates
- [x] `testing` improvements: assert_eq, assert_raises, mock, benchmark runner
- [x] `iter` improvements: zip, enumerate, chain, take, drop, flatten, group_by
- [x] `collections` improvements: deque, priority_queue, ordered_dict, counter
- [x] `strings` improvements: more string utilities
- [x] `math` improvements: matrix ops, statistics, constants
- [x] `os` improvements: env, platform info, argv, getcwd

## Phase 5 — Language Features (Chaining, Syntax)
- [x] Chain comparison: `1 < x < 10`
- [x] Chain function calls: `.method1().method2().method3()`
- [x] Chain assignment: `a = b = c = 0`
- [x] Walrus operator `:=` (declare-assign shortcut)
- [x] `repeat N { }` loop
- [x] `repeat while cond { }` loop
- [x] `repeat until cond { }` loop
- [x] Range `..` and `..=` (inclusive) operators
- [x] `for i in 1..10 { }` range loop
- [x] `for i in 0..100 step 5 { }` stepped range
- [x] `expect` assertion statement
- [x] `when` reactive condition blocks
- [x] Optional semicolons already work
- [x] Pipe operator `|>` improvements
- [x] `?:` ternary expression
- [x] `is` type check operator
- [x] `not in` membership
- [x] Improved f-string: pre-parsed templates at parse time

## Phase 6 — Tests & Edge Cases
- [x] test_chaining.pr — chain comparisons, method chains, assignment chains
- [x] test_gc_cycles.pr — circular reference GC cycle tests
- [x] test_closures_advanced.pr — deep closure nesting, upvalue capture
- [x] test_error_handling.pr — try/catch/throw edge cases
- [x] test_memory.pr — memory pressure, large allocations
- [x] test_recursion.pr — deep recursion, mutual recursion, tail-call patterns
- [x] test_types.pr improvements — all type coercions and edge cases
- [x] test_numeric.pr — overflow, underflow, NaN, Inf, hex, bin, oct
- [x] test_strings_advanced.pr — regex, unicode edge cases, multiline
- [x] test_iterators.pr — all iter operations
- [x] test_http.pr — http module
- [x] test_fs_advanced.pr — file edge cases
- [x] test_pss_link.pr — PSS link directive
- [x] test_vm.pr — bytecode VM correctness
- [x] test_jit.pr — JIT correctness

## Phase 7 — Benchmarks (Cross-Language Comparison)
- [x] bench_fibonacci.pr — Prism vs Python vs C
- [x] bench_sieve.pr — Sieve of Eratosthenes
- [x] bench_string_ops.pr — string manipulation
- [x] bench_dict_ops.pr — dictionary operations
- [x] bench_array_ops.pr — array operations
- [x] bench_closures.pr — closure-heavy workloads
- [x] bench_gc_pressure.pr — GC under load
- [x] bench_jit.pr — JIT vs interpreter
- [x] compare.sh — shell script to run Prism + Python + node.js benchmarks
- [x] BENCHMARK_RESULTS.md — documented comparison results

## Phase 8 — Documentation
- [x] docs/speed-guide.md — performance tuning guide
- [x] docs/gc-deep-dive.md — GC internals deep dive
- [x] docs/xgui-reference.md — complete XGUI + PSS reference
- [x] docs/stdlib-reference.md — full standard library reference
- [x] docs/internals.md — VM, compiler, JIT internals
- [x] docs/error-guide.md — error messages and debugging
- [x] docs/chaining.md — chain syntax guide
- [x] docs/pss-reference.md — complete PSS styling reference
- [x] docs/benchmarks.md — benchmark results and methodology

## Phase 9 — Examples
- [x] examples/web_scraper.pr
- [x] examples/json_api.pr
- [x] examples/file_manager.pr
- [x] examples/data_pipeline.pr
- [x] examples/gui_calculator.pr
- [x] examples/gui_todo.pr
- [x] examples/gui_styled.pr
- [x] examples/chaining_demo.pr
- [x] examples/advanced_closures.pr
- [x] examples/pattern_matching.pr
- [x] examples/class_hierarchy.pr
- [x] examples/functional_demo.pr
- [x] examples/crypto_demo.pr
- [x] examples/regex_demo.pr
- [x] examples/benchmark_runner.pr

## Phase 10 — Pending todo.md / pipeline.md Items
- [x] Iterative mark phase
- [x] Wire string interning at all call-sites
- [x] Threshold-based major GC trigger
- [x] Expose generation counters in --gc-stats
- [x] Cycle-focused tests
- [x] AST arena allocator
- [x] Wire gc_push_root per value-creating opcode in vm_run
- [x] Stress-mode collection
- [x] Computed-goto dispatch
- [x] Direct uint8_t* ip pointer
- [x] Strip bounds checks in release
- [x] Local variable slots
- [x] Constant folding
- [x] 32-bit jump offsets
- [x] Dead code elimination
- [x] Deduplicate constant pool
- [x] fs module
- [x] os module
- [x] net/http module
- [x] Native json module improvements
- [x] REPL improvements
- [x] Better error handling for edge cases

## New Features (for review)
See: NEW_FEATURES.md
