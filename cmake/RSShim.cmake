# rs_add_shim(<target>
#     INSTALL_NAME <name>          # exactly what the game's LC_LOAD_DYLIB says
#     SOURCES <src>...
#     [OUTPUT_NAME <file>]         # default: basename of INSTALL_NAME
#     [ARCHITECTURES <arch>...]    # default: CMAKE_OSX_ARCHITECTURES
#     [COMPATIBILITY_VERSION <v>]  # e.g. 7.0.0 for libstdc++.6
#     [CURRENT_VERSION <v>]
#     [LINK <lib>...]              # target_link_libraries(PRIVATE ...)
#     [REEXPORT <path>...]         # -reexport_library <path>
#     [STAGE_DIR <dir>]            # default: the calling CMakeLists' directory
#     [NO_STAGE])
#
# Builds a SHARED library whose file name and LC_ID_DYLIB match INSTALL_NAME, copies
# it into STAGE_DIR (when RS_STAGE_OUTPUTS is on), and registers a ctest that checks
# install name and architectures with tools/check-shim.sh.
function(rs_add_shim target)
  cmake_parse_arguments(PARSE_ARGV 1 S
    "NO_STAGE"
    "INSTALL_NAME;OUTPUT_NAME;COMPATIBILITY_VERSION;CURRENT_VERSION;STAGE_DIR"
    "SOURCES;ARCHITECTURES;LINK;REEXPORT")

  if(NOT S_INSTALL_NAME)
    message(FATAL_ERROR "rs_add_shim(${target}): INSTALL_NAME is required")
  endif()

  get_filename_component(install_dir "${S_INSTALL_NAME}" DIRECTORY)
  get_filename_component(install_file "${S_INSTALL_NAME}" NAME)
  if(NOT S_OUTPUT_NAME)
    set(S_OUTPUT_NAME "${install_file}")
  endif()
  if(NOT S_STAGE_DIR)
    set(S_STAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  add_library(${target} SHARED ${S_SOURCES})

  # PREFIX/SUFFIX cleared so OUTPUT_NAME is the literal file name ("Cg",
  # "libstdc++.6.dylib", ...). BUILD_WITH_INSTALL_NAME_DIR makes ld write
  # INSTALL_NAME_DIR/<file> as LC_ID_DYLIB directly instead of an @rpath name.
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    SUFFIX ""
    OUTPUT_NAME "${S_OUTPUT_NAME}"
    INSTALL_NAME_DIR "${install_dir}"
    BUILD_WITH_INSTALL_NAME_DIR ON
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default)

  if(NOT S_OUTPUT_NAME STREQUAL install_file)
    # File name differs from the install name's basename: force it explicitly.
    target_link_options(${target} PRIVATE "LINKER:-install_name,${S_INSTALL_NAME}")
  endif()
  if(S_ARCHITECTURES)
    set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${S_ARCHITECTURES}")
  endif()
  if(S_COMPATIBILITY_VERSION)
    set_target_properties(${target} PROPERTIES MACHO_COMPATIBILITY_VERSION "${S_COMPATIBILITY_VERSION}")
  endif()
  if(S_CURRENT_VERSION)
    set_target_properties(${target} PROPERTIES MACHO_CURRENT_VERSION "${S_CURRENT_VERSION}")
  endif()
  if(S_LINK)
    target_link_libraries(${target} PRIVATE ${S_LINK})
  endif()
  foreach(lib IN LISTS S_REEXPORT)
    target_link_options(${target} PRIVATE "LINKER:-reexport_library,${lib}")
  endforeach()

  if(RS_STAGE_OUTPUTS AND NOT S_NO_STAGE)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${S_STAGE_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${S_STAGE_DIR}/${S_OUTPUT_NAME}"
      COMMENT "Staging ${S_OUTPUT_NAME} -> ${S_STAGE_DIR}"
      VERBATIM)
  endif()

  get_target_property(archs ${target} OSX_ARCHITECTURES)
  if(NOT archs)
    set(archs "${CMAKE_OSX_ARCHITECTURES}")
  endif()
  string(REPLACE ";" "," archs_csv "${archs}")
  add_test(NAME shim.${target}.macho
    COMMAND "${CMAKE_SOURCE_DIR}/tools/check-shim.sh"
            "$<TARGET_FILE:${target}>" "${S_INSTALL_NAME}" "${archs_csv}")
endfunction()
