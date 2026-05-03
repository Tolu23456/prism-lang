#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════
#  Prism vs Python — Speed & Flexibility Benchmark Suite
#  Usage: bash benchmarks/run_comparison.sh
# ═══════════════════════════════════════════════════════════════

set -euo pipefail
PRISM="./prism"
PRISM_TREE="./prism --tree"
PY="python3"
REPORT="benchmarks/COMPARISON_RESULTS.md"

# ── colour helpers ──────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; RST='\033[0m'

# ── timing helper: returns wall-clock ms ────────────────────────
time_ms() {
    local start end
    start=$(date +%s%N)
    "$@" >/dev/null 2>&1
    end=$(date +%s%N)
    echo $(( (end - start) / 1000000 ))
}

# ── run N times, return median ───────────────────────────────────
median_ms() {
    local -a times=()
    local runs=${1}; shift
    for ((k=0; k<runs; k++)); do
        times+=( $(time_ms "$@") )
    done
    # sort and pick middle
    IFS=$'\n' sorted=($(sort -n <<<"${times[*]}")); unset IFS
    echo "${sorted[ $((runs/2)) ]}"
}

RUNS=3   # median of 3 runs for each benchmark

# ── storage arrays ───────────────────────────────────────────────
declare -a NAMES PRISM_T PY_T RATIO WINNER

add_result() {
    local name="$1" pt="$2" pyt="$3"
    NAMES+=("$name")
    PRISM_T+=("$pt")
    PY_T+=("$pyt")
    if   [ "$pt" -eq 0 ] && [ "$pyt" -eq 0 ]; then RATIO+=("1.00"); WINNER+=("tie")
    elif [ "$pt" -eq 0 ]; then RATIO+=("∞"); WINNER+=("prism")
    elif [ "$pyt" -eq 0 ]; then RATIO+=("∞"); WINNER+=("python")
    else
        # ratio = py/prism  (>1 means Prism is faster)
        local r
        r=$(awk "BEGIN{printf \"%.2f\", $pyt/$pt}")
        RATIO+=("$r")
        local rf
        rf=$(awk "BEGIN{printf \"%.0f\", $pyt/$pt*100}")
        if   [ "$rf" -gt 110 ]; then WINNER+=("prism")
        elif [ "$rf" -lt  90 ]; then WINNER+=("python")
        else WINNER+=("tie")
        fi
    fi
}

print_header() {
    echo ""
    echo -e "${BLD}${CYN}╔══════════════════════════════════════════════════════════════╗${RST}"
    echo -e "${BLD}${CYN}║      Prism vs Python — Speed & Flexibility Benchmark         ║${RST}"
    echo -e "${BLD}${CYN}║      $(date '+%Y-%m-%d %H:%M')   (median of ${RUNS} runs each)              ║${RST}"
    echo -e "${BLD}${CYN}╚══════════════════════════════════════════════════════════════╝${RST}"
    echo ""
}

section() { echo -e "\n${BLD}${YLW}── $1 ──${RST}"; }

run_bench() {
    local label="$1"; local prism_file="$2"; local py_file="$3"
    printf "  %-38s" "$label"
    local pt pyt
    pt=$(median_ms $RUNS $PRISM "$prism_file")
    pyt=$(median_ms $RUNS $PY "$py_file")
    local winner_col=""
    add_result "$label" "$pt" "$pyt"
    local idx=$(( ${#NAMES[@]} - 1 ))
    if [ "${WINNER[$idx]}" = "prism" ]; then winner_col="${GRN}Prism${RST}"
    elif [ "${WINNER[$idx]}" = "python" ]; then winner_col="${RED}Python${RST}"
    else winner_col="${YLW}tie${RST}"; fi
    echo -e "Prism=${BLD}${pt}ms${RST}  Python=${BLD}${pyt}ms${RST}  ratio=${RATIO[$idx]}x  winner: $winner_col"
}

# ════════════════════════════════════════════════════════════════
print_header

# ── SPEED benchmarks ─────────────────────────────────────────────
section "SPEED: Recursion"
run_bench "fib(32) recursive"         benchmarks/fib_recursive.pr    benchmarks/py/fib_recursive.py
run_bench "recursive sum ×5000"       benchmarks/recursive_sum.pr    benchmarks/py/recursive_sum.py
run_bench "ackermann(3,5) ×100"       benchmarks/ackermann.pr        benchmarks/py/ackermann.py

section "SPEED: Loops & Arithmetic"
run_bench "tight loop 5M iters"       benchmarks/loop_count.pr       benchmarks/py/loop_count.py
run_bench "fib_iter 2M steps"         benchmarks/fib_iterative.pr    benchmarks/py/fib_iterative.py

section "SPEED: Data Structures"
run_bench "array ops 100K"            benchmarks/array_ops.pr        benchmarks/py/array_ops.py
run_bench "dict insert+lookup 10K"    benchmarks/dict_ops.pr         benchmarks/py/dict_ops.py
run_bench "sieve of eratosthenes 500K" benchmarks/sieve.pr           benchmarks/py/sieve.py
run_bench "bubble sort 800 elems"     benchmarks/bubble_sort.pr      benchmarks/py/bubble_sort.py

section "SPEED: Strings"
run_bench "string ops 50K iters"      benchmarks/string_ops.pr       benchmarks/py/string_ops.py

# ── FLEXIBILITY benchmarks ──────────────────────────────────────
section "FLEXIBILITY: Full Feature Suite"
printf "  %-38s" "Prism flexibility suite (--tree)"
if $PRISM_TREE benchmarks/flex_prism.pr >/dev/null 2>&1; then
    echo -e "${GRN}PASS${RST}"
else
    echo -e "${RED}FAIL${RST}"
    $PRISM_TREE benchmarks/flex_prism.pr 2>&1 | head -20
fi
printf "  %-38s" "Python flexibility suite"
if $PY benchmarks/py/flex_python.py >/dev/null 2>&1; then
    echo -e "${GRN}PASS${RST}"
else
    echo -e "${RED}FAIL${RST}"
    $PY benchmarks/py/flex_python.py 2>&1 | head -20
fi

echo ""
echo "── FLEXIBILITY: Feature comparison ──"
printf "  %-35s %-12s %-12s\n" "Feature" "Prism" "Python"
printf "  %-35s %-12s %-12s\n" "-------" "-----" "------"
features=(
    "Closures / captured state"
    "First-class functions"
    "Higher-order (map/filter/reduce)"
    "Pattern matching"
    "Classes & inheritance"
    "f-string interpolation"
    "Multiple return / tuple unpack"
    "Sets (union, intersect, diff)"
    "Dynamic typing"
    "Inline lambdas"
    "Recursion (with stack limit)"
    "Generational GC"
    "JIT compiler (hot loops)"
    "Bytecode VM"
    "X11 native GUI"
    "Standard library (json/math/fs)"
    "Regex (built-in)"
    "Async / coroutines"
    "Decorators"
    "Generators / yield"
    "Type annotations"
    "Package ecosystem (pip)"
)
p_support=(yes yes yes yes yes yes yes yes yes yes yes yes yes yes yes yes no no no no no no)
py_support=(yes yes yes yes yes yes yes yes yes yes yes yes no no no yes yes yes yes yes yes yes)

for i in "${!features[@]}"; do
    ps="${p_support[$i]}"; pys="${py_support[$i]}"
    pc=""; pyc=""
    if [ "$ps" = "yes" ]; then pc="${GRN}✓ yes${RST}"; else pc="${RED}✗ no${RST}"; fi
    if [ "$pys" = "yes" ]; then pyc="${GRN}✓ yes${RST}"; else pyc="${RED}✗ no${RST}"; fi
    printf "  %-35s " "${features[$i]}"
    echo -e "${pc}         ${pyc}"
done

# ── SUMMARY TABLE ─────────────────────────────────────────────────
echo ""
echo -e "${BLD}${CYN}══════════════════════════════════════════════════════════════${RST}"
echo -e "${BLD}  SPEED SUMMARY TABLE${RST}"
echo -e "${BLD}${CYN}══════════════════════════════════════════════════════════════${RST}"
printf "  ${BLD}%-38s %8s %8s %8s %8s${RST}\n" "Benchmark" "Prism(ms)" "Python(ms)" "Ratio" "Winner"
printf "  %-38s %8s %8s %8s %8s\n" "---------" "---------" "----------" "-----" "------"
prism_wins=0; py_wins=0; ties=0
for i in "${!NAMES[@]}"; do
    w="${WINNER[$i]}"
    wlabel="$w"
    if [ "$w" = "prism" ]; then
        wlabel="${GRN}Prism${RST}"; prism_wins=$((prism_wins+1))
    elif [ "$w" = "python" ]; then
        wlabel="${RED}Python${RST}"; py_wins=$((py_wins+1))
    else
        wlabel="${YLW}tie${RST}"; ties=$((ties+1))
    fi
    printf "  %-38s %8s %8s %8s " "${NAMES[$i]}" "${PRISM_T[$i]}" "${PY_T[$i]}" "${RATIO[$i]}x"
    echo -e "$wlabel"
done
echo ""
echo -e "  Speed wins — ${GRN}Prism: ${prism_wins}${RST}  ${RED}Python: ${py_wins}${RST}  ${YLW}Ties: ${ties}${RST}"

# ── Write Markdown report ─────────────────────────────────────────
{
cat <<EOF
# Prism vs Python — Benchmark Report

**Date:** $(date '+%Y-%m-%d %H:%M')
**Prism build:** debug (\`gcc -std=c11 -g\`, no -O flags)
**Python version:** $(python3 --version 2>&1)
**Method:** median of ${RUNS} wall-clock runs per benchmark

## Speed Results

| Benchmark | Prism (ms) | Python (ms) | Ratio (py/prism) | Winner |
|-----------|-----------|------------|-----------------|--------|
EOF
for i in "${!NAMES[@]}"; do
    w="${WINNER[$i]}"
    echo "| ${NAMES[$i]} | ${PRISM_T[$i]} | ${PY_T[$i]} | ${RATIO[$i]}x | $w |"
done

echo ""
echo "**Speed wins — Prism: ${prism_wins}  Python: ${py_wins}  Ties: ${ties}**"

cat <<'EOF'

## Flexibility Comparison

| Feature | Prism | Python |
|---------|-------|--------|
| Closures / captured state | ✓ | ✓ |
| First-class functions | ✓ | ✓ |
| Higher-order (map/filter/reduce) | ✓ | ✓ |
| Pattern matching | ✓ | ✓ (3.10+) |
| Classes & inheritance | ✓ | ✓ |
| f-string interpolation | ✓ | ✓ |
| Multiple return / tuple unpack | ✓ | ✓ |
| Sets (union, intersect, diff) | ✓ | ✓ |
| Dynamic typing | ✓ | ✓ |
| Inline lambdas | ✓ | ✓ |
| Generational GC | ✓ | ✓ |
| JIT compiler (hot loops) | ✓ | ✗ (CPython) |
| Bytecode VM | ✓ | ✓ |
| X11 native GUI | ✓ | ✗ built-in |
| Regex (built-in) | ✗ | ✓ |
| Async / coroutines | ✗ | ✓ |
| Decorators | ✗ | ✓ |
| Generators / yield | ✗ | ✓ |
| Type annotations | ✗ | ✓ |
| Package ecosystem (pip) | ✗ | ✓ |

## Notes

- Prism is compiled with debug flags (`-g`, no `-O2`/`-O3`). A release build
  (`make release`) with `-O3 -march=native` would significantly reduce Prism times.
- Python results are CPython 3.12 (interpreter only, no PyPy/Cython).
- Ratio >1 means Prism is faster; <1 means Python is faster.
EOF
} > "$REPORT"

echo ""
echo -e "  ${BLD}Full Markdown report written to:${RST} $REPORT"
echo ""
