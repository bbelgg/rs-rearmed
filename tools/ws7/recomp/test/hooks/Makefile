# Hook points and the mods folder, without a game: the same mod dylib in a 64-bit (Mac) and a 32-bit
# (Learn & Play) runtime.
#   make test OUT=DIR    test_hooks64, test_hooks32 (dispatch and arguments for each convention, what a hook
#                        changes, refusals, names; the cost of a point without hooks), and probe_mod.dylib, the mod
#                        of the game runs (docs/*/TESTING.md)
OUT      ?= build
root     := ../../../../..
recomp   := ../..
runtime  := $(recomp)/runtime
PY       ?= python3
CC       := clang -arch arm64
CFLAGS   := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$(runtime) -I$(recomp)/overrides -I$(root)/shims/include
MOD      := $(CC) -dynamiclib -undefined dynamic_lookup $(CFLAGS)

all: $(OUT)/test_hooks64 $(OUT)/test_hooks32 $(OUT)/mods/hooks_mod.dylib $(OUT)/plain/plain_mod.dylib $(OUT)/probe_mod.dylib

$(OUT)/target/rc_target_names.h: $(root)/tools/targets/targets.py $(root)/tools/targets/win-x86/symbols.map | $(OUT)
	$(PY) -I $(root)/tools/targets/targets.py gen --target win-x86 $(OUT)/target > /dev/null
$(OUT)/mods/hooks_mod.dylib: hooks_mod.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	mkdir -p $(OUT)/mods && $(MOD) $< -o $@
$(OUT)/plain/plain_mod.dylib: plain_mod.c | $(OUT)
	mkdir -p $(OUT)/plain && $(MOD) $< -o $@
$(OUT)/probe_mod.dylib: probe_mod.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	$(MOD) $< -o $@
$(OUT)/test_hooks64: test_hooks.c $(runtime)/rc_hook.c $(runtime)/rc_mods.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	$(CC) $(CFLAGS) test_hooks.c $(runtime)/rc_hook.c $(runtime)/rc_mods.c -o $@
$(OUT)/test_hooks32: test_hooks.c $(OUT)/target/rc_target_names.h $(wildcard $(runtime)/*.c) $(wildcard $(runtime)/rc*.h) | $(OUT)
	$(CC) $(CFLAGS) -DRC_GUEST32 -I$(OUT)/target test_hooks.c $(runtime)/rc_hook.c $(runtime)/rc_mods.c $(runtime)/rc32_hook.c \
	    $(runtime)/rc32_mem.c $(runtime)/rc32_teb.c -o $@
$(OUT):
	mkdir -p $@

test: all
	$(OUT)/test_hooks64 $(OUT)
	$(OUT)/test_hooks32 $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all test clean
