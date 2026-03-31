#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$ROOT_DIR/build/lox"
TESTS_DIR="$SCRIPT_DIR/golden"

if [[ ! -x "$BINARY" ]]; then
    echo "Binary not found: $BINARY"
    echo "Run 'make' first."
    exit 1
fi

PASS=0
FAIL=0

for dir in "$TESTS_DIR"/*/; do
    name=$(basename "$dir")
    input="$dir/input.lox"

    if [[ ! -f "$input" ]]; then
        continue
    fi

    actual_stdout=$("$BINARY" "$input" 2>/tmp/lox_stderr.txt) || true
    actual_stderr=$(cat /tmp/lox_stderr.txt)

    test_passed=true

    # Check stdout
    if [[ -f "$dir/expected_stdout.txt" ]]; then
        expected_stdout=$(cat "$dir/expected_stdout.txt")
        if [[ "$actual_stdout" != "$expected_stdout" ]]; then
            echo "FAIL $name (stdout)"
            diff <(printf '%s\n' "$expected_stdout") <(printf '%s\n' "$actual_stdout") | head -20
            test_passed=false
        fi
    fi

    # Check stderr (optional)
    if [[ -f "$dir/expected_stderr.txt" ]]; then
        expected_stderr=$(cat "$dir/expected_stderr.txt")
        if [[ "$actual_stderr" != "$expected_stderr" ]]; then
            echo "FAIL $name (stderr)"
            diff <(printf '%s\n' "$expected_stderr") <(printf '%s\n' "$actual_stderr") | head -20
            test_passed=false
        fi
    fi

    if $test_passed; then
        echo "PASS $name"
        ((PASS++))
    else
        ((FAIL++))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
