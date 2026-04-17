#!/usr/bin/env bash
# Prism cross-language benchmark comparison
# Compares Prism against Python 3 and Lua (if available) on common tasks.

set -e
PRISM="./prism"
RESULTS_DIR="benchmarks/results"
mkdir -p "$RESULTS_DIR"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║           Prism Cross-Language Benchmark                     ║"
echo "║  Date: $(date +%Y-%m-%d)                                         ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# ─── Helper: time a command in ms ──────────────────────────────
time_ms() {
    local start end elapsed
    start=$(python3 -c "import time; print(int(time.time()*1000))" 2>/dev/null || date +%s%3N)
    "$@" >/dev/null 2>&1
    end=$(python3 -c "import time; print(int(time.time()*1000))" 2>/dev/null || date +%s%3N)
    echo $((end - start))
}

# ─── Fibonacci ─────────────────────────────────────────────────
echo "── Fibonacci(35) ──"

FIB_PRISM_CODE='
func fib(n) {
    if n < 2 { return n }
    return fib(n-1) + fib(n-2)
}
print(fib(35))
'
echo "$FIB_PRISM_CODE" > /tmp/bench_fib.pr
T_PRISM=$(time_ms $PRISM /tmp/bench_fib.pr)
echo "  Prism:  ${T_PRISM}ms"

if command -v python3 &>/dev/null; then
    T_PY=$(time_ms python3 -c "
def fib(n):
    if n < 2: return n
    return fib(n-1) + fib(n-2)
print(fib(35))
")
    echo "  Python: ${T_PY}ms"
    if [ "$T_PY" -gt 0 ]; then
        RATIO=$(echo "scale=2; $T_PY / $T_PRISM" | bc -l 2>/dev/null || echo "N/A")
        echo "  Ratio Prism/Python: ${RATIO}x"
    fi
fi

if command -v lua &>/dev/null || command -v lua5.4 &>/dev/null; then
    LUA_CMD=$(command -v lua5.4 || command -v lua)
    T_LUA=$(time_ms $LUA_CMD -e "
function fib(n)
    if n < 2 then return n end
    return fib(n-1) + fib(n-2)
end
print(fib(35))
")
    echo "  Lua:    ${T_LUA}ms"
fi

echo ""

# ─── Sum 1..10M ────────────────────────────────────────────────
echo "── Integer sum 1..10M ──"

SUM_PRISM_CODE='
let s = 0
let i = 0
while i < 10000000 { s += i; i += 1 }
print(s)
'
echo "$SUM_PRISM_CODE" > /tmp/bench_sum.pr
T_PRISM2=$(time_ms $PRISM /tmp/bench_sum.pr)
echo "  Prism:  ${T_PRISM2}ms"

if command -v python3 &>/dev/null; then
    T_PY2=$(time_ms python3 -c "
s = 0
for i in range(10000000):
    s += i
print(s)
")
    echo "  Python: ${T_PY2}ms"
fi

echo ""

# ─── String operations ─────────────────────────────────────────
echo "── String concat x10000 ──"

STR_PRISM='
let s = ""
let i = 0
while i < 10000 { s = s + "x"; i += 1 }
print(len(s))
'
echo "$STR_PRISM" > /tmp/bench_str.pr
T_PRISM3=$(time_ms $PRISM /tmp/bench_str.pr)
echo "  Prism:  ${T_PRISM3}ms"

if command -v python3 &>/dev/null; then
    T_PY3=$(time_ms python3 -c "
s = ''
for i in range(10000):
    s += 'x'
print(len(s))
")
    echo "  Python: ${T_PY3}ms"
fi

echo ""
echo "═══════════════════════════════════════════"
echo "Results saved to $RESULTS_DIR/"
echo "$(date)" > "$RESULTS_DIR/last_run.txt"
echo "Prism fib(35): ${T_PRISM}ms" >> "$RESULTS_DIR/last_run.txt"
echo "Prism sum 10M: ${T_PRISM2}ms" >> "$RESULTS_DIR/last_run.txt"
echo "Prism str 10K: ${T_PRISM3}ms" >> "$RESULTS_DIR/last_run.txt"
cat "$RESULTS_DIR/last_run.txt"
