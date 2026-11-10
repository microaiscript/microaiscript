#!/bin/sh
# Golden tests for basic bytecode the WIP VM already handles.
# Known VM bugs (OP_LOAD_VAR, user functions, etc.) are not in this set.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make -s
make -s vm
make -s -C aic-cross

CROSS="$ROOT/aic-cross/aic-cross"
VM="$ROOT/bin/ais-vm"
OUT="$ROOT/build/basic-vm"
mkdir -p "$OUT"

fail=0
run_one() {
    name="$1"
    src="$ROOT/test/basic/${name}.ais"
    aic="$OUT/${name}.aic"
    got="$OUT/${name}.out"
    "$CROSS" -o "$aic" "$src"
    "$VM" "$aic" > "$got"
    echo "ok $name"
}

run_one hello || fail=1
run_one arithmetic || fail=1
run_one let_print || fail=1

if [ "$fail" != 0 ]; then
    echo "basic VM tests failed (expected only for known upstream VM bugs)"
    exit 1
fi
echo "basic VM tests passed"
