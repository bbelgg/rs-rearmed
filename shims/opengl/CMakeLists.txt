# OpenGL. macOS still ships OpenGL.framework for arm64e, implemented on Metal
# by Apple (AppleMetalOpenGLRenderer), and it provides all 107 GL/CGL functions
# the game imports. This directory ships:
#   libRSOpenGL.dylib  OpenGL.framework replacement: wrappers for the game's 107
#                      imports forwarding to the system framework (+ re-export of
#                      the rest). The seam for compatibility fixes.
#   libglspy.dylib     the same wrappers as a DYLD_INSERT_LIBRARIES interposer,
#                      for tracing the real game under Rosetta
#   gl_native_test     arm64 test of every import through a real NSOpenGLContext

set(ogl_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(ogl_imports "${ogl_dir}/game-gl-imports.txt")
set(ogl_inc "${CMAKE_CURRENT_BINARY_DIR}/gl_api.inc")
execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE ogl_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
set(ogl_gl_headers "${ogl_sdk}/System/Library/Frameworks/OpenGL.framework/Headers")

add_custom_command(OUTPUT "${ogl_inc}"
  COMMAND python3 "${ogl_dir}/tools/gen-gl-api.py" "${ogl_imports}" "${ogl_gl_headers}" "${ogl_inc}"
  DEPENDS "${ogl_dir}/tools/gen-gl-api.py" "${ogl_imports}"
  COMMENT "Generating gl_api.inc from the game's OpenGL imports"
  VERBATIM)
add_custom_target(opengl_api DEPENDS "${ogl_inc}")

# Enum names for the call tracer (src/gltrace.c).
set(ogl_enums "${CMAKE_CURRENT_BINARY_DIR}/gl_enums.inc")
add_custom_command(OUTPUT "${ogl_enums}"
  COMMAND python3 "${ogl_dir}/tools/gen-gl-enums.py" "${ogl_gl_headers}" "${ogl_enums}"
  DEPENDS "${ogl_dir}/tools/gen-gl-enums.py"
  COMMENT "Generating gl_enums.inc from the SDK's OpenGL headers"
  VERBATIM)
add_custom_target(opengl_enums DEPENDS "${ogl_enums}")

set(ogl_sources src/rsgl.c src/nsgl_hook.m src/gltrace.c)
set(ogl_frameworks "-framework OpenGL" "-framework AppKit" "-framework ImageIO")

# --- OpenGL.framework replacement ---------------------------------------------
# Not the system install name: dyld matches loaded images by install name, so a
# shim called /System/.../OpenGL could never load the real framework (same as
# the libstdc++ shim).
rs_add_shim(opengl
  INSTALL_NAME @executable_path/libRSOpenGL.dylib
  SOURCES ${ogl_sources}
  ARCHITECTURES arm64 x86_64
  LINK ${ogl_frameworks})
add_dependencies(opengl opengl_api opengl_enums)
target_include_directories(opengl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)
# Everything else the framework exports (CGL*, other gl*) passes straight through.
target_link_options(opengl PRIVATE "LINKER:-reexport_framework,OpenGL")
set_target_properties(opengl PROPERTIES C_VISIBILITY_PRESET hidden OBJC_VISIBILITY_PRESET hidden)

# --- Spy ------------------------------------------------------------------------
add_library(glspy SHARED ${ogl_sources})
add_dependencies(glspy opengl_api opengl_enums)
target_compile_definitions(glspy PRIVATE RS_GL_SPY)
target_include_directories(glspy PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)
target_link_libraries(glspy PRIVATE ${ogl_frameworks})
set_target_properties(glspy PROPERTIES
  PREFIX "lib" SUFFIX ".dylib"
  OSX_ARCHITECTURES "arm64;x86_64"
  C_VISIBILITY_PRESET hidden OBJC_VISIBILITY_PRESET hidden)

# --- Tests ----------------------------------------------------------------------
# Same source twice: bound to the system framework, and bound to the shim.
foreach(variant system shim)
  add_executable(gl_native_test_${variant} test/gl_native_test.m)
  add_dependencies(gl_native_test_${variant} opengl_api)
  target_include_directories(gl_native_test_${variant} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)
  target_compile_options(gl_native_test_${variant} PRIVATE -fobjc-arc)
  set_target_properties(gl_native_test_${variant} PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
  target_link_libraries(gl_native_test_${variant} PRIVATE
    "-framework Cocoa" "-framework CoreVideo" "-framework ApplicationServices")
endforeach()
target_link_libraries(gl_native_test_system PRIVATE "-framework OpenGL")
# The shim re-exports OpenGL.framework, so the test links nothing else for GL.
target_link_libraries(gl_native_test_shim PRIVATE opengl)

set(ogl_pass PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
foreach(arch arm64 x86_64)
  add_test(NAME opengl.system.core.${arch}
    COMMAND arch -${arch} "$<TARGET_FILE:gl_native_test_system>")
  add_test(NAME opengl.system.legacy.${arch}
    COMMAND arch -${arch} "$<TARGET_FILE:gl_native_test_system>" --legacy)
  add_test(NAME opengl.shim.window.${arch}
    COMMAND arch -${arch} "$<TARGET_FILE:gl_native_test_shim>" --window)
  set_tests_properties(opengl.system.core.${arch} opengl.system.legacy.${arch}
    PROPERTIES ${ogl_pass})
  # With error checking on, through the shim, all 107 imports must be called.
  set_tests_properties(opengl.shim.window.${arch} PROPERTIES
    ENVIRONMENT "RS_GL_CHECK=1"
    PASS_REGULAR_EXPRESSION "COVERAGE 107/107.*[0-9]+ passed, 0 failed"
    RUN_SERIAL TRUE)

  # The spy, injected into the system-bound test, sees every call too.
  add_test(NAME opengl.spy.${arch}
    COMMAND sh -c "arch -${arch} -e DYLD_INSERT_LIBRARIES=\"$1\" -e RS_GL_LOG=\"$2\" \"$0\" --window && grep -q '^used 107/107' \"$2\" && echo SPY-OK"
            "$<TARGET_FILE:gl_native_test_system>" "$<TARGET_FILE:glspy>"
            "${CMAKE_CURRENT_BINARY_DIR}/spy-${arch}.log")
  set_tests_properties(opengl.spy.${arch} PROPERTIES
    PASS_REGULAR_EXPRESSION "SPY-OK" RUN_SERIAL TRUE)
endforeach()

if(EXISTS "${RS_GAME_APP}/Contents/MacOS/Rocksmith2014")
  # The checked-in import list still matches the game binary.
  add_test(NAME opengl.game.import_list
    COMMAND sh -c "xcrun dyld_info -arch x86_64 -imports \"$0\" | awk '/from OpenGL\\)/ {print $1}' | sort -u | diff - \"$1\""
            "${RS_GAME_APP}/Contents/MacOS/Rocksmith2014" "${ogl_imports}")
endif()
