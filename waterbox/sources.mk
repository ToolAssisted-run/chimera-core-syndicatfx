# sources.mk - what native.mk and guest.mk both build: the same files with the same defines, so the
# native reference and the sandboxed core are the same program. Included, not run.
#
# The game is SyndicatFX (extern/syndicatfx, patched by patches/): the original Syndicate code as i386
# assembly plus its C library layer. It is built as one static i386 ELF (../i386, against musl and the
# core's SDL2 shim) and translated to portable C (../xlat/elf2c.py) over a 32-bit arena: build/xlat.
ROOT := ..
MB   ?= $(or $(MINIBOX_DIR),$(HOME)/chimera/extern/chimera-common-minibox)
PY   ?= python3
XL   := $(ROOT)/build/xlat
GEN  := $(ROOT)/build/gen

# ---- the patch series goes onto the submodule before anything of SyndicatFX builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) $(ROOT)/tools/apply-patches.sh
	sh $(ROOT)/tools/apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# ---- the i386 program and its translation (../tools/setup-i386-toolchain.sh provides the toolchain)
ELF := $(ROOT)/build/i386/syndicatfx.elf
LANGS := $(ROOT)/build/i386/language/eng/guitext.dat $(ROOT)/build/i386/language/fre/guitext.dat $(ROOT)/build/i386/language/ita/guitext.dat
$(ELF) $(LANGS): $(PATCH_STAMP) $(wildcard $(ROOT)/i386/*.c $(ROOT)/i386/*.cpp $(ROOT)/i386/*.h $(ROOT)/i386/Makefile)
	sh $(ROOT)/tools/setup-i386-toolchain.sh
	$(MAKE) -s -C $(ROOT)/i386 PY="$(PY)" > /dev/null
XLSTAMP := $(XL)/xl_table.c
$(XLSTAMP): $(ELF) $(ROOT)/xlat/elf2c.py
	PY="$(PY)" sh $(ROOT)/tools/translate.sh
XL_SRCS = $(sort $(wildcard $(XL)/xl_code_*.c)) $(XL)/xl_table.c $(XL)/xl_image.c
$(GEN)/lang_data.c: $(LANGS) $(ROOT)/tools/bin2c.py
	@mkdir -p $(GEN)
	$(PY) $(ROOT)/tools/bin2c.py $@ sfx_lang_eng=$(word 1,$(LANGS)) sfx_lang_fre=$(word 2,$(LANGS)) sfx_lang_ita=$(word 3,$(LANGS))

# ---- the core
CORE_NAMES := syndicatfx-driver sfx-machine game-state coro sha1 wbx-entry opl3
CORE_HDRS := syndicatfx-driver.h sfx-machine.h game-state.h coro.h sha1.h sfx-tables.h opl3.h ../i386/hostcall.h ../xlat/xlat.h
DEFS := -DXL_ARENA_BITS=26 -I. -I$(ROOT)/xlat -I$(XL)
# the translated code is generated: its warnings are the translator's business, not the build's
XL_CFLAGS_COMMON := -std=gnu11 -O2 -w $(DEFS)
CORE_CFLAGS_COMMON := -std=gnu11 -O2 $(DEFS) -Wall -Wno-unused-function

# Every object depends on the flags it was built with: a change to them rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
