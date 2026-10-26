# The platform shell (app, window, view, display link, input, file paths,
# Rocksmith.ini) rewritten as arm64-ready ObjC/C, with the engine behind
# function-pointer hooks. tools/ws7/recomp/app/build.sh compiles these sources
# into the native app. Part of the root build; also builds on its own:
#   cmake -S shims/platform -B build-platform && cmake --build build-platform
#   ctest --test-dir build-platform --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(rs_platform LANGUAGES C OBJC)

set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")
set(CMAKE_C_STANDARD 11)
set(rsp_archs "arm64;x86_64")
get_filename_component(rsp_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RS_GAME_PLAIN "${rsp_repo}/analysis/cg/Rocksmith2014_x64_plain"
  CACHE FILEPATH "Decrypted x86_64 game executable (for the diff test)")

add_compile_options(-Wall -Wextra -Wno-unused-parameter -O2)

# Portable C: engine boundary, input queue, paths, Rocksmith.ini, helpers.
add_library(rsp_core STATIC src/rsp_engine.c src/rsp_paths.c src/rsp_config.c src/rsp_logic.c)
target_include_directories(rsp_core PUBLIC include)
set_target_properties(rsp_core PROPERTIES OSX_ARCHITECTURES "${rsp_archs}")

# ObjC: AppDelegate, OpenGLView, MainController, MessageBox, the nib, main().
# Manual retain/release like the original.
add_library(rsp_shell STATIC src/rsp_shell.m)
target_link_libraries(rsp_shell PUBLIC rsp_core "-framework AppKit" "-framework OpenGL"
  "-framework CoreVideo" "-framework IOKit")
target_compile_options(rsp_shell PRIVATE -fno-objc-arc -Wno-deprecated-declarations)
set_target_properties(rsp_shell PROPERTIES OSX_ARCHITECTURES "${rsp_archs}")

# Unit tests of the portable half.
add_executable(rsp_core_test test/rsp_core_test.c)
target_link_libraries(rsp_core_test PRIVATE rsp_core)
set_target_properties(rsp_core_test PROPERTIES OSX_ARCHITECTURES "${rsp_archs}")

# The window test, as an app bundle in its own "game root" folder.
set(rsp_root "${CMAKE_CURRENT_BINARY_DIR}/root")
add_executable(rsp_shell_test MACOSX_BUNDLE test/rsp_shell_test.m)
target_link_libraries(rsp_shell_test PRIVATE rsp_shell)
target_compile_options(rsp_shell_test PRIVATE -fno-objc-arc -Wno-deprecated-declarations)
set_target_properties(rsp_shell_test PROPERTIES
  OSX_ARCHITECTURES "${rsp_archs}"
  OUTPUT_NAME RSPShellTest
  RUNTIME_OUTPUT_DIRECTORY "${rsp_root}"
  MACOSX_BUNDLE_BUNDLE_NAME RSPShellTest
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.bbelgg.rs-rearmed.rsp-shell-test)
set(rsp_shell_exe "${rsp_root}/RSPShellTest.app/Contents/MacOS/RSPShellTest")

# The diff test against the original x86_64 functions, run under Rosetta. It
# compiles the shims/game harness sources read-only.
set(rsp_harness "${rsp_repo}/shims/game/harness")
add_executable(rsp_platform_difftest test/rsp_platform_difftest.c
  "${rsp_harness}/rsimage.c" "${rsp_harness}/efd_env.c")
target_include_directories(rsp_platform_difftest PRIVATE "${rsp_harness}")
target_link_libraries(rsp_platform_difftest PRIVATE rsp_core)
target_compile_options(rsp_platform_difftest PRIVATE -Wno-sign-compare -Wno-missing-field-initializers)
set_target_properties(rsp_platform_difftest PROPERTIES OSX_ARCHITECTURES "x86_64")

enable_testing()
foreach(arch arm64 x86_64)
  add_test(NAME platform.core.${arch} COMMAND arch -${arch} $<TARGET_FILE:rsp_core_test>)
  set_tests_properties(platform.core.${arch} PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
  # Needs a logged-in GUI session (opens a 1280x720 window for a few seconds).
  add_test(NAME platform.shell.${arch} COMMAND arch -${arch} "${rsp_shell_exe}")
  set_tests_properties(platform.shell.${arch} PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed" RUN_SERIAL TRUE TIMEOUT 90)
endforeach()
if(EXISTS "${RS_GAME_PLAIN}")
  add_test(NAME platform.difftest.x86_64 COMMAND $<TARGET_FILE:rsp_platform_difftest> "${RS_GAME_PLAIN}")
  set_tests_properties(platform.difftest.x86_64 PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
endif()
