# guest.mk - core.wbx: the translated game and the core, built with miniBox's musl guest toolchain
# (a C core: build/meson-linux is enough, build/meson-cpp works too) and linked by the guest kit's recipe.
#
# Usage: make -f guest.mk -j$(nproc) [MB=<miniBox checkout>]
.DEFAULT_GOAL := all
include sources.mk
B := $(ROOT)/build/guest
MBUILD := $(or $(wildcard $(MB)/build/meson-cpp),$(MB)/build/meson-linux)
SR := $(MBUILD)/guest-sysroot
GFLAGS := -specs $(SR)/lib/musl-gcc.specs -fvisibility=hidden -mcmodel=large -mno-red-zone \
	-mstack-protector-guard=global -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DCHIMERA_GUEST \
	-I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
XL_CFLAGS := $(XL_CFLAGS_COMMON) $(GFLAGS)
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) $(GFLAGS)
$(call flags_stamp,$(B),$(XL_CFLAGS) | $(CORE_CFLAGS))

XL_OBJS = $(patsubst $(XL)/%.c,$(B)/xl/%.o,$(XL_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_NAMES))) $(B)/core/xlat_rt.o $(B)/core/lang_data.o

all: $(XLSTAMP)
	$(MAKE) -f guest.mk $(B)/core.wbx

$(B)/xl/%.o: $(XL)/%.c $(ROOT)/xlat/xlat.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(XL_CFLAGS) -c -o $@ $<
$(B)/core/%.o: %.c $(CORE_HDRS) $(XLSTAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<
$(B)/core/xlat_rt.o: $(ROOT)/xlat/xlat_rt.c $(ROOT)/xlat/xlat.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<
$(B)/core/lang_data.o: $(GEN)/lang_data.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 $(GFLAGS) -c -o $@ $<
$(B)/core.wbx: $(CORE_OBJS) $(XL_OBJS)
	gcc -specs $(SR)/lib/musl-gcc.specs -o $@ $^ -static -no-pie -Wl,-O2 -Wl,--no-relax -Wl,-z,stack-size=8388608 \
	  -T $(MB)/source/guest/linkscript.T $(MBUILD)/source/guest/emulibc.c.o -L$(SR)/lib -lgcc -lc
	sh $(MB)/source/guest/check-wbx.sh $@

clean:
	rm -rf $(B)
.PHONY: all clean
