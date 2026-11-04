# win-x86 offline online layer (online32.h).
#   make         build $(OUT)/online32.o
#   make test    run tests/test_online32 (guest ABI end to end, no game needed)
#
# The game links online32.o (build_shims.sh puts it in win32_shims.a) and calls on32_install.
OUT      ?= build
root     := ../..
runtime  := $(root)/tools/ws7/recomp/runtime
CC       := clang -arch arm64
CFLAGS   := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DRC_GUEST32 -I$(runtime) -I../shims -I.

all: $(OUT)/online32.o

$(OUT)/online32.o: online32.c online32.h ../shims/win32_guestobj.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/win32_guestobj.o: ../shims/win32_guestobj.c ../shims/win32_guestobj.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT):
	mkdir -p $@

RT := rc32_mem rc32_teb  # no image registry: the test has its own rc32_lookup
$(OUT)/rt_%.o: $(runtime)/%.c | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/test_online32: tests/test_online32.c $(OUT)/online32.o $(OUT)/win32_guestobj.o $(RT:%=$(OUT)/rt_%.o)
	$(CC) -std=gnu11 $(CFLAGS) $^ -o $@

test: $(OUT)/test_online32
	$(OUT)/test_online32

clean:
	rm -rf $(OUT)

.PHONY: all test clean
