#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Write an LHA archive (level 2 headers, files stored: -lh0-), which LhA,
lha and lhasa on the Amiga and the PC all extract. The PC's lha is often
lhasa, which only reads archives: hence this.

    tools/lha.py OUT.lha DIR      every file and folder under DIR, as DIR/...

Times are SOURCE_DATE_EPOCH when it is set, so the same files give the same
archive.
"""
import os, struct, sys, time


def crc16(data, crc=0):
    """CRC-16/ARC, as LHA uses it"""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def ext(kind, data):
    return bytes([kind]) + data


def header(name, dirname, data, mtime, is_dir):
    exts = [ext(0x00, b"\0\0")]                       # the header's CRC, filled in below
    if not is_dir:
        exts.append(ext(0x01, name.encode("latin-1")))
    if dirname:
        exts.append(ext(0x02, dirname.encode("latin-1").replace(b"/", b"\xff") + b"\xff"))
    body = b""
    sizes = [len(e) + 2 for e in exts]                 # each with the next one's size field
    for i, e in enumerate(exts):
        body += e + struct.pack("<H", sizes[i + 1] if i + 1 < len(exts) else 0)
    fixed = struct.pack("<H5sIIIBBHB", 0, b"-lhd-" if is_dir else b"-lh0-", len(data), len(data),
                        mtime, 0x20, 2, crc16(data), ord("A")) + struct.pack("<H", sizes[0])
    h = bytearray(fixed + body)
    struct.pack_into("<H", h, 0, len(h))
    hcrc = crc16(h)
    struct.pack_into("<H", h, len(fixed) + 1, hcrc)    # the common header's CRC
    return bytes(h)


def main():
    out, top = sys.argv[1], sys.argv[2]
    mtime = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
    base = os.path.dirname(os.path.abspath(top))
    arc = bytearray()
    for root, dirs, files in os.walk(top):
        dirs.sort()
        rel = os.path.relpath(root, base).replace(os.sep, "/")
        arc += header("", rel, b"", mtime, True)
        for f in sorted(files):
            data = open(os.path.join(root, f), "rb").read()
            arc += header(f, rel, data, mtime, False) + data
    arc += b"\0"
    open(out, "wb").write(arc)


main()
