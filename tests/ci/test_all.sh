#!/bin/sh
# SPDX-License-Identifier: MIT
# The repository's own tests, as CI runs them (LV-16: green means they ran). The tests are found, not listed:
# every run.sh, run_*.sh, test_*.sh and test_*.py in a tests/ or test/ folder anywhere in the tree (not .git,
# build, vendor, third_party, external, upstream or node_modules), so a new test is in CI the day it is added.
# tests/ci/extra.txt adds the repository's other test commands, one a line ("name @ paths: command", run from
# the top). A test that needs what CI's machine has not got (a stove, an Amiga, a lab) is named in
# tests/ci/skip.txt with why; a test may also exit 77 to say it skipped itself. Each test has TEST_TIMEOUT
# seconds (default 900).
#
# Targeted per pull request, everything at night (Dale, 24 Nov 2026). With TEST_BASE (the commit to compare
# with) only the tests the change touches run:
#   - a test in X/tests/ (or X/test/) when anything under X/ changed;
#   - a test in the top tests/ folder when it changed, or when its text names a changed file (its name
#     without the extension, 3 letters or more);
#   - an extra.txt line when a change is under one of its paths; a line with no paths runs in the whole run only.
# A change to tests/ci/, tests/ci_runner/ or repo-tests.yml, a diff that can't be read, or TEST_SCOPE=all
# runs everything, as the nightly regression does.
#   tests/ci/test_all.sh [-list]          TEST_BASE=origin/main tests/ci/test_all.sh
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
    [ -f "$EXTRA" ] && grep -v '^#' "$EXTRA" | grep ':' | sed -e 's/:.*//' -e 's/ *@.*//' -e 's/^/extra:/'
}
extra_line() { grep -v '^#' "$EXTRA" | grep -E "^$1( *@[^:]*)?:" | head -n 1; }
skipped() { [ -f "$SKIP" ] && grep -v '^#' "$SKIP" | awk '{print $1}' | grep -qx "$1"; }
limit() { if command -v timeout >/dev/null 2>&1; then timeout "$TMO" "$@"; else "$@"; fi; }

CHANGED=ALL
if [ -n "${TEST_BASE:-}" ] && [ "${TEST_SCOPE:-changed}" != all ]; then
    if CHANGED=$(git diff --name-only "$TEST_BASE"...HEAD 2>/dev/null); then
        printf '%s\n' "$CHANGED" | grep -qE '^(tests/ci/|tests/ci_runner/|\.github/workflows/repo-tests\.yml$)' && CHANGED=ALL
    else
        CHANGED=ALL
    fi
fi

# touched TEST: does the change reach it?
touched() {
    [ "$CHANGED" = ALL ] && return 0
    [ -n "$CHANGED" ] || return 1
    case "$1" in
    extra:*)
        paths=$(extra_line "${1#extra:}" | sed -n 's/^[^@:]*@ *\([^:]*\):.*/\1/p')
        for p in $paths; do
            p=${p%/}
            printf '%s\n' "$CHANGED" | grep -qE "^$p(/|\$)" && return 0
        done
        return 1 ;;
    esac
    printf '%s\n' "$CHANGED" | grep -qxF "$1" && return 0
    owner=$(printf '%s\n' "$1" | sed -n 's%^\(.*\)/tests\{0,1\}/.*%\1%p')
    if [ -n "$owner" ]; then
        printf '%s\n' "$CHANGED" | grep -q "^$owner/"
        return
    fi
    for f in $CHANGED; do
        stem=$(basename "$f"); stem=${stem%.*}
        [ ${#stem} -ge 3 ] && grep -qF -- "$stem" "$1" 2>/dev/null && return 0
    done
    return 1
}

if [ "${1:-}" = "-list" ]; then for t in $(found); do touched "$t" && echo "$t"; done; exit 0; fi

pass=0 fail=0 skip=0 untouched=0 failed=""
LOG=${OUT:-$ROOT/build/tests}/ci
mkdir -p "$LOG"
if [ "$CHANGED" = ALL ]; then echo "Every test"; else echo "The tests that $(printf '%s\n' "$CHANGED" | grep -c .) changed file(s) touch"; fi
for t in $(found); do
    touched "$t" || { untouched=$((untouched + 1)); continue; }
    if skipped "$t"; then echo "SKIP $t ($(grep "^$t " "$SKIP" | cut -d' ' -f2-))"; skip=$((skip + 1)); continue; fi
    echo "RUN  $t"
    l=$LOG/$(echo "$t" | tr '/:' '__').log
    case "$t" in
        extra:*) c=$(extra_line "${t#extra:}" | cut -d: -f2-)
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
echo "REPO_TESTS pass=$pass fail=$fail skip=$skip untouched=$untouched"
[ $fail -eq 0 ] || { echo "failed:$failed"; exit 1; }
