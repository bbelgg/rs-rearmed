# Cg.framework replacement: the Cg runtime API on Apple's OpenGL (ARB programs
# run as is or translated to GLSL). GL comes from libRSOpenGL (shims/opengl,
# @executable_path/libRSOpenGL.dylib), so the shim's calls take the game's path
# through the wrappers and the selected backend; it re-exports OpenGL.framework.
rs_add_shim(cg
  INSTALL_NAME @executable_path/../Frameworks/Cg.framework/Cg
  SOURCES src/cg_shim.cpp src/arb_parse.cpp src/arb_glsl.cpp
  LINK opengl)
# Export only the Cg API; the translator stays internal.
set_target_properties(cg PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_link_options(cg PRIVATE "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/cg.exports")
set_property(TARGET cg APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/cg.exports")
# Test programs live in build/shims/cg/ and load the shim by its real install
# name, @executable_path/../Frameworks/Cg.framework/Cg, like the game does, and
# libRSOpenGL as @executable_path/libRSOpenGL.dylib (a symlink next to them, as
# Contents/MacOS/libRSOpenGL.dylib in the app).
set(cg_fw "${CMAKE_CURRENT_BINARY_DIR}/../Frameworks/Cg.framework")
add_custom_command(TARGET cg POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${cg_fw}"
  COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:cg>" "${cg_fw}/Cg"
  COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:opengl>" "${CMAKE_CURRENT_BINARY_DIR}/libRSOpenGL.dylib"
  VERBATIM)

foreach(t render_test param_dump corpus_load diff_render stress_test)
  add_executable(cg_${t} tests/${t}.c)
  target_link_libraries(cg_${t} PRIVATE cg opengl)
  target_compile_options(cg_${t} PRIVATE -Wno-deprecated-declarations)
endforeach()

# Every Cg symbol the game imports must be exported.
set(game_x64 "${CMAKE_SOURCE_DIR}/analysis/Rocksmith2014_x64")
if(EXISTS "${game_x64}")
  add_test(NAME cg.exports
    COMMAND bash -c "missing=$(comm -23 <(nm -u \"$1\" | grep '^_cg' | sort) <(nm -gU \"$2\" | awk '{print $3}' | sort)); [ -z \"$missing\" ] || { echo missing: $missing; exit 1; }"
            bash "${game_x64}" $<TARGET_FILE:cg>)
endif()

# Self-contained: hand-written programs with exact expected pixels.
foreach(backend glsl arb)
  add_test(NAME cg.render.${backend} COMMAND cg_render_test)
  set_tests_properties(cg.render.${backend} PROPERTIES ENVIRONMENT "RS_CG_BACKEND=${backend}")
endforeach()

# The game's call patterns (bind caching, Scaleform's programs in between,
# several contexts), with the corpus when it is there (below).
set(cg_stress_args "")
# Against the game's shader corpus (tools/extract_corpus.py) and NVIDIA's own
# answers for it (tools/cg_reference.c). Skipped when they haven't been made.
set(RS_CG_CORPUS "${CMAKE_SOURCE_DIR}/analysis/cg" CACHE PATH "Output of shims/cg/tools/extract_corpus.py")
if(EXISTS "${RS_CG_CORPUS}/corpus")
  file(GLOB cg_corpus "${RS_CG_CORPUS}/corpus/*.?po")
  foreach(backend glsl arb)
    add_test(NAME cg.corpus.load.${backend} COMMAND cg_corpus_load ${cg_corpus})
    set_tests_properties(cg.corpus.load.${backend} PROPERTIES ENVIRONMENT "RS_CG_BACKEND=${backend}")
  endforeach()
  if(EXISTS "${RS_CG_CORPUS}/reference.jsonl")
    add_test(NAME cg.corpus.params
      COMMAND sh -c "\"$1\" \"$2\" \"$4\"/*.?po && python3 -I \"$3\" \"$5\" \"$2\""
              sh $<TARGET_FILE:cg_param_dump> ${CMAKE_CURRENT_BINARY_DIR}/params.jsonl
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_params.py ${RS_CG_CORPUS}/corpus
              ${RS_CG_CORPUS}/reference.jsonl)
  endif()
  # GLSL of every corpus program, as input for the GLSL -> MSL translator.
  add_test(NAME cg.corpus.glsl_dump
    COMMAND cg_corpus_load --dump-glsl ${CMAKE_CURRENT_BINARY_DIR}/glsl-corpus ${cg_corpus})
  set(cg_stress_args ${cg_corpus})
  add_test(NAME cg.corpus.render
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_diff_render.sh $<TARGET_FILE:cg_diff_render>
            ${RS_CG_CORPUS}/corpus ${CMAKE_CURRENT_BINARY_DIR}/renders)
endif()

foreach(backend glsl arb)
  add_test(NAME cg.stress.${backend} COMMAND cg_stress_test ${cg_stress_args})
  set_tests_properties(cg.stress.${backend} PROPERTIES ENVIRONMENT "RS_CG_BACKEND=${backend}")
endforeach()
# The shim's checks of the current program through libRSOpenGL's tracking
# (rs_gl_current_program, here answered by the test), and without it.
add_test(NAME cg.stress.glsl.tracked COMMAND cg_stress_test)
set_tests_properties(cg.stress.glsl.tracked PROPERTIES ENVIRONMENT "RS_CG_BACKEND=glsl;RS_CG_TEST_TRACKED=gl")
add_test(NAME cg.stress.glsl.untracked COMMAND cg_stress_test)
set_tests_properties(cg.stress.glsl.untracked PROPERTIES ENVIRONMENT "RS_CG_BACKEND=glsl;RS_CG_TEST_TRACKED=unknown")
# Pair cache: a run records its pairs, the next ones link them while loading
# (also after a bad line and a cut last line were added to the file).
add_test(NAME cg.stress.pairs
  COMMAND sh -c "set -e; f=\"$2/cg-pairs.txt\"; rm -f \"$f\"; export RS_CG_BACKEND=glsl RS_CG_PAIR_CACHE=\"$f\"
                 \"$1\" --pairs 1; \"$1\" --pairs 2; printf 'not a pair\\n0123' >> \"$f\"; \"$1\" --pairs 3
                 grep -qx '0123' \"$f\" && \"$1\" --pairs 4"
          sh $<TARGET_FILE:cg_stress_test> ${CMAKE_CURRENT_BINARY_DIR})
# libRSOpenGL's Metal backend (glsl only), without its shared contexts.
if(TARGET rsmsl)
  add_test(NAME cg.render.metal COMMAND cg_render_test)
  add_test(NAME cg.stress.metal COMMAND cg_stress_test)
  set_tests_properties(cg.render.metal cg.stress.metal PROPERTIES ENVIRONMENT "RS_GL_BACKEND=metal")
endif()

# libRSOpenGL's default backend is Metal: every other test here runs on GL, as it was written for.
get_property(cg_tests DIRECTORY PROPERTY TESTS)
foreach(t IN LISTS cg_tests)
  if(NOT t MATCHES "\\.metal$")
    set_property(TEST ${t} APPEND PROPERTY ENVIRONMENT "RS_GL_BACKEND=gl")
  endif()
endforeach()

# x86_64 tool that records NVIDIA's runtime's answers for the corpus
# (tools/make_reference.sh); links the Cg.framework from the game install.
set(cg_orig "${RS_GAME_APP}/Contents/Frameworks/Cg.framework/Cg")
if(EXISTS "${cg_orig}")
  add_executable(cg_reference tools/cg_reference.c)
  set_target_properties(cg_reference PROPERTIES OSX_ARCHITECTURES x86_64)
  target_link_libraries(cg_reference PRIVATE "${cg_orig}" "-framework OpenGL")
  target_compile_options(cg_reference PRIVATE -Wno-deprecated-declarations)
endif()
