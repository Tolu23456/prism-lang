#!/usr/bin/env bash
# Prism test runner — runs all tests/test_*.pm files
# Exit 0 if all pass, 1 if any fail.

PRISM="${1:-./prism}"
PASS=0
FAIL=0
ERRORS=""

if [ ! -x "$PRISM" ]; then
    echo "ERROR: prism binary not found at $PRISM — run 'make' first"
    exit 1
fi

for f in tests/test_*.pm; do
    OUTPUT=$("$PRISM" "$f" 2>&1)
    CODE=$?
    if [ $CODE -eq 0 ]; then
        echo "  PASS  $f"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $f"
        # Show the [FAIL] lines only
        FAIL_LINE=$(echo "$OUTPUT" | grep "\[FAIL\]")
        if [ -n "$FAIL_LINE" ]; then
            echo "        $FAIL_LINE"
        fi
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  $f"
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failed tests:$ERRORS"
    exit 1
fi
