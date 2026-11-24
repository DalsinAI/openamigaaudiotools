#!/bin/sh
# SPDX-License-Identifier: MIT
# Every one of the repository's own tests, as pull request CI runs them (LV-16: a PR is green only when
# its repository's tests ran). The tests are found, not listed: every run.sh, run_*.sh, test_*.sh and
# test_*.py in a tests/ or test/ folder anywhere in the tree (not .git, build, vendor, third_party,
# external, upstream or node_modules), so a new test is in CI the day it is added. tests/ci/extra.txt
# adds the repository's other test commands, one a line ("name: command", run from the top).
# A test that needs what CI's machine has not got (a stove, an Amiga, a lab) is named in
# tests/ci/skip.txt with why; a test may also exit 77 to say it skipped itself. Each test has
# TEST_TIMEOUT seconds (default 900).
#   tests/ci/test_all.sh [-list]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SKIP=$HERE/skip.txt
EXTRA=$HERE/extra.txt
TMO=${TEST_TIMEOUT:-900}
cd "$ROOT" || exit 2

found() {
    find . \( -name .git -o -name build -o -name vendor -o -name third_party -o -name external -o -name upstream \
              -o -name node_modules \) -prune -o -type f \( -name run.sh -o -name 'run_*.sh' -o -name 'test_*.sh' \
              -o -name 'test_*.py' \) -print | sed 's|^\./||' | grep -E '(^|/)tests?/' | grep -v '^tests/ci/' | sort
    [ -f "$EXTRA" ] && grep -v '^#' "$EXTRA" | grep ':' | sed 's/:.*//' | sed 's/^/extra:/'
}
skipped() { [ -f "$SKIP" ] && grep -v '^#' "$SKIP" | awk '{print $1}' | grep -qx "$1"; }
limit() { if command -v timeout >/dev/null 2>&1; then timeout "$TMO" "$@"; else "$@"; fi; }

if [ "${1:-}" = "-list" ]; then found; exit 0; fi

pass=0 fail=0 skip=0 failed=""
LOG=${OUT:-$ROOT/build/tests}/ci
mkdir -p "$LOG"
for t in $(found); do
    if skipped "$t"; then echo "SKIP $t ($(grep "^$t " "$SKIP" | cut -d' ' -f2-))"; skip=$((skip + 1)); continue; fi
    l=$LOG/$(echo "$t" | tr '/:' '__').log
    case "$t" in
        extra:*) c=$(grep -v '^#' "$EXTRA" | grep "^${t#extra:}:" | head -n 1 | cut -d: -f2-)
                 limit sh -c "$c" >"$l" 2>&1 ;;
        *.py) d=$(dirname "$t"); m=$(basename "$t" .py)
              if ! grep -qE '^import (unittest|pytest)|^from (unittest|pytest)' "$t" &&
                 { grep -q '__name__ *== *.__main__.' "$t" || ! grep -qE '^ *def test_' "$t"; }; then
                  limit python3 "$t" >"$l" 2>&1          # a script: run from the top, as its workflow ran it
              elif python3 -c 'import pytest' 2>/dev/null; then limit python3 -m pytest -q "$t" >"$l" 2>&1
              else (cd "$d" && limit python3 -m unittest -v "$m") >"$l" 2>&1; fi ;;
        *)    limit sh "$t" >"$l" 2>&1 ;;
    esac
    rc=$?
    if [ $rc -eq 0 ]; then echo "PASS $t"; pass=$((pass + 1))
    elif [ $rc -eq 77 ]; then echo "SKIP $t (skipped itself)"; skip=$((skip + 1))
    else
        [ $rc -eq 124 ] && echo "FAIL $t (timed out after ${TMO}s), its last lines:" || echo "FAIL $t (exit $rc), its last lines:"
        tail -n 30 "$l" | sed 's/^/    /'; fail=$((fail + 1)); failed="$failed $t"
    fi
done
echo "REPO_TESTS pass=$pass fail=$fail skip=$skip"
[ $fail -eq 0 ] || { echo "failed:$failed"; exit 1; }
