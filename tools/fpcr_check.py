#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence (LICENSE); from gfopenrtg tools/fpcr_check.py.
# SPDX-License-Identifier: MIT
"""fpcr_check.py OBJDUMP FILE...: GCC 16's -m68040 float-to-int conversion
saves FPCR in a data register, sets round-to-zero, stores with
fmove.l fpN,<ea> and puts FPCR back. Sometimes it saves FPCR in the very
register <ea> indexes with, so the value lands in the wrong place (found
8 October 2026 by the SDL 2 helper: tiny_jpeg's JPEGs came out flat grey;
Mesa's feedback.c has one, in update_hit_record). -m68020 -m68881 and -m68060
convert with fintrz and don't do this.

This lists each such store and exits 1 if there is one, so a build can't
ship it. FILE is a program, a library or an object. (The same check as the SDL 2 helper's
library/modules/sdl2/fpcr_check.py, usable from other scripts: scan().)"""
import re
import subprocess
import sys

MEMBER = re.compile(r"^(\S+):\s+file format ")
FUNC = re.compile(r"^[0-9a-f]+ <(.*)>:")
SAVE = re.compile(r"fmovel %?fpcr,%?(d[0-7])")
STORE = re.compile(r"fmovel %?fp[0-7],")


def scan(objdump, *paths):
    """[(file, function, instruction)] for every store through the FPCR register
    in paths (objects and programs; AmigaOS hunk libraries (.a) aren't
    archives objdump can take apart, so scan their objects)."""
    paths = list(paths)
    r = subprocess.run([objdump, "-m", "m68k:68040", "-d"] + paths, capture_output=True, text=True)
    if r.returncode != 0:
        # objdump can crash part-way through a long list (it did at the 20th of
        # Mesa's objects, 8 October 2026), and what it had printed by then
        # looked like a clean scan: split the list and scan the halves.
        if len(paths) == 1:
            raise RuntimeError("fpcr_check: %s couldn't disassemble %s (exit %d)" % (objdump, paths[0], r.returncode))
        half = len(paths) // 2
        return scan(objdump, *paths[:half]) + scan(objdump, *paths[half:])
    out = r.stdout
    found, member, saved, fn = [], paths[0] if paths else "?", None, "?"
    for line in out.splitlines():
        m = MEMBER.match(line)
        if m:
            member, saved, fn = m.group(1), None, "?"
            continue
        m = FUNC.match(line)
        if m:
            fn, saved = m.group(1), None
            continue
        m = SAVE.search(line)
        if m:
            saved = m.group(1)
            continue
        if saved and STORE.search(line):
            ea = line.split("fmovel", 1)[1].split(",", 1)[1]
            if saved in ea:
                found.append((member, fn, line.strip()))
            saved = None
    return found


def main():
    objdump, files = sys.argv[1], sys.argv[2:]
    bad = 0
    for i in range(0, len(files), 200):
        for path, fn, ins in scan(objdump, *files[i:i + 200]):
            print("%s: %s: %s" % (path, fn, ins))
            bad += 1
    print("fpcr_check: %d float-to-int store(s) through the FPCR register" % bad if bad else "fpcr_check: none")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
