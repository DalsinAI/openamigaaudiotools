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
printf '# extra\nmake-test: test -f tests/run.sh\n' > "$T/tests/ci/extra.txt"
out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh")
echo "$out" | grep -qx 'REPO_TESTS pass=7 fail=0 skip=2' || { echo "FAIL all-good tree: $out"; exit 1; }
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
echo "PASS ci runner: finds, fails, skips, times out"
