# Hook points and the mods folder, without a game: the same mod dylib in a 64-bit (Mac) and a 32-bit
# (Learn & Play) runtime.
#   make test OUT=DIR    test_hooks64, test_hooks32 (dispatch and arguments for each convention, what a hook
#                        changes, refusals, names; the cost of a point without hooks), and probe_mod.dylib, the mod
#                        of the game runs (docs/*/TESTING.md); test_files (the files a mod serves, runtime/rc_file.h) and
#                        files_mod.dylib, the mod of its game runs; test_framerate (the arithmetic of the frame rate
#                        fixes, overrides/mods/framerate.h); test_fftsetup (the Mac game's FFT setups, kept and
#                        handed out again, overrides/mods/fftsetup.c); test_quit_tables (the quit's teardown of the
#                        JSON tables: freed when empty, kept when a load left entries, overrides/mods/quit_tables.c); test_shared64, test_shared32 (the built-in fixes both
#                        editions share, overrides/mods/mods_shared.c: one object, built as a mod is, on both runtimes)
OUT      ?= build
root     := ../../../../..
recomp   := ../..
runtime  := $(recomp)/runtime
PY       ?= python3
CC       := clang -arch arm64
CFLAGS   := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$(runtime) -I$(recomp)/overrides -I$(root)/shims/include
MOD      := $(CC) -dynamiclib -undefined dynamic_lookup $(CFLAGS)
builtin  := $(recomp)/overrides/mods
SHARED   := $(OUT)/mods_shared.o $(OUT)/framerate.o

all: $(OUT)/test_hooks64 $(OUT)/test_hooks32 $(OUT)/test_files $(OUT)/test_framerate $(OUT)/test_fftsetup $(OUT)/test_quit_tables $(OUT)/test_shared64 $(OUT)/test_shared32 $(OUT)/files_mod.dylib $(OUT)/mods/hooks_mod.dylib $(OUT)/plain/plain_mod.dylib $(OUT)/probe_mod.dylib

$(OUT)/target/rc_target_names.h: $(root)/tools/targets/targets.py $(root)/tools/targets/win-x86/symbols.map | $(OUT)
	$(PY) -I $(root)/tools/targets/targets.py gen --target win-x86 $(OUT)/target > /dev/null
$(OUT)/mods/hooks_mod.dylib: hooks_mod.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	mkdir -p $(OUT)/mods && $(MOD) $< -o $@
$(OUT)/plain/plain_mod.dylib: plain_mod.c | $(OUT)
	mkdir -p $(OUT)/plain && $(MOD) $< -o $@
$(OUT)/probe_mod.dylib: probe_mod.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	$(MOD) $< -o $@
$(OUT)/files_mod.dylib: files_mod.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	$(MOD) $< -o $@
$(OUT)/test_hooks64: test_hooks.c $(runtime)/rc_hook.c $(runtime)/rc_mods.c $(runtime)/rc_mid.c $(wildcard $(runtime)/rc_*.h) | $(OUT)
	$(CC) $(CFLAGS) test_hooks.c $(runtime)/rc_hook.c $(runtime)/rc_mods.c $(runtime)/rc_mid.c -o $@
$(OUT)/test_hooks32: test_hooks.c $(OUT)/target/rc_target_names.h $(wildcard $(runtime)/*.c) $(wildcard $(runtime)/rc*.h) | $(OUT)
	$(CC) $(CFLAGS) -DRC_GUEST32 -I$(OUT)/target test_hooks.c $(runtime)/rc_hook.c $(runtime)/rc_mods.c $(runtime)/rc_mid.c $(runtime)/rc32_hook.c \
	    $(runtime)/rc32_mem.c $(runtime)/rc32_teb.c -o $@
$(OUT)/test_files: test_files.c $(runtime)/rc_file.c $(runtime)/rc_file.h | $(OUT)
	$(CC) $(CFLAGS) test_files.c $(runtime)/rc_file.c -o $@
$(OUT)/test_framerate: test_framerate.c $(recomp)/overrides/mods/framerate.h | $(OUT)
	$(CC) $(CFLAGS) test_framerate.c -o $@
$(OUT)/test_fftsetup: test_fftsetup.c $(builtin)/fftsetup.c $(builtin)/mods_shared.h | $(OUT)
	$(CC) $(CFLAGS) -I$(builtin) test_fftsetup.c $(builtin)/fftsetup.c -o $@
$(OUT)/test_quit_tables: test_quit_tables.c $(builtin)/quit_tables.c | $(OUT)
	$(CC) $(CFLAGS) test_quit_tables.c $(builtin)/quit_tables.c -o $@
# the shared fixes as both apps build them: one object for either runtime (no RC_GUEST32), names at run time
$(OUT)/%.o: $(builtin)/%.c $(wildcard $(builtin)/*.h) $(wildcard $(runtime)/rc_*.h) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/test_shared64: test_mods_shared.c $(SHARED) $(runtime)/rc_hook.c $(runtime)/rc_mid.c
	$(CC) $(CFLAGS) -I$(builtin) test_mods_shared.c $(SHARED) $(runtime)/rc_hook.c $(runtime)/rc_mid.c -o $@
$(OUT)/test_shared32: test_mods_shared.c $(SHARED) $(wildcard $(runtime)/*.c) $(wildcard $(runtime)/rc*.h)
	$(CC) $(CFLAGS) -DRC_GUEST32 -I$(builtin) test_mods_shared.c $(SHARED) $(runtime)/rc_hook.c $(runtime)/rc_mid.c \
	    $(runtime)/rc32_hook.c $(runtime)/rc32_mem.c $(runtime)/rc32_teb.c -o $@
$(OUT):
	mkdir -p $@

test: all
	$(OUT)/test_hooks64 $(OUT)
	$(OUT)/test_hooks32 $(OUT)
	$(OUT)/test_files
	$(OUT)/test_framerate
	$(OUT)/test_fftsetup
	$(OUT)/test_quit_tables
	$(OUT)/test_shared64
	$(OUT)/test_shared32

clean:
	rm -rf $(OUT)

.PHONY: all test clean
