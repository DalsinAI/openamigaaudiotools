#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""The release archives, into build/release (make release builds first):

    OpenAmigaAudioTools-V-amigaos-m68k.lha   the tools for AmigaOS 3.x (68040 + FPU)
    openamigaaudiotools-V-linux-ARCH.tar.gz   the tools for Linux
    libxmp-4.7.3-amigaos-m68k.lha, .tar.gz    libxmp for AmigaOS 3.x: 68040 + FPU and 68000
    SHA256SUMS

The Amiga tools hold no third-party code (they read and save through
GFOpenDatatypes' datatypes); the Linux tools and libxmp carry every notice
in Licences/. File times are the last commit's (SOURCE_DATE_EPOCH), so the
same tree gives the same archives.
"""
import hashlib, io, json, os, platform, re, shutil, subprocess, sys, tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "build")
OUT = os.path.join(B, "release")
UP = os.path.join(B, "upstream")
TOOLS = ["ogg2iff", "mp32iff", "wav2iff", "flac2iff", "mod2iff", "x2iff"]
VERSION = open(os.path.join(ROOT, "VERSION")).read().strip()
XMP = json.load(open(os.path.join(ROOT, "UPSTREAM.json")))["libxmp"]


def epoch():
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"])
    r = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%ct"], capture_output=True, text=True)
    return int(r.stdout.strip() or 0)


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)


def comment_head(path):
    """the first comment block that names a copyright or the public domain"""
    text = open(path, encoding="latin-1").read()
    for m in re.finditer(r"/\*.*?\*/|(?://[^\n]*\n)+", text, re.S):
        c = m.group(0)
        if re.search(r"copyright|public domain|permission", c, re.I):
            return c.strip()
    return None


def bundled_notices(xdir):
    """libxmp's own sources from others, each notice once"""
    seen, out = set(), []
    files = []
    for sub in ("src/depackers", "src/depackers/lhasa", "src/loaders/prowizard"):
        files += sorted(os.path.join(xdir, sub, f) for f in os.listdir(os.path.join(xdir, sub)) if f.endswith(".c"))
    files += [os.path.join(xdir, "src/miniz_tinfl.c"), os.path.join(xdir, "src/loaders/vorbis.c")]
    for f in files:
        c = comment_head(f)
        if c and c not in seen:
            seen.add(c)
            out.append("== %s\n%s\n" % (os.path.relpath(f, xdir), c))
    return "\n".join(out)


def targz(top, out, mtime):
    with tarfile.open(out, "w:gz", compresslevel=9) as t:
        for root, dirs, files in os.walk(top):
            dirs.sort()
            for name in [""] + sorted(files):
                p = os.path.join(root, name) if name else root
                ti = t.gettarinfo(p, os.path.relpath(p, os.path.dirname(top)))
                ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname = mtime, 0, 0, "", ""
                if ti.isfile():
                    with open(p, "rb") as f:
                        t.addfile(ti, f)
                else:
                    t.addfile(ti)


def lha(top, out, mtime):
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(mtime))
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/lha.py"), out, top], check=True, env=env)
    if shutil.which("lha"):                   # lhasa reads it back, CRCs and all
        subprocess.run(["lha", "tq", out], check=True)


def main():
    mtime = epoch()
    shutil.rmtree(OUT, ignore_errors=True)
    stage = os.path.join(OUT, "stage")
    xdir = os.path.join(UP, XMP["dir"])

    # the Amiga tools
    a = os.path.join(stage, "OpenAmigaAudioTools")
    for t in TOOLS:
        copy(os.path.join(B, "amiga", t), os.path.join(a, "C", t))
    copy(os.path.join(ROOT, "docs/OpenAmigaAudioTools.readme"), os.path.join(a, "OpenAmigaAudioTools.readme"))
    copy(os.path.join(ROOT, "LICENSE"), os.path.join(a, "LICENSE"))
    lha(a, os.path.join(OUT, "OpenAmigaAudioTools-%s-amigaos-m68k.lha" % VERSION), mtime)

    # the Linux tools
    arch = platform.machine()
    l = os.path.join(stage, "openamigaaudiotools-%s-linux-%s" % (VERSION, arch))
    for t in TOOLS:
        copy(os.path.join(B, "host", t), os.path.join(l, "bin", t))
    copy(os.path.join(ROOT, "README.md"), os.path.join(l, "README.md"))
    copy(os.path.join(ROOT, "LICENSE"), os.path.join(l, "LICENSE"))
    lic = os.path.join(l, "Licences")
    os.makedirs(lic)
    up = json.load(open(os.path.join(ROOT, "UPSTREAM.json")))
    with open(os.path.join(lic, "README"), "w") as f:
        f.write("What in the Linux tools comes from others, and its licence.\n\n")
        for name, p in up.items():
            f.write("- %s %s: %s\n    %s\n" % (name, p["version"], p["used_for"], p["licence"]))
    for name in ("stb_vorbis.c", "dr_mp3.h", "dr_wav.h", "dr_flac.h"):
        text = open(os.path.join(UP, name), encoding="latin-1").read()
        i = text.rfind("This software is available")    # the licence statement at the end
        if i < 0:
            sys.exit("release: no licence statement found in " + name)
        head = text[:text.find("\n", text.find("\n") + 1) + 1]   # its first lines: what and who
        open(os.path.join(lic, name.split(".")[0] + ".txt"), "w").write(head + "\n" + text[i:])
    copy(os.path.join(xdir, "docs/COPYING"), os.path.join(lic, "libxmp.txt"))
    open(os.path.join(lic, "libxmp-bundled.txt"), "w").write(bundled_notices(xdir))
    targz(l, os.path.join(OUT, os.path.basename(l) + ".tar.gz"), mtime)

    # libxmp for the Amiga
    x = os.path.join(stage, "libxmp-%s-amigaos-m68k" % XMP["version"])
    copy(os.path.join(xdir, "include/xmp.h"), os.path.join(x, "include/xmp.h"))
    copy(os.path.join(B, "amiga/libxmp.a"), os.path.join(x, "lib/libxmp.a"))
    copy(os.path.join(B, "amiga-000/libxmp.a"), os.path.join(x, "lib/68000/libxmp.a"))
    for sub, cpu in (("lib", "-m68040 -m68881"), ("lib/68000", "-m68000")):
        up_rel = "../.." if sub == "lib" else "../../.."
        pc = ("prefix=${pcfiledir}/%s\nincludedir=${prefix}/include\nlibdir=${prefix}/%s\n\n"
              "Name: libxmp\nDescription: Extended Module Player library, for AmigaOS 3.x (%s, libnix)\n"
              "Version: %s\nCflags: -I${includedir} -DLIBXMP_STATIC\nLibs: -L${libdir} -lxmp -lm\n"
              % (up_rel, sub, cpu, XMP["version"]))
        os.makedirs(os.path.join(x, sub, "pkgconfig"), exist_ok=True)
        open(os.path.join(x, sub, "pkgconfig/libxmp.pc"), "w").write(pc)
    copy(os.path.join(ROOT, "examples/xmp-render.c"), os.path.join(x, "examples/xmp-render.c"))
    copy(os.path.join(ROOT, "docs/libxmp-amiga.readme"), os.path.join(x, "libxmp-amiga.readme"))
    for f in ("README", "docs/COPYING", "docs/CREDITS", "docs/Changelog", "docs/libxmp.html", "docs/formats.txt"):
        copy(os.path.join(xdir, f), os.path.join(x, "docs" if f.startswith("docs/") else "", os.path.basename(f)))
    open(os.path.join(x, "docs/bundled-notices.txt"), "w").write(bundled_notices(xdir))
    lha(x, os.path.join(OUT, os.path.basename(x) + ".lha"), mtime)
    targz(x, os.path.join(OUT, os.path.basename(x) + ".tar.gz"), mtime)

    shutil.rmtree(stage)
    sums = []
    for f in sorted(os.listdir(OUT)):
        sums.append("%s  %s" % (hashlib.sha256(open(os.path.join(OUT, f), "rb").read()).hexdigest(), f))
    open(os.path.join(OUT, "SHA256SUMS"), "w").write("\n".join(sums) + "\n")
    for f in sorted(os.listdir(OUT)):
        print("%-50s %9d" % (f, os.path.getsize(os.path.join(OUT, f))))


main()
