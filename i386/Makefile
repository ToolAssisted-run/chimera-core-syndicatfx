# Builds the game as ONE static, non-PIE i386 ELF with relocations kept: the translator's input.
# make -C i386 [O=build/i386]   (needs tools/setup-i386-toolchain.sh first)
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
O    ?= $(ROOT)/build/i386
TC   := $(ROOT)/build/i386tc
SYN  := $(ROOT)/extern/syndicatfx
SDLI := $(TC)/src/SDL2-2.30.12/include
ALI  := $(TC)/src/openal-soft-1.23.1/include
PY   ?= python3

ARCH   := -m32 -march=i686 -mno-sse -mno-mmx -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(ROOT)/=
INC    := -I$(ROOT)/i386/config -I$(ROOT)/i386 -I$(SYN)/src -I$(SYN)/bflibrary/include -I$(SYN)/bfsoundlib/include -I$(O)
SYSINC := -nostdinc -isystem $(TC)/musl/include -isystem $(SDLI) -isystem $(ALI) -isystem $(ROOT)/i386/stubinc
DEFS   := -DHAVE_CONFIG_H -DSYNDICATFX_CORE
CFLAGS := $(ARCH) -O2 -g0 -w $(DEFS) $(INC) $(SYSINC)
CXXFLAGS := $(ARCH) -O2 -g0 -w -fno-exceptions -fno-rtti -fno-threadsafe-statics -nostdinc++ -isystem $(ROOT)/i386/shimcxx $(DEFS) $(INC) $(SYSINC)
LDFLAGS := -m32 -static -nostdlib -no-pie -Wl,--emit-relocs -Wl,-Ttext-segment=0x100000 -Wl,--build-id=none -Wl,--gc-sections

GAME_C   := $(filter-out %/oswindws.c,$(wildcard $(SYN)/src/*.c))
# bflibrary: its own source list (Makefile.am), minus gpng.c (PNG screenshots: stubbed, no libpng in the core)
BFL_C    := $(filter-out %/gpng.c %_s.c,$(wildcard $(SYN)/bflibrary/src/general/*.c)) $(wildcard $(SYN)/bflibrary/src/x86-win-sdl2/*.c)
BFL_CXX  := $(wildcard $(SYN)/bflibrary/src/general/*.cpp) $(wildcard $(SYN)/bflibrary/src/x86-win-sdl2/*.cpp)
SHIM_C   := $(ROOT)/i386/sdlshim.c $(ROOT)/i386/sound_stub.c $(ROOT)/i386/audio_stubs.c $(ROOT)/i386/oal.c $(ROOT)/i386/ail32.c $(ROOT)/i386/corehooks.c
BSND_C   := $(addprefix $(SYN)/bfsoundlib/src/,ail.c aila.c aildebug.c ailss.c ailvoc.c ailwav.c bfaudio.c cd.c dllload.c dpmi.c drv_oal.c init_mus.c init_snd.c memfile.c miscutil.c mseqnfad.c mseqnlst.c mseqnply.c mssdig.c msssys.c mssxdig.c mssxmidi.c oggvorbis.c sb16.c awe32.c awe32use.c snderr.c sndtimer.c ssampfad.c ssamplst.c ssampply.c streamfx.c variable.c)
SHIM_CXX := $(ROOT)/i386/cxxrt.cpp
WRAPPERS := wrappers_dos wrappers_game wrappers_libc wrappers_util

OBJS := $(patsubst $(SYN)/%.c,$(O)/%.o,$(GAME_C) $(BFL_C) $(BSND_C)) \
        $(patsubst $(SYN)/%.cpp,$(O)/%.o,$(BFL_CXX)) \
        $(patsubst $(ROOT)/i386/%.c,$(O)/shim/%.o,$(SHIM_C)) \
        $(patsubst $(ROOT)/i386/%.cpp,$(O)/shim/%.o,$(SHIM_CXX)) \
        $(O)/src/syndre.o $(WRAPPERS:%=$(O)/src/%.o)

all: $(O)/syndicatfx.elf $(O)/language/eng/guitext.dat $(O)/language/fre/guitext.dat $(O)/language/ita/guitext.dat

$(O)/syndicatfx.elf: $(OBJS)
	gcc $(LDFLAGS) $(TC)/musl/lib/crt1.o $(TC)/musl/lib/crti.o $(OBJS) \
	  $(TC)/musl/lib/libc.a $(TC)/gcc-lib/libgcc.a $(TC)/musl/lib/crtn.o -o $@

$(O)/src/%.o: $(SYN)/src/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -include $(ROOT)/i386/config/config.h -c $< -o $@
$(O)/bfsoundlib/%.o: $(SYN)/bfsoundlib/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -include $(ROOT)/i386/config/bsndconf.h -I$(SYN)/bfsoundlib/include -c $< -o $@
$(O)/%.o: $(SYN)/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@
$(O)/%.o: $(SYN)/%.cpp
	@mkdir -p $(dir $@)
	g++ $(CXXFLAGS) -c $< -o $@
$(O)/shim/%.o: $(ROOT)/i386/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@
$(O)/shim/%.o: $(ROOT)/i386/%.cpp
	@mkdir -p $(dir $@)
	g++ $(CXXFLAGS) -c $< -o $@
$(O)/src/syndre.o: $(SYN)/src/syndre.sx
	@mkdir -p $(dir $@)
	gcc $(ARCH) $(DEFS) $(INC) -c $< -o $@
$(O)/src/wrappers_%.sx: $(SYN)/conf/wrappers_%.conf
	@mkdir -p $(dir $@)
	$(PY) $(SYN)/util/mkwrappers -r $(SYN)/conf/fnmap_posix.conf -o $@ $<
$(O)/src/wrappers_%.o: $(O)/src/wrappers_%.sx
	gcc $(ARCH) $(DEFS) $(INC) -c $< -o $@

$(O)/language/eng/guitext.dat: $(SYN)/lang/guitext_eng.pot
	@mkdir -p $(dir $@)
	$(PY) $(SYN)/util/textdat.py -t $(SYN)/conf/char_encoding_tbl_eu.txt -d $@ -c $<
$(O)/language/%/guitext.dat: $(SYN)/lang/guitext_%.po
	@mkdir -p $(dir $@)
	$(PY) $(SYN)/util/textdat.py -t $(SYN)/conf/char_encoding_tbl_eu.txt -d $@ -c $<

clean:
	rm -rf $(O)
.PHONY: all clean
