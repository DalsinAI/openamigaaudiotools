#!/bin/sh
# The Amiga programs (ogg2iff mp32iff wav2iff flac2iff mod2iff x2iff, 68040
# + FPU) and libxmp for the 68040 and the 68000, as the kitchen's stove
# builds call a repository, with the PC's tests first.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   STOVE     compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16-open9)
#   OUT       where the programs go (default ./build-amiga)
#   OPENNDK   the OpenNDK headers (default: the stove's own, else make's default)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open9"}
OUT=${OUT:-"$HERE/build-amiga"}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
[ -x "$CC" ] || CC=$(command -v m68k-amigaos-gcc)
if [ -z "${OPENNDK:-}" ]; then
    for d in "$STOVE/prefix/m68k-amigaos/ndk-include" "$STOVE/prefix/m68k-amigaos/ndk/include" \
             "$STOVE/prefix/m68k-amigaos/include"; do
        if [ -f "$d/proto/exec.h" ]; then OPENNDK=$d; break; fi
    done
fi
echo "compiler: $CC ($("$CC" --version | head -1))"
echo "headers:  ${OPENNDK:-make's default}"
cd "$HERE"
make host test
make amiga AMIGA_CC="$CC" ${OPENNDK:+OPENNDK="$OPENNDK"}
mkdir -p "$OUT"
cp build/amiga/ogg2iff build/amiga/mp32iff build/amiga/wav2iff build/amiga/flac2iff \
   build/amiga/mod2iff build/amiga/x2iff build/amiga/libxmp.a "$OUT/"
cp build/amiga-000/libxmp.a "$OUT/libxmp-68000.a"
ls -l "$OUT"
