# arm64 rewrites of game-binary functions (linked into the native app through
# tools/ws7/recomp/overrides) + the x86_64 differential harness that runs the
# original code under Rosetta.

set(rsg_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(RS_GAME_PLAIN "${CMAKE_SOURCE_DIR}/analysis/cg/Rocksmith2014_x64_plain"
  CACHE FILEPATH "Decrypted x86_64 game executable (tools/steamless)")

# Rewritten modules. arm64 is the target; x86_64 so the harness can link them.
add_library(rs_game STATIC src/ak_hash.c src/efd_log.c src/efd_id.c src/efd_util.c
  src/efd_fixedstring.cpp src/efd_datastream.cpp src/efd_msghelper.cpp
  src/crypto_aes.c src/crypto_bn.c src/crypto_dsa.c src/crypto_game.c)
target_include_directories(rs_game PUBLIC include)
# crypto_game.c: profile loads inflate with the system zlib. SHA-1 is
# CommonCrypto's (libSystem); CC_SHA1 is marked deprecated, and fine here.
target_link_libraries(rs_game PUBLIC z)
set_source_files_properties(src/crypto_dsa.c PROPERTIES COMPILE_OPTIONS -Wno-deprecated-declarations)
# The efd rewrites must not contract float math (none uses it so far) and
# build without exceptions or RTTI, like the engine.
target_compile_options(rs_game PRIVATE -O2 -ffp-contract=off
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -Wno-invalid-offsetof>)
set_target_properties(rs_game PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")

# The offline replacement of the online layer (RSConnectExtension_RDV), kept in
# its own library so test game.offline.no_network can check what it imports.
add_library(rs_online STATIC src/online_stub.cpp)
target_include_directories(rs_online PUBLIC include)
target_compile_options(rs_online PRIVATE -O2 -fno-exceptions -fno-rtti)
set_target_properties(rs_online PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")

# Third-party libraries the original links statically, rebuilt from upstream
# source (see THIRD_PARTY_NOTICES.md). Configured like the game's copy.
rs_glob(rsg_lua_src third_party/lua-5.1.4-lnum/src/*.c)
add_library(rs_lua51 STATIC ${rsg_lua_src})
target_include_directories(rs_lua51 PUBLIC third_party/lua-5.1.4-lnum/src)
target_compile_definitions(rs_lua51 PUBLIC LNUM_DOUBLE LNUM_INT64)
# gnu89 like the original (no C99 math.acosh/asinh/atanh); LNUM's overflow
# checks rely on wrapping signed arithmetic.
target_compile_options(rs_lua51 PRIVATE -O2 -fwrapv -ffp-contract=off -w)
set_target_properties(rs_lua51 PROPERTIES OSX_ARCHITECTURES "arm64;x86_64"
  C_STANDARD 90 C_EXTENSIONS ON)

rs_glob(rsg_png_src third_party/libpng-1.5.13/*.c)
add_library(rs_png15 STATIC ${rsg_png_src})
target_include_directories(rs_png15 PUBLIC third_party/libpng-1.5.13)
# pngpriv.h would pick the classic Mac OS <fp.h> because TARGET_OS_MAC is set.
target_compile_options(rs_png15 PRIVATE -O2 -w -include math.h)
target_link_libraries(rs_png15 PUBLIC z)
set_target_properties(rs_png15 PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")

rs_glob(rsg_jpeg_src third_party/jpeg-8d/*.c)
add_library(rs_jpeg8 STATIC ${rsg_jpeg_src})
target_include_directories(rs_jpeg8 PUBLIC third_party/jpeg-8d)
# No FMA contraction: JDCT_FLOAT must round like the game's x86_64 SSE code.
target_compile_options(rs_jpeg8 PRIVATE -O2 -ffp-contract=off -w)
set_target_properties(rs_jpeg8 PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")

# Differential test: original x86_64 functions vs rewrites, writes the golden file.
add_executable(rs_game_difftest harness/difftest.c harness/rsimage.c harness/efd_env.c)
target_link_libraries(rs_game_difftest PRIVATE rs_game rs_online rs_lua51 rs_png15 rs_jpeg8 z)
target_compile_options(rs_game_difftest PRIVATE -Wno-deprecated-declarations)
set_target_properties(rs_game_difftest PROPERTIES OSX_ARCHITECTURES "x86_64")

# Native test: arm64 rewrites vs the golden file.
add_executable(rs_game_native_test test/native_test.c)
target_include_directories(rs_game_native_test PRIVATE test)
target_link_libraries(rs_game_native_test PRIVATE rs_game rs_online rs_lua51 rs_png15 rs_jpeg8 z)
target_compile_options(rs_game_native_test PRIVATE -Wno-deprecated-declarations)
set_target_properties(rs_game_native_test PROPERTIES OSX_ARCHITECTURES "arm64")

# The installed game (read-only): real archives for the crypto tests.
get_filename_component(rsg_game_dir_default "${RS_GAME_APP}" DIRECTORY)
set(RS_GAME_DIR "${rsg_game_dir_default}" CACHE PATH "Rocksmith2014 install (the folder with the .psarc files)")
set(rsg_game_dir "")
if(EXISTS "${RS_GAME_DIR}/songs.psarc")
  set(rsg_game_dir "${RS_GAME_DIR}")
endif()

# The stub and the crypto must import nothing that talks to a network.
add_test(NAME game.offline.no_network
  COMMAND ${CMAKE_COMMAND} -DLIBS=$<TARGET_FILE:rs_online>|$<TARGET_FILE:rs_game>
    -P "${rsg_dir}/test/no_network.cmake")
set_tests_properties(game.offline.no_network PROPERTIES PASS_REGULAR_EXPRESSION "no network imports")

set(rsg_golden "${CMAKE_CURRENT_BINARY_DIR}/golden.txt")
if(EXISTS "${RS_GAME_PLAIN}")
  add_test(NAME game.difftest.x86_64
    COMMAND rs_game_difftest "${RS_GAME_PLAIN}" "${rsg_golden}" ${rsg_game_dir})
  set_tests_properties(game.difftest.x86_64 PROPERTIES
    FIXTURES_SETUP rsg_golden
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
  add_test(NAME game.native.arm64
    COMMAND rs_game_native_test "${rsg_golden}" "${RS_GAME_PLAIN}" ${rsg_game_dir})
  set_tests_properties(game.native.arm64 PROPERTIES
    FIXTURES_REQUIRED rsg_golden
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
else()
  message(STATUS "shims/game: ${RS_GAME_PLAIN} not found, differential tests disabled")
endif()
