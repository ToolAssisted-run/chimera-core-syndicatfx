# native.mk - the native reference: the same translated game and core sources as guest.mk, built for the
# host, plus the two harnesses (run-native drives the exports directly; run-wbx drives core.wbx through
# the miniBox host exactly as the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc) [MB=<miniBox checkout>] [PY=<python with capstone and pyelftools>]
.DEFAULT_GOAL := all
include sources.mk
B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
XL_CFLAGS := $(XL_CFLAGS_COMMON)
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) $(MBINCS)
$(call flags_stamp,$(B),$(XL_CFLAGS) | $(CORE_CFLAGS))

XL_OBJS = $(patsubst $(XL)/%.c,$(B)/xl/%.o,$(XL_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_NAMES))) $(B)/core/xlat_rt.o $(B)/core/lang_data.o

all: $(XLSTAMP)
	$(MAKE) -f native.mk $(B)/run-native $(B)/run-wbx $(B)/sfx-run

$(B)/xl/%.o: $(XL)/%.c $(ROOT)/xlat/xlat.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(XL_CFLAGS) -c -o $@ $<
$(B)/core/%.o: %.c $(CORE_HDRS) $(XLSTAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<
$(B)/core/xlat_rt.o: $(ROOT)/xlat/xlat_rt.c $(ROOT)/xlat/xlat.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<
$(B)/core/lang_data.o: $(GEN)/lang_data.c
	@mkdir -p $(dir $@)
	gcc -O2 -c -o $@ $<
$(B)/core/run-native.o: run-native.c gate-harness.h sfx-tables.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<
$(B)/run-native: $(CORE_OBJS) $(XL_OBJS) $(B)/core/run-native.o
	gcc -o $@ $^ -lm

# sfx-run: the machine alone, for the oracle comparisons (tests/oracle/README.md)
$(B)/core/sfx-run.o: $(ROOT)/tests/sfx-run.c sfx-machine.h $(ROOT)/xlat/xlat.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall $(DEFS) -c -o $@ $<
$(B)/sfx-run: $(B)/core/sfx-run.o $(B)/core/sfx-machine.o $(B)/core/coro.o $(B)/core/xlat_rt.o $(XL_OBJS)
	gcc -o $@ $^ -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c gate-harness.h sfx-tables.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)
.PHONY: all clean
