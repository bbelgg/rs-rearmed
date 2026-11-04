# A target's game names for C code built by CMake (tools/targets):
#
#   rs_target_names(<target-id> <out-var>)
#
# runs `tools/targets/targets.py gen` into <build>/target-<target-id> at configure time and sets
# <out-var> to that directory, for target_include_directories (it holds rc_target_names.h). Configure
# reruns when a map or list changes; targets.py rewrites only files whose contents changed.
get_filename_component(rs_targets_dir "${CMAKE_CURRENT_LIST_DIR}/../tools/targets" ABSOLUTE)

function(rs_target_names tid out_var)
  if(NOT Python3_EXECUTABLE)
    set(Python3_FIND_FRAMEWORK LAST)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
  endif()
  set(dir "${CMAKE_BINARY_DIR}/target-${tid}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${rs_targets_dir}/targets.py" gen --target ${tid} "${dir}"
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "tools/targets/targets.py gen --target ${tid} failed: ${err}")
  endif()
  file(GLOB_RECURSE deps "${rs_targets_dir}/*.map" "${rs_targets_dir}/*.list" "${rs_targets_dir}/*.toml")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${rs_targets_dir}/targets.py" ${deps})
  set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()
