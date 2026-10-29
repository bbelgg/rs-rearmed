# Metal core (src/metal/, docs/METAL-DESIGN.md): sources added to the opengl target and its tests.
set(ogl_mtl_sources
  src/metal/mtl_context.m
  src/metal/mtl_state.m
  src/metal/mtl_objects.m
  src/metal/mtl_program.m
  src/metal/mtl_draw.m
  src/metal/mtl_extras.m
  src/metal/mtl_cache.m)
target_sources(opengl PRIVATE ${ogl_mtl_sources})
set_source_files_properties(${ogl_mtl_sources} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(opengl PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
if(TARGET rsmsl)
  target_link_libraries(opengl PRIVATE rsmsl)
else()
  # No translator in this build: resolve it at run time (a test program may provide one).
  target_link_options(opengl PRIVATE "LINKER:-U,_rsmsl_translate" "LINKER:-U,_rsmsl_free" "LINKER:-U,_rsmsl_key")
  set_property(SOURCE src/metal/mtl_program.m APPEND PROPERTY COMPILE_DEFINITIONS RSM_MSL_DYNAMIC)
  set_tests_properties(opengl.gfx.offscreen.metal PROPERTIES DISABLED TRUE)
endif()

# --- Tests ------------------------------------------------------------------------------
# metal_core_test runs one GL call sequence through an rs_gfx offscreen context;
# --compare runs it on both backends (child processes) and compares the pixels.
add_executable(metal_core_test test/metal_core_test.c)
add_dependencies(metal_core_test opengl_api)
target_include_directories(metal_core_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)
target_compile_options(metal_core_test PRIVATE -Wno-deprecated-declarations)
target_link_libraries(metal_core_test PRIVATE opengl)

add_executable(metal_cache_test test/metal_cache_test.c)
target_include_directories(metal_cache_test PRIVATE src)
target_compile_options(metal_cache_test PRIVATE -Wno-deprecated-declarations)
target_link_libraries(metal_cache_test PRIVATE opengl)

set(ogl_metal_cases
  clear triangle indexed instanced instance_id client_arrays blend depth stencil scissor viewport
  fbo render_to_texture depth_texture cube_map dxt bgra luminance mipmaps alpha_test polygon_mode subdata_in_flight
  map_buffer readback_orientation tex_subimage unpack_alignment uniforms rebind_attrib queries errors object_lifetime
  mip_redefine fbo_mipmap orphans restart_index stream_subdata stencil_clear_ref pbo_bounds api)
# Programs need the translator (msl.cmake's rsmsl).
if(TARGET rsmsl)
  foreach(c ${ogl_metal_cases})
    add_test(NAME opengl.metal.${c} COMMAND metal_core_test --compare ${c})
    set_tests_properties(opengl.metal.${c} PROPERTIES ${ogl_pass})
  endforeach()
  # Shader/pipeline disk cache: cold, warm, corrupt and stale runs in child processes sharing a
  # cache directory under the build tree (never ~/Library).
  add_test(NAME opengl.metal.cache
    COMMAND metal_cache_test "${CMAKE_CURRENT_BINARY_DIR}/metal-cache-test")
  set_tests_properties(opengl.metal.cache PROPERTIES ${ogl_pass} TIMEOUT 600)
  # All cases in one process on Metal, with error checking: every game import is called.
  add_test(NAME opengl.metal.coverage COMMAND metal_core_test --all)
  set_tests_properties(opengl.metal.coverage PROPERTIES
    ENVIRONMENT "RS_GL_BACKEND=metal;RS_GL_CHECK=1"
    PASS_REGULAR_EXPRESSION "COVERAGE 107/107.*all on metal: [0-9]+ passed, 0 failed")
endif()
