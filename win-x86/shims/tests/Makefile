# Tests of the Win32 seam that need no game.
#   make test    run tests/test_ksync (kernel32 waits from host threads), then again with LP_WAKE_ALL=1,
#                tests/test_nofile (the open-file limit) and tests/test_close (CloseHandle against the
#                other handle calls, under the address sanitizer), tests/test_kfile (paths and finds)
OUT      ?= build
root     := ../../..
runtime  := $(root)/tools/ws7/recomp/runtime
CC       := clang -arch arm64
CFLAGS   := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DRC_GUEST32 \
            -I$(runtime) -I..

RT := rc32_mem rc32_teb rc32_image rc32_seh
$(OUT)/rt_%.o: $(runtime)/%.c | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/win32_ksync.o: ../win32_ksync.c ../win32_handles.h ../win32_shims.h | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/win32_guestobj.o: ../win32_guestobj.c ../win32_guestobj.h | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/test_ksync: test_ksync.c $(OUT)/win32_ksync.o $(OUT)/win32_guestobj.o $(RT:%=$(OUT)/rt_%.o)
	$(CC) $(CFLAGS) $^ -o $@
$(OUT)/asan_ksync.o: ../win32_ksync.c ../win32_handles.h ../win32_shims.h | $(OUT)
	$(CC) $(CFLAGS) -fsanitize=address -g -w -c $< -o $@
$(OUT)/test_close: test_close.c $(OUT)/asan_ksync.o $(OUT)/win32_guestobj.o $(RT:%=$(OUT)/rt_%.o)
	$(CC) $(CFLAGS) -fsanitize=address -g $^ -o $@
$(OUT)/win32_kfile.o: ../win32_kfile.c ../win32_handles.h ../win32_shims.h | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/test_kfile: test_kfile.c test_kfile_stubs.c $(OUT)/win32_kfile.o $(OUT)/win32_ksync.o $(OUT)/win32_guestobj.o $(RT:%=$(OUT)/rt_%.o)
	$(CC) $(CFLAGS) $^ -o $@
$(OUT)/test_nofile: test_nofile.c $(runtime)/rc_nofile.h | $(OUT)
	$(CC) $(CFLAGS) $< -o $@
$(OUT):
	mkdir -p $@

test: $(OUT)/test_ksync $(OUT)/test_nofile $(OUT)/test_close $(OUT)/test_kfile
	$(OUT)/test_ksync
	$(OUT)/test_ksync herd
	$(OUT)/test_nofile
	$(OUT)/test_close
	$(OUT)/test_kfile

clean:
	rm -rf $(OUT)

.PHONY: test clean
