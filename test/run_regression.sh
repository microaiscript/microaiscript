#!/bin/sh
# Run every *.ais script in a directory with the given aiscript binary.
# A script passes when it exits with status 0 within 10 seconds.
#
# usage: run_regression.sh <aiscript-binary> <directory>
set -u

bin=$1
dir=$2
pass=0
fail=0

cd "$dir" || exit 2
for f in *.ais; do
    case "$f" in math_utils*.ais) continue ;; esac  # helper modules, not tests
    if timeout 10 "$bin" "$f" </dev/null >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        echo "FAIL: $f"
        fail=$((fail + 1))
    fi
done

echo "regression scripts: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
