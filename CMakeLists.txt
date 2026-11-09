# Rocksmith 2014 ARM64 native port — shim libraries.
#
#   cmake -S . -B build -G Ninja
#   cmake --build build                      # every shim
#   cmake --build build --target cg          # one shim
#   ctest --test-dir build                   # unit tests and install-name checks
#
# Options:
#   -DRS_ROSETTA_TESTING=ON   build arm64 + x86_64 so shims can be loaded into the
#                             (x86_64) game under Rosetta. Default is arm64 only.
#   -DRS_STAGE_OUTPUTS=OFF    don't copy finished shims into their shims/<name>/ directory.
cmake_minimum_required(VERSION 3.20)

option(RS_ROSETTA_TESTING "Also build an x86_64 slice for testing under Rosetta" OFF)
option(RS_STAGE_OUTPUTS "Copy built shims into their shims/<name>/ directory" ON)

# Must be set before project() so the compiler checks use them.
if(RS_ROSETTA_TESTING)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
else()
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

# Optimized by default (the shims run in the game's frame loop: Cg parameter uploads, Bink decoding),
# with debug info and assertions kept (the tests use assert).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "" FORCE)
set(CMAKE_OBJC_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "" FORCE)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" RS_VERSION LIMIT_COUNT 1)
project(rs_rearmed VERSION ${RS_VERSION} LANGUAGES C CXX OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Location of the installed game, used by tests.
set(RS_GAME_APP
  "$ENV{HOME}/Library/Application Support/Steam/steamapps/common/Rocksmith2014/Rocksmith2014.app"
  CACHE PATH "Path to Rocksmith2014.app")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(RSShim)
include(RSTargetNames)

# The python3 on PATH (the build's venv, or the command line tools' one) before any framework Python
# CMake finds by itself, e.g. Homebrew's: the build uses nothing from Homebrew.
set(Python3_FIND_FRAMEWORK LAST)
find_package(Python3 COMPONENTS Interpreter)

enable_testing()

# shims/steam builds with its own Makefile (make -C shims/steam).
foreach(shim libstdcxx bink opengl cg game platform coreaudio)
  add_subdirectory(shims/${shim})
endforeach()

# Hook points and the mods folder on both runtimes, with the one mod dylib (tools/ws7/recomp/test/hooks)
add_test(NAME recomp.hooks COMMAND make -C ${CMAKE_SOURCE_DIR}/tools/ws7/recomp/test/hooks test
         OUT=${CMAKE_BINARY_DIR}/recomp-hooks)

# The per-game name tables (tools/targets): every name resolves in every target.
if(Python3_FOUND)
  add_test(NAME targets.check COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/targets/test_targets.py)
  # The Learn & Play install steps (win-x86/install): fixture cabinets, no Steam, no network.
  add_test(NAME win-x86.install COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/win-x86/install/test_lp_install.py)
  # Learn & Play's shader corpus sources (win-x86/shaders): hand-assembled shaders, nothing of the game.
  add_test(NAME win-x86.shaders COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/win-x86/shaders/test_shaders.py)
  # The installs' own copy of the game files (tools/play-native/gamefiles.py): made-up game folders.
  add_test(NAME play-native.gamefiles COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/play-native/test_gamefiles.py)
  add_test(NAME steamless.decrypt COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/steamless/test_decrypt.py)
  # The Direct Connect overlay (tools/play-native/cache_overlay.py): made-up archives; the real cache.psarc
  # when there is one.
  add_test(NAME play-native.cache_overlay COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/play-native/test_cache_overlay.py)
  # settings.env, the built-in mods switched off in the installer: the Mac launcher's and Learn & Play's readers on
  # the same made-up files, and the same list of keys in both and in the installer.
  add_test(NAME play-native.settings COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/play-native/test_settings.py)
endif()
