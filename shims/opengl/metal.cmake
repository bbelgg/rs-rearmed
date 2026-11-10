# Metal core (src/metal/, docs/mac/METAL.md): sources added to the opengl target and its tests.
set(ogl_mtl_sources
  src/metal/mtl_context.m
  src/metal/mtl_state.m
  src/metal/mtl_objects.m
  src/metal/mtl_program.m
  src/metal/mtl_draw.m
  src/metal/mtl_extras.m
  src/metal/mtl_texlog.m
  src/metal/mtl_render.m
  src/metal/mtl_cache.m)
target_sources(opengl PRIVATE ${ogl_mtl_sources})
set_source_files_properties(${ogl_mtl_sources} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(opengl PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation"
                                     "-weak_framework MetalFX")  # macOS 13+: upscaling at present
if(TARGET rsmsl)
  target_link_libraries(opengl PRIVATE rsmsl)
else()
  # No translator in this build: resolve it at run time (a test program may provide one).
  target_link_options(opengl PRIVATE "LINKER:-U,_rsmsl_translate" "LINKER:-U,_rsmsl_free" "LINKER:-U,_rsmsl_key"
    "LINKER:-U,_rsmsl_corpus_count" "LINKER:-U,_rsmsl_corpus_msl")
  set_property(SOURCE src/metal/mtl_program.m src/metal/mtl_cache.m APPEND PROPERTY COMPILE_DEFINITIONS RSM_MSL_DYNAMIC)
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
if(TARGET rsmsl)
  # Writes the test's shader corpus (rsmsl_corpus_*: also in libRSOpenGL, hidden there).
  target_link_libraries(metal_cache_test PRIVATE rsmsl)
  set_target_properties(metal_cache_test PROPERTIES LINKER_LANGUAGE CXX)
endif()

set(ogl_metal_cases
  clear triangle indexed instanced instance_id client_arrays blend depth stencil scissor viewport
  fbo pass_targets render_to_texture depth_texture cube_map dxt bgra luminance texture_1d mipmaps alpha_test polygon_mode subdata_in_flight
  map_buffer readback_orientation tex_subimage unpack_alignment uniforms rebind_attrib queries errors object_lifetime
  mip_redefine fbo_mipmap orphans restart_index stream_subdata stencil_clear_ref pbo_bounds key_reuse packed_float
  api)
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
  # All cases in one process on Metal, with error checking: every game import is called. And with the
  # pipeline key check (mtl_draw.m): no draw may take a kept key that building it again would not give.
  add_test(NAME opengl.metal.coverage COMMAND metal_core_test --all)
  set_tests_properties(opengl.metal.coverage PROPERTIES
    ENVIRONMENT "RS_GL_BACKEND=metal;RS_GL_CHECK=1;RS_METAL_PKEY_CHECK=1"
    PASS_REGULAR_EXPRESSION "COVERAGE 107/107.*all on metal: [0-9]+ passed, 0 failed"
    FAIL_REGULAR_EXPRESSION "pipeline key check: the kept key is stale")
endif()
