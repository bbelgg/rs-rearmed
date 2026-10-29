# Universal (arm64 + x86_64) libsteam_api.dylib replacement.
#   make         build build/libsteam_api.dylib and copy it to ./libsteam_api.dylib
#   make test    run ABI tests on both architectures (x86_64 under Rosetta)
#   make check   compare exported symbols with the game's imports

CXX      ?= clang++
ARCHS    := -arch arm64 -arch x86_64
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden \
            -fno-exceptions -mmacosx-version-min=11.0
GAME_BIN ?= $(HOME)/Library/Application Support/Steam/steamapps/common/Rocksmith2014/Rocksmith2014.app/Contents/MacOS/Rocksmith2014

SRCS := src/shim.cpp src/interfaces.cpp
HDRS := src/shim.h
LIB  := build/libsteam_api.dylib

all: libsteam_api.dylib

$(LIB): $(SRCS) $(HDRS)
	@mkdir -p build
	$(CXX) $(ARCHS) $(CXXFLAGS) -dynamiclib $(SRCS) -o $@ \
	    -install_name @loader_path/libsteam_api.dylib \
	    -compatibility_version 1.0.0 -current_version 1.0.0
	codesign -f -s - $@

libsteam_api.dylib: $(LIB)
	cp $< $@

build/test_shim: tests/test_shim.cpp $(LIB)
	$(CXX) $(ARCHS) -std=c++17 -O1 -mmacosx-version-min=11.0 $< -Lbuild -lsteam_api \
	    -Wl,-rpath,@executable_path -o $@
	install_name_tool -change @loader_path/libsteam_api.dylib @rpath/libsteam_api.dylib $@ 2>/dev/null
	codesign -f -s - $@

build/test_vtables: tests/test_vtables.cpp $(SRCS) $(HDRS)
	$(CXX) $(ARCHS) $(CXXFLAGS) -fvisibility=default tests/test_vtables.cpp $(SRCS) -o $@

# The tests run with a HOME of their own (no Steam there, unless a run makes one) and without the
# settings the developer's environment may have for the game (README: launchctl setenv).
unexport RS_STEAM_MODE RS_STEAM_DLC RS_STEAM_LANGUAGE RS_STEAM_ID RS_STEAM_PERSONA RS_STEAM_LOG \
    RS_STEAM_DATA_DIR RS_STEAM_REAL_LIB SteamAppId
TEST_ENV := RS_STEAM_MODE=stub RS_STEAM_DATA_DIR=$(CURDIR)/build/testdata
NO_STEAM := HOME=$(CURDIR)/build/emptyhome

test: build/test_shim build/test_vtables
	@if [ -e build/steam_shim.ini ]; then echo "build/steam_shim.ini would change the tests' settings: move it away" >&2; exit 1; fi
	arch -arm64 build/test_vtables
	arch -x86_64 build/test_vtables
	$(TEST_ENV) $(NO_STEAM) RS_STEAM_DLC=248750,258341 arch -arm64 build/test_shim
	$(TEST_ENV) $(NO_STEAM) RS_STEAM_DLC=248750,258341 arch -x86_64 build/test_shim
	$(TEST_ENV) $(NO_STEAM) RS_STEAM_DLC=all arch -arm64 build/test_shim
	$(TEST_ENV) $(NO_STEAM) arch -arm64 build/test_shim
	@# The native launcher's stage: symlinks to an install in another Steam library (e.g. on an external
	@# drive), whose appmanifest has the DLC and language. Steam's own folder has another appmanifest.
	@rm -rf build/stage build/testlib build/stagehome
	@mkdir -p build/stage/Rocksmith2014.app/Contents/MacOS build/testlib/steamapps/common/Rocksmith2014 \
	    "build/stagehome/Library/Application Support/Steam/steamapps"
	@cp build/test_shim $(LIB) build/stage/Rocksmith2014.app/Contents/MacOS/
	@touch build/testlib/steamapps/common/Rocksmith2014/static.psarc
	@ln -s $(CURDIR)/build/testlib/steamapps/common/Rocksmith2014/static.psarc build/stage/static.psarc
	@printf '"AppState" { "appid" "221680" "UserConfig" { "language" "german" }\n  "InstalledDepots" { "258342" { "manifest" "1" "dlcappid" "258341" } } }\n' \
	    > build/testlib/steamapps/appmanifest_221680.acf
	@printf '"AppState" { "appid" "221680" "UserConfig" { "language" "french" }\n  "InstalledDepots" { "258344" { "manifest" "1" "dlcappid" "258343" } } }\n' \
	    > "build/stagehome/Library/Application Support/Steam/steamapps/appmanifest_221680.acf"
	$(TEST_ENV) HOME=$(CURDIR)/build/stagehome RS_TEST_STAGE=1 arch -arm64 build/stage/Rocksmith2014.app/Contents/MacOS/test_shim

check: $(LIB)
	@nm -u -arch x86_64 "$(GAME_BIN)" | grep -E '^_Steam(API|Internal)_' | sort > build/imports.txt
	@for a in arm64 x86_64; do \
	    nm -gU -arch $$a $(LIB) | awk '{print $$3}' | sort > build/exports-$$a.txt; \
	    missing=$$(comm -23 build/imports.txt build/exports-$$a.txt); \
	    if [ -n "$$missing" ]; then echo "$$a missing: $$missing"; exit 1; fi; \
	    echo "$$a: all $$(wc -l < build/imports.txt | tr -d ' ') game imports exported"; \
	done

clean:
	rm -rf build libsteam_api.dylib

.PHONY: all test check clean
