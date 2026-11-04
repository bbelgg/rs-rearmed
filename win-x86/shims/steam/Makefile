# win-x86 steam_api.dll stub (offline).
#   make         build $(OUT)/libsteam32.a (steam32.c + the Mac stub's offline state)
#   make test    run tests/test_steam32 (guest ABI end to end, no game needed)
#   make slots STEAM_API_DLL=/path/to/steam_api.dll PY=python-with-capstone
#                regenerate steam32_slots.h from the game's DLL
#
# The game links libsteam32.a, win32_guestobj.o and libc++ (the host part is C++).
OUT      ?= build
root     := ../../..
runtime  := $(root)/tools/ws7/recomp/runtime
macstub  := $(root)/shims/steam/src
CC       := clang -arch arm64
CXX      := clang++ -arch arm64
CFLAGS   := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DRC_GUEST32 -I$(runtime) -I.. -I.
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-exceptions -I$(macstub) -I.
PY       ?= python3

all: $(OUT)/libsteam32.a

$(OUT)/%.o: %.c steam32.h steam32_host.h steam32_slots.h ../win32_guestobj.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/win32_guestobj.o: ../win32_guestobj.c ../win32_guestobj.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/steam32_host.o: steam32_host.cpp steam32_host.h $(macstub)/interfaces.cpp $(macstub)/enumerate.h $(macstub)/shim.h | $(OUT)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/macstub_shim.o: $(macstub)/shim.cpp $(macstub)/shim.h | $(OUT)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT):
	mkdir -p $@

$(OUT)/libsteam32.a: $(OUT)/steam32.o $(OUT)/steam32_host.o $(OUT)/macstub_shim.o
	rm -f $@ && ar rcs $@ $^

RT := rc32_mem rc32_teb  # no image registry: the test has its own rc32_lookup
$(OUT)/rt_%.o: $(runtime)/%.c | $(OUT)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/test_steam32: tests/test_steam32.c $(OUT)/libsteam32.a $(OUT)/win32_guestobj.o $(RT:%=$(OUT)/rt_%.o)
	$(CXX) -x c -std=gnu11 $(CFLAGS) tests/test_steam32.c -x none $(OUT)/libsteam32.a $(OUT)/win32_guestobj.o \
	    $(RT:%=$(OUT)/rt_%.o) -o $@

# The tests run with a HOME of their own (no Steam there) and a data directory under $(OUT).
unexport RS_STEAM_MODE RS_STEAM_DLC RS_STEAM_LANGUAGE RS_STEAM_ID RS_STEAM_PERSONA RS_STEAM_LOG \
    RS_STEAM_DATA_DIR RS_STEAM_REAL_LIB SteamAppId
test: $(OUT)/test_steam32
	rm -rf $(OUT)/testdata $(OUT)/emptyhome && mkdir -p $(OUT)/emptyhome
	HOME=$(abspath $(OUT))/emptyhome RS_STEAM_DATA_DIR=$(abspath $(OUT))/testdata RS_STEAM_DLC=248750,258341 \
	    $(OUT)/test_steam32

slots:
	$(PY) gen_slots.py $(root)/tools/ws7/recomp $(STEAM_API_DLL) > steam32_slots.h

clean:
	rm -rf $(OUT)

.PHONY: all test slots clean
