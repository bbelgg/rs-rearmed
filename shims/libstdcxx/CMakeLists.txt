# libstdc++. The system still provides /usr/lib/libstdc++.6.dylib for
# arm64(e) and x86_64 (dyld shared cache only), so this directory ships:
#   libstdc++.6.tbd    link stub for it (the SDK no longer has one)
#   libstdc++.6.dylib  optional forwarding shim that re-exports it
# plus tests proving the system library provides the GNU ABI the game needs.

set(cxx_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(cxx_tbd "${CMAKE_CURRENT_BINARY_DIR}/libstdc++.6.tbd")

# --- Link stub ----------------------------------------------------------------
add_custom_command(OUTPUT "${cxx_tbd}"
  COMMAND "${cxx_dir}/tools/gen-tbd.sh" "${cxx_tbd}"
  COMMAND ${CMAKE_COMMAND} -E copy "${cxx_tbd}" "${cxx_dir}/libstdc++.6.tbd"
  DEPENDS "${cxx_dir}/tools/gen-tbd.sh"
  COMMENT "Generating libstdc++.6.tbd from the dyld shared cache"
  VERBATIM)
add_custom_target(libstdcxx_tbd DEPENDS "${cxx_tbd}")

# --- Forwarding shim ----------------------------------------------------------
# Not /usr/lib/libstdc++.6.dylib: ld refuses to re-export a dylib with the
# shim's own install name, and dyld matches loaded images by install name, so
# even linking via an alias the re-export resolves back to the shim itself.
rs_add_shim(libstdcxx
  INSTALL_NAME @executable_path/libstdc++.6.dylib
  SOURCES src/forward.c
  ARCHITECTURES arm64 x86_64
  COMPATIBILITY_VERSION 7.0.0
  CURRENT_VERSION 104.1.0
  REEXPORT "${cxx_tbd}")
add_dependencies(libstdcxx libstdcxx_tbd)
set_property(TARGET libstdcxx APPEND PROPERTY LINK_DEPENDS "${cxx_tbd}")

# --- Tests --------------------------------------------------------------------
add_executable(libstdcxx_symcheck tools/symcheck.c)
set_target_properties(libstdcxx_symcheck PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")

# libc++ side of the exception-interop test: a separate dylib so the test
# binary itself binds its C++ runtime symbols only through libstdc++.
add_library(libstdcxx_interop SHARED test/interop_libcxx.cpp)
set_target_properties(libstdcxx_interop PROPERTIES
  OSX_ARCHITECTURES "arm64;x86_64"
  INSTALL_NAME_DIR "@executable_path"
  BUILD_WITH_INSTALL_NAME_DIR ON)

add_executable(libstdcxx_abi_test test/abi_test.cpp)
add_dependencies(libstdcxx_abi_test libstdcxx_tbd)
set_target_properties(libstdcxx_abi_test PROPERTIES
  OSX_ARCHITECTURES "arm64;x86_64"
  CXX_STANDARD 11)
# -nostdinc++/-nostdlib++: GNU declarations from gnu_abi.h only, no libc++.
target_compile_options(libstdcxx_abi_test PRIVATE -nostdinc++ -Wall)
target_link_options(libstdcxx_abi_test PRIVATE -nostdlib++)
target_link_libraries(libstdcxx_abi_test PRIVATE "${cxx_tbd}" libstdcxx_interop)

set(cxx_imports "${cxx_dir}/game-imports.txt")
set(cxx_system_lib /usr/lib/libstdc++.6.dylib)
foreach(arch arm64 x86_64)
  add_test(NAME libstdcxx.system.imports.${arch}
    COMMAND sh -c "arch -${arch} \"$0\" ${cxx_system_lib} < \"$1\""
            "$<TARGET_FILE:libstdcxx_symcheck>" "${cxx_imports}")
  add_test(NAME libstdcxx.shim.imports.${arch}
    COMMAND sh -c "arch -${arch} \"$0\" \"$2\" < \"$1\""
            "$<TARGET_FILE:libstdcxx_symcheck>" "${cxx_imports}" "$<TARGET_FILE:libstdcxx>")
  add_test(NAME libstdcxx.abi.${arch}
    COMMAND arch -${arch} "$<TARGET_FILE:libstdcxx_abi_test>")
  set_tests_properties(libstdcxx.abi.${arch} PROPERTIES
    PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")
endforeach()

# The shim itself (not just the system library) is what gets loaded.
add_test(NAME libstdcxx.shim.verbose
  COMMAND sh -c "\"$0\" \"$1\" < /dev/null"
          "$<TARGET_FILE:libstdcxx_symcheck>" "$<TARGET_FILE:libstdcxx>")
set_tests_properties(libstdcxx.shim.verbose PROPERTIES
  ENVIRONMENT "RS_SHIM_VERBOSE=1"
  PASS_REGULAR_EXPRESSION "\\[rs-libstdcxx\\] forwarding")

# The checked-in import list still matches the game binary.
if(EXISTS "${RS_GAME_APP}/Contents/MacOS/Rocksmith2014")
  add_test(NAME libstdcxx.game.import_list
    COMMAND sh -c "xcrun dyld_info -arch x86_64 -imports \"$0\" | awk '/from libstdc/ {print $1}' | sort -u | diff - \"$1\""
            "${RS_GAME_APP}/Contents/MacOS/Rocksmith2014" "${cxx_imports}")
endif()
