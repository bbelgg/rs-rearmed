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

# GL: libRSOpenGL (shims/opengl's `opengl` target, which re-exports OpenGL.framework) in the root build,
# as in the app; the system framework when this directory builds on its own. libRSOpenGL has the root
# build's architectures only, so what links it does too.
if(TARGET opengl)
  set(rsp_gl opengl)
  set(rsp_gl_archs "${CMAKE_OSX_ARCHITECTURES}")
else()
  set(rsp_gl "-framework OpenGL")
  set(rsp_gl_archs "${rsp_archs}")
endif()

# ObjC: AppDelegate, OpenGLView, MainController, MessageBox, the nib, main().
# Manual retain/release like the original.
# rsp_present_metal.m: the CAMetalLayer path, used when libRSOpenGL's backend is Metal (rsp_present.h).
add_library(rsp_shell STATIC src/rsp_shell.m src/rsp_overlay.c src/rsp_present_metal.m)
target_link_libraries(rsp_shell PUBLIC rsp_core "-framework AppKit" ${rsp_gl}
  "-framework CoreVideo" "-framework IOKit" "-framework Metal" "-framework QuartzCore")
target_compile_options(rsp_shell PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc> -Wno-deprecated-declarations)
set_target_properties(rsp_shell PROPERTIES OSX_ARCHITECTURES "${rsp_gl_archs}")

# Unit tests of the portable half.
add_executable(rsp_core_test test/rsp_core_test.c)
target_link_libraries(rsp_core_test PRIVATE rsp_core)
set_target_properties(rsp_core_test PROPERTIES OSX_ARCHITECTURES "${rsp_archs}")

# The overlay API (rsp_overlay.c) on an offscreen GL context: no window.
add_executable(rsp_overlay_test test/rsp_overlay_test.c src/rsp_overlay.c)
target_include_directories(rsp_overlay_test PRIVATE include)
target_link_libraries(rsp_overlay_test PRIVATE ${rsp_gl})
target_compile_options(rsp_overlay_test PRIVATE -Wno-deprecated-declarations)
set_target_properties(rsp_overlay_test PROPERTIES OSX_ARCHITECTURES "${rsp_gl_archs}")

# The window test, as an app bundle in its own "game root" folder.
set(rsp_root "${CMAKE_CURRENT_BINARY_DIR}/root")
add_executable(rsp_shell_test MACOSX_BUNDLE test/rsp_shell_test.m)
target_link_libraries(rsp_shell_test PRIVATE rsp_shell)
target_compile_options(rsp_shell_test PRIVATE -fno-objc-arc -Wno-deprecated-declarations)
set_target_properties(rsp_shell_test PROPERTIES
  OSX_ARCHITECTURES "${rsp_gl_archs}"
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
  # Needs a logged-in GUI session (opens a 1280x720 window for a few seconds): only in this
  # directory's own build, not in the root build's ctest.
  if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_test(NAME platform.shell.${arch} COMMAND arch -${arch} "${rsp_shell_exe}")
    set_tests_properties(platform.shell.${arch} PROPERTIES
      PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed" RUN_SERIAL TRUE TIMEOUT 90)
  endif()
endforeach()
# The overlay through libRSOpenGL (whose install name is @executable_path/, next to the app's
# executable: DYLD_LIBRARY_PATH finds it in its build directory). The Metal variant waits for the
# Metal core (docs/METAL-PLAN.md step 5).
if(TARGET opengl)
  set(rsp_dyld -e "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:opengl>")
endif()
foreach(arch IN LISTS rsp_gl_archs)
  add_test(NAME platform.overlay.${arch} COMMAND arch -${arch} ${rsp_dyld} $<TARGET_FILE:rsp_overlay_test>)
  set_tests_properties(platform.overlay.${arch} PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
endforeach()
if(TARGET opengl)
  add_test(NAME platform.overlay.metal COMMAND arch -arm64 ${rsp_dyld} -e RS_GL_BACKEND=metal
    $<TARGET_FILE:rsp_overlay_test>)
  # Needs the Metal core's shader translator (rsmsl, shims/opengl/msl.cmake).
  set_tests_properties(platform.overlay.metal PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
  if(NOT TARGET rsmsl)
    set_tests_properties(platform.overlay.metal PROPERTIES DISABLED TRUE)
  endif()
endif()
# The Metal presentation path on a CAMetalLayer in no window (rsp_present_metal.m). Needs
# libRSOpenGL; the Metal variant needs the Metal core (metal.cmake) in it.
if(TARGET opengl)
  add_executable(rsp_present_test test/rsp_present_test.m src/rsp_present_metal.m)
  target_link_libraries(rsp_present_test PRIVATE opengl "-framework Foundation" "-framework AppKit"
    "-framework Metal" "-framework QuartzCore")
  target_compile_options(rsp_present_test PRIVATE -fno-objc-arc -Wno-deprecated-declarations)
  set_target_properties(rsp_present_test PROPERTIES OSX_ARCHITECTURES "${rsp_gl_archs}")
  foreach(arch IN LISTS rsp_gl_archs)
    add_test(NAME platform.present.gl.${arch} COMMAND arch -${arch} ${rsp_dyld} -e RS_GL_BACKEND=gl
      $<TARGET_FILE:rsp_present_test>)
    set_tests_properties(platform.present.gl.${arch} PROPERTIES
      PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
  endforeach()
  add_test(NAME platform.present.metal COMMAND arch -arm64 ${rsp_dyld} -e RS_GL_BACKEND=metal
    $<TARGET_FILE:rsp_present_test>)
  set_tests_properties(platform.present.metal PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
endif()
# The app's warm-only mode (--warm-shaders, rsp_mtl_warm_only) on a test corpus written with the
# translator (rsmsl): child processes, no window.
if(TARGET opengl AND TARGET rsmsl)
  add_executable(rsp_warm_test test/rsp_warm_test.m src/rsp_present_metal.m)
  target_link_libraries(rsp_warm_test PRIVATE opengl rsmsl "-framework Foundation" "-framework AppKit"
    "-framework Metal" "-framework QuartzCore")
  target_compile_options(rsp_warm_test PRIVATE -fno-objc-arc -Wno-deprecated-declarations)
  set_target_properties(rsp_warm_test PROPERTIES OSX_ARCHITECTURES arm64 LINKER_LANGUAGE CXX)
  add_test(NAME platform.warm.metal COMMAND arch -arm64 ${rsp_dyld} $<TARGET_FILE:rsp_warm_test>
    "${CMAKE_CURRENT_BINARY_DIR}/warm-test")
  set_tests_properties(platform.warm.metal PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed" TIMEOUT 300)
endif()
if(EXISTS "${RS_GAME_PLAIN}")
  add_test(NAME platform.difftest.x86_64 COMMAND $<TARGET_FILE:rsp_platform_difftest> "${RS_GAME_PLAIN}")
  set_tests_properties(platform.difftest.x86_64 PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
endif()
