# Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
#   make            the tools for the PC (build/host) and the Amiga (build/amiga),
#                   libxmp for the Amiga, then the tests
#   make host       ogg2iff mp32iff wav2iff flac2iff mod2iff x2iff, for the PC
#   make amiga      the same for AmigaOS 3.x, 68040 + FPU; and libxmp.a for the
#                   68040 + FPU (build/amiga) and the 68000 (build/amiga-000)
#   make test       the tests, against build/host
#   make release    the archives in build/release (tools/release.py)
#
# Third-party code is fetched at build time at the commits UPSTREAM.json
# pins, and checked against its sha256 (tools/fetch.py); none is committed.
# AMIGA_CC picks the Amiga compiler; default m68k-amigaos-gcc on PATH, else
# the Cradle's GFOpenAmigaGCC 16.2.

UP      := build/upstream
XMP     := $(UP)/libxmp-4.7.3
TOOLS   := ogg2iff mp32iff wav2iff flac2iff mod2iff x2iff
HOST_CC ?= cc
CRADLE_CC := $(HOME)/CradleLab/toolchain/openamigagcc-16.2.0-open4-x86_64-linux/bin/m68k-amigaos-gcc
AMIGA_CC ?= $(shell command -v m68k-amigaos-gcc 2>/dev/null || echo $(CRADLE_CC))
AMIGA_AR ?= $(patsubst %gcc,%ar,$(AMIGA_CC))

# -fwrapv: dr_flac's prediction overflows a signed int on a damaged file
HOST_CFLAGS := -O2 -DNDEBUG -Wall -fwrapv
# GCC 16 for the Amiga, as the Team builds everything else:
# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga;
# -fno-tree-loop-distribute-patterns: GCC 16's memmove-as-forward-copy.
AMIGA_BASE  := -O2 -fomit-frame-pointer -mno-bitfield -DNDEBUG -noixemul \
               -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns
AMIGA_CPU   ?= -m68040 -m68881
OPENNDK ?= $(HOME)/CradleLab/gfopensdk/OpenNDK/Include_H
AMIGA_CFLAGS := $(AMIGA_BASE) $(AMIGA_CPU)

D_ogg2iff  := -DX2IFF_OGG
D_mp32iff  := -DX2IFF_MP3
D_wav2iff  := -DX2IFF_WAV
D_flac2iff := -DX2IFF_FLAC
D_mod2iff  := -DX2IFF_XMP
D_x2iff    := -DX2IFF_OGG -DX2IFF_MP3 -DX2IFF_WAV -DX2IFF_FLAC -DX2IFF_XMP

.PHONY: all host amiga test release fetch clean
all: host amiga test

fetch: $(UP)/.fetched
$(UP)/.fetched: UPSTREAM.json tools/fetch.py $(wildcard patches/*.patch)
	rm -rf $(UP)/patched
	python3 tools/fetch.py $(UP)
	touch $@

# libxmp's own lists of its sources. In a fresh tree they come with the
# fetch: make fetches, finds them made and starts again with them read.
XMP_LISTS := $(XMP)/src/Makefile $(XMP)/src/loaders/Makefile $(XMP)/src/loaders/prowizard/Makefile \
             $(XMP)/src/depackers/Makefile $(XMP)/src/depackers/lhasa/Makefile
$(XMP_LISTS): $(UP)/.fetched
	@test -f $@
-include $(XMP)/src/Makefile
-include $(XMP)/src/loaders/Makefile
-include $(XMP)/src/loaders/prowizard/Makefile
-include $(XMP)/src/depackers/Makefile
-include $(XMP)/src/depackers/lhasa/Makefile
.DEFAULT_GOAL := all
XMP_SRCS := $(OBJS:.o=.c) $(PROWIZARD_OBJS:.o=.c) $(DEPACKER_OBJS:.o=.c)
XMP_DEFS := -DLIBXMP_STATIC -DHAVE_POPEN=0 -I$(XMP)/include -I$(XMP)/src

# $(call xmp_lib,DIR,CC,AR,CFLAGS)
define xmp_lib
$(1)/libxmp.a: $(UP)/.fetched
	@test -n "$$(strip $$(XMP_SRCS))" || { echo "libxmp's source lists weren't read"; exit 1; }
	@mkdir -p $(1)/xmp
	cd $(1)/xmp && for f in $$(XMP_SRCS); do o=$$$$(echo $$$$f | tr / _).o; \
	  $(2) $(4) -w $$(patsubst -I%,-I$(CURDIR)/%,$$(XMP_DEFS)) -c $(CURDIR)/$(XMP)/$$$$f -o $$$$o || exit 1; done
	rm -f $$@ && $(3) rcs $$@ $(1)/xmp/*.o
endef
$(eval $(call xmp_lib,build/host,$(HOST_CC),ar,$(HOST_CFLAGS)))
$(eval $(call xmp_lib,build/amiga,$(AMIGA_CC),$(AMIGA_AR),$(AMIGA_CFLAGS) -idirafter $(OPENNDK)))
$(eval $(call xmp_lib,build/amiga-000,$(AMIGA_CC),$(AMIGA_AR),$(AMIGA_BASE) -m68000 -idirafter $(OPENNDK)))

SRC := src/x2iff.c src/sources.c src/x2iff.h
# $(call tool,DIR,CC,CFLAGS,NAME)
define tool
$(1)/$(4): $(SRC) $(UP)/.fetched $(if $(findstring XMP,$(D_$(4))),$(1)/libxmp.a)
	@mkdir -p $(1)
	$(2) $(3) -DX2IFF_NAME='"$(4)"' $(D_$(4)) -I$(UP)/patched -I$(UP) -I$(XMP)/include -o $$@ \
	  src/x2iff.c src/sources.c $(if $(findstring XMP,$(D_$(4))),$(1)/libxmp.a) -lm
endef
$(foreach t,$(TOOLS),$(eval $(call tool,build/host,$(HOST_CC),$(HOST_CFLAGS),$(t))))
# The Amiga: the same converter over datatypes (src/dtio.c), no decoders;
# the programs differ only in their names.
define amiga_tool
build/amiga/$(1): src/x2iff.c src/dtio.c src/x2iff.h
	@mkdir -p build/amiga
	$(AMIGA_CC) $(AMIGA_CFLAGS) -Wall -idirafter $(OPENNDK) -DX2IFF_NAME='"$(1)"' -o $$@ \
	  src/x2iff.c src/dtio.c -lamiga -lm
endef
$(foreach t,$(TOOLS),$(eval $(call amiga_tool,$(t))))

host: $(addprefix build/host/,$(TOOLS))
amiga: $(addprefix build/amiga/,$(TOOLS)) build/amiga/libxmp.a build/amiga-000/libxmp.a
	python3 tools/fpcr_check.py $(patsubst %gcc,%objdump,$(AMIGA_CC)) $(addprefix build/amiga/,$(TOOLS)) build/amiga/xmp/*.o

test: host
	python3 tests/check.py build/host build/test

release: host amiga
	python3 tools/release.py

clean:
	rm -rf build/host build/amiga build/amiga-000 build/test build/release
