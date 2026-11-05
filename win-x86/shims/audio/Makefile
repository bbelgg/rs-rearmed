# win-x86 audio: the WASAPI and winmm faces on CoreAudio (audio.h, docs/windows/DESIGN.md "Audio").
#   make         build $(OUT)/libwinaudio.a (wa_*.c + the Mac build's game-independent audio code)
#   make test    run tests/test_audio (the objects through their guest cells, no game needed;
#                plays under a second of a quiet tone on the default output device)
#
# The game links libwinaudio.a, win32_guestobj.o, win32_ksync.o (w32_event_signal) and the
# AudioToolbox, CoreAudio and CoreFoundation frameworks.
OUT      ?= build
root     := ../../..
runtime  := $(root)/tools/ws7/recomp/runtime
coreaudio := $(root)/shims/coreaudio
CC       := clang -arch arm64
CFLAGS   := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DRC_GUEST32 \
            -I$(runtime) -I.. -I. -I$(coreaudio)
FRAMEWORKS := -framework AudioToolbox -framework CoreAudio -framework CoreFoundation

SRC := wa_engine wa_com wa_winmm
HDR := audio.h wa_engine.h ../win32_guestobj.h ../win32_shims.h $(coreaudio)/rs_audio_host.h \
       $(coreaudio)/rs_audio_input.h

all: $(OUT)/libwinaudio.a

$(OUT)/%.o: %.c $(HDR) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/rs_audio_host.o: $(coreaudio)/rs_audio_host.c $(coreaudio)/rs_audio_host.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/rs_audio_input.o: $(coreaudio)/rs_audio_input.c $(coreaudio)/rs_audio_input.h $(coreaudio)/rs_audio_host.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/win32_guestobj.o: ../win32_guestobj.c ../win32_guestobj.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/win32_ksync.o: ../win32_ksync.c ../win32_handles.h ../win32_shims.h | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT):
	mkdir -p $@

$(OUT)/libwinaudio.a: $(SRC:%=$(OUT)/%.o) $(OUT)/rs_audio_host.o $(OUT)/rs_audio_input.o
	rm -f $@ && ar rcs $@ $^

RT := rc32_mem rc32_teb rc32_image rc32_seh
$(OUT)/rt_%.o: $(runtime)/%.c | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/test_audio: tests/test_audio.c $(OUT)/libwinaudio.a $(OUT)/win32_guestobj.o $(OUT)/win32_ksync.o \
                   $(RT:%=$(OUT)/rt_%.o)
	$(CC) $(CFLAGS) tests/test_audio.c $(OUT)/libwinaudio.a $(OUT)/win32_guestobj.o $(OUT)/win32_ksync.o \
	    $(RT:%=$(OUT)/rt_%.o) $(FRAMEWORKS) -o $@

# The test leaves the devices' sample rates alone (RS_AUDIO_SET_RATE=0).
test: $(OUT)/test_audio
	RS_AUDIO_SET_RATE=0 RS_AUDIO_LOG=1 $(OUT)/test_audio

clean:
	rm -rf $(OUT)

.PHONY: all test clean
