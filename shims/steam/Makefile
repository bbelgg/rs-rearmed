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

test: build/test_shim build/test_vtables
	arch -arm64 build/test_vtables
	arch -x86_64 build/test_vtables
	RS_STEAM_MODE=stub RS_STEAM_DLC=248750,258341 RS_STEAM_DATA_DIR=$(CURDIR)/build/testdata arch -arm64 build/test_shim
	RS_STEAM_MODE=stub RS_STEAM_DLC=248750,258341 RS_STEAM_DATA_DIR=$(CURDIR)/build/testdata arch -x86_64 build/test_shim
	RS_STEAM_MODE=stub RS_STEAM_DLC=all RS_STEAM_DATA_DIR=$(CURDIR)/build/testdata arch -arm64 build/test_shim
	RS_STEAM_MODE=stub RS_STEAM_DATA_DIR=$(CURDIR)/build/testdata arch -arm64 build/test_shim

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
