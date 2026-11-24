#!/bin/sh
# SPDX-License-Identifier: MIT
# tests/ci/test_all.sh on a made-up repository: it finds every kind of test in any tests/ folder, runs
# tests/ci/extra.txt's commands, a failing test makes it fail and is named, exit 77 and tests/ci/skip.txt
# skip, a test past TEST_TIMEOUT fails, and files outside the patterns or in vendor/ are not run.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/tests/ci" "$T/tests/a" "$T/tests/b" "$T/lib/x/tests" "$T/vendor/y/tests"
cp "$HERE/../ci/test_all.sh" "$T/tests/ci/"
printf 'exit 0\n'  > "$T/tests/run.sh"
printf 'exit 0\n'  > "$T/tests/a/test_good.sh"
printf 'exit 0\n'  > "$T/lib/x/tests/run.sh"
printf 'exit 1\n'  > "$T/vendor/y/tests/test_theirs.sh"
printf 'exit 77\n' > "$T/tests/a/test_self_skip.sh"
printf 'exit 1\n'  > "$T/tests/a/test_listed.sh"
printf 'exit 1\n'  > "$T/tests/a/helper.sh"
printf 'import unittest\nclass T(unittest.TestCase):\n    def test_ok(self): pass\n' > "$T/tests/b/test_ok.py"
printf 'import os, sys\nsys.exit(0 if os.path.isdir("tests/ci") else 1)\n' > "$T/tests/b/test_script.py"
printf 'import os, sys\ndef test_db(tmp): pass\nif __name__ == "__main__":\n    sys.exit(0 if os.path.isdir("tests/ci") else 1)\n' > "$T/tests/b/test_main.py"
printf '# why\ntests/a/test_listed.sh needs an Amiga\n' > "$T/tests/ci/skip.txt"
printf '# extra\nmake-test @ src: test -f tests/run.sh\n' > "$T/tests/ci/extra.txt"
out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh")
echo "$out" | grep -qx 'REPO_TESTS pass=7 fail=0 skip=2 untouched=0' || { echo "FAIL all-good tree: $out"; exit 1; }
echo "$out" | grep -q 'helper.sh\|test_theirs' && { echo "FAIL ran a non-test or vendor's: $out"; exit 1; }
echo "$out" | grep -qx 'PASS extra:make-test' || { echo "FAIL extra not run: $out"; exit 1; }
printf 'import unittest\nclass T(unittest.TestCase):\n    def test_bad(self): self.fail("broken")\n' > "$T/tests/b/test_bad.py"
if out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh"); then echo "FAIL a failing test passed: $out"; exit 1; fi
echo "$out" | grep -q '^FAIL tests/b/test_bad.py' || { echo "FAIL not named: $out"; exit 1; }
echo "$out" | grep -q 'broken' || { echo "FAIL no log tail: $out"; exit 1; }
rm "$T/tests/b/test_bad.py"
if command -v timeout >/dev/null 2>&1; then
    printf 'sleep 5\n' > "$T/tests/a/test_slow.sh"
    if out=$(TEST_TIMEOUT=1 OUT=$T/out sh "$T/tests/ci/test_all.sh"); then echo "FAIL a slow test passed: $out"; exit 1; fi
    echo "$out" | grep -q '^FAIL tests/a/test_slow.sh (timed out' || { echo "FAIL timeout not named: $out"; exit 1; }
fi
# targeted: only what the change touches (TEST_BASE)
if command -v git >/dev/null 2>&1; then
    rm -f "$T/tests/a/test_slow.sh"
    printf 'grep -q widget lib/x/widget.c\n' > "$T/lib/x/tests/run.sh"
    printf '# uses gadget.h\nexit 0\n' > "$T/tests/a/test_gadget.sh"
    mkdir -p "$T/src"; printf 'x\n' > "$T/src/gadget.h"; printf 'widget\n' > "$T/lib/x/widget.c"
    G="git -C $T -c user.name=t -c user.email=t@t"
    $G init -q && $G add -A && $G commit -qm one
    printf 'widget 2\n' > "$T/lib/x/widget.c"; $G commit -qam two
    out=$(TEST_BASE=HEAD~1 OUT=$T/out sh "$T/tests/ci/test_all.sh")
    echo "$out" | grep -qx 'REPO_TESTS pass=1 fail=0 skip=0 untouched=9' || { echo "FAIL lib/x change: $out"; exit 1; }
    echo "$out" | grep -qx 'PASS lib/x/tests/run.sh' || { echo "FAIL lib/x test not run: $out"; exit 1; }
    printf 'y\n' > "$T/src/gadget.h"; $G commit -qam three
    out=$(TEST_BASE=HEAD~1 OUT=$T/out sh "$T/tests/ci/test_all.sh")
    echo "$out" | grep -qx 'PASS tests/a/test_gadget.sh' || { echo "FAIL top test naming gadget not run: $out"; exit 1; }
    echo "$out" | grep -qx 'PASS extra:make-test' || { echo "FAIL extra @ src not run: $out"; exit 1; }
    echo "$out" | grep -q 'tests/run.sh$' && { echo "FAIL untouched top test ran: $out"; exit 1; }
    out=$(TEST_SCOPE=all TEST_BASE=HEAD~1 OUT=$T/out sh "$T/tests/ci/test_all.sh")
    echo "$out" | grep -q 'untouched=0$' || { echo "FAIL TEST_SCOPE=all: $out"; exit 1; }
    printf '# note\n' >> "$T/tests/ci/skip.txt"; $G commit -qam four
    out=$(TEST_BASE=HEAD~1 OUT=$T/out sh "$T/tests/ci/test_all.sh")
    echo "$out" | grep -q 'untouched=0$' || { echo "FAIL a tests/ci change didn't run everything: $out"; exit 1; }
fi
echo "PASS ci runner: finds, fails, skips, times out, runs only what a change touches"
