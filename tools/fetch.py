#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Fetch the third-party sources UPSTREAM.json pins, and check each sha256.

    tools/fetch.py DIR

Nothing third-party is committed: each part is downloaded at its pinned
commit or release into DIR, checked against its sha256 (the build stops on
a mismatch) and, for a tarball, unpacked. A file already in DIR with the
right hash isn't fetched again. UPSTREAM_CACHE=dir is looked in first, for
offline builds. A part with "patches" is copied to DIR/patched/ and the
patches (patches/, the Team's own fixes) applied there with patch -p1;
the fetched file itself stays as pinned.
"""
import hashlib, json, os, shutil, subprocess, sys, tarfile, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 16), b""):
            h.update(b)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pins = json.load(open(os.path.join(ROOT, "UPSTREAM.json")))
    cache = os.environ.get("UPSTREAM_CACHE")
    for name, p in pins.items():
        dst = os.path.join(out, p["file"])
        if not (os.path.isfile(dst) and sha256(dst) == p["sha256"]):
            part = dst + ".part"
            src = cache and os.path.join(cache, p["file"])
            if src and os.path.isfile(src):
                shutil.copyfile(src, part)
            else:
                print("fetch %s %s" % (name, p["url"]))
                with urllib.request.urlopen(p["url"]) as r, open(part, "wb") as f:
                    shutil.copyfileobj(r, f)
            got = sha256(part)
            if got != p["sha256"]:
                os.remove(part)
                sys.exit("%s: sha256 %s, pinned %s" % (name, got, p["sha256"]))
            os.replace(part, dst)
        if "dir" in p and not os.path.isdir(os.path.join(out, p["dir"])):
            with tarfile.open(dst) as t:
                for m in t.getmembers():
                    if m.name.startswith("/") or ".." in m.name.split("/"):
                        sys.exit("%s: unsafe path %s in the tarball" % (name, m.name))
                t.extractall(out)
        if p.get("patches"):
            pd = os.path.join(out, "patched")
            os.makedirs(pd, exist_ok=True)
            shutil.copyfile(dst, os.path.join(pd, p["file"]))
            for pt in p["patches"]:
                with open(os.path.join(ROOT, pt), "rb") as f:
                    subprocess.run(["patch", "-p1", "-s", "-d", pd], stdin=f, check=True)


main()
