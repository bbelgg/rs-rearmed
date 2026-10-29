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

enable_testing()

# shims/steam builds with its own Makefile (make -C shims/steam).
foreach(shim libstdcxx bink opengl cg game platform)
  add_subdirectory(shims/${shim})
endforeach()
