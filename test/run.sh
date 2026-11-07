#!/bin/sh
#
# test/run.sh - build and run the sqlite unit suite.
#
# One CT program (unit.ct) asserts every public method. This script builds it
# with -Wall -Wextra, fails on any compiler warning, runs it, and prints the
# assertion counts. Exit 0 means all assertions passed.
#
#   sh test/run.sh
#   CT=/path/to/ct sh test/run.sh
#
# The default compiler is the frozen ct 0.20.0 worktree. Override CT to use
# another ct >= 0.20.0.
#
# License: MIT (see LICENSE).

set -u

here="$(cd "$(dirname "$0")" && pwd)"
pkg="$(cd "$here/.." && pwd)"
CT="${CT:-/tmp/ct-0.20.0-worktree/ct}"

if [ ! -x "$CT" ]; then
    echo "ct not found or not executable: $CT" >&2
    echo "set CT=/path/to/ct (ct >= 0.20.0)" >&2
    exit 1
fi

work="${TMPDIR:-/tmp}/ct-sqlite-unit.$$"
rm -rf "$work"
mkdir -p "$work" || exit 1

cleanup() {
    rm -rf "$work"
    rm -f /tmp/ct-sqlite-unit.db /tmp/ct-sqlite-unit-ro.db
    rm -f /tmp/ct-sqlite-unit.db-journal /tmp/ct-sqlite-unit-ro.db-journal
}
trap cleanup EXIT HUP INT TERM

rm -f /tmp/ct-sqlite-unit.db /tmp/ct-sqlite-unit-ro.db

echo "ct: $CT ($("$CT" --version 2>/dev/null || true))"
echo

echo "== build =="
if ! "$CT" build -I "$pkg" --ldflags '-lsqlite3' --cflags '-Wall -Wextra' \
        "$here/unit.ct" -o "$work/unit" >"$work/build.log" 2>&1; then
    echo "build failed:" >&2
    cat "$work/build.log" >&2
    exit 1
fi
if grep -q 'warning:' "$work/build.log"; then
    echo "build produced warnings:" >&2
    cat "$work/build.log" >&2
    exit 1
fi
echo "ok: ct build -Wall -Wextra (zero warnings)"
echo

echo "== unit =="
"$work/unit" >"$work/unit.out" 2>"$work/unit.err"
rc=$?
cat "$work/unit.out"
if [ -s "$work/unit.err" ]; then
    cat "$work/unit.err" >&2
fi

summary="$(sed -n 's/^SUMMARY passed=\([0-9][0-9]*\) failed=\([0-9][0-9]*\)$/\1 \2/p' "$work/unit.out")"
if [ -z "$summary" ]; then
    echo "no SUMMARY line (test crashed?)" >&2
    exit 1
fi
pass="${summary% *}"
fail="${summary#* }"

echo
printf 'sqlite: %s passed, %s failed\n' "$pass" "$fail"

if [ "$rc" -ne 0 ] || [ "$fail" -ne 0 ]; then
    exit 1
fi
exit 0
