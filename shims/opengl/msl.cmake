# GLSL -> MSL translator (src/msl/, docs/METAL-DESIGN.md): library linked into the opengl target, tests.
#
#   rsmsl            static library (C++), linked into libRSOpenGL
#   msl_unit         translator / corpus unit tests                       ctest msl.unit
#   msl_verify       GL-vs-Metal render diff (headless)                   ctest msl.verify.builtin
#                    and, with shader dumps, the game's shaders           ctest msl.verify.corpus
#   msl_corpus       target: build the offline corpus from shader dumps (tools/metal/build-msl-corpus.sh)
#
# Shader dumps (game-derived, never committed): RS_MSL_SCALEFORM_DUMPS (RS_GL_DUMP_SHADERS output)
# and RS_MSL_CG_DUMPS (cg_corpus_load --dump-glsl output).
add_library(rsmsl STATIC src/msl/msl_xlat.cpp src/msl/msl_corpus.cpp)
target_include_directories(rsmsl PUBLIC src/msl)
set_target_properties(rsmsl PROPERTIES
  OSX_ARCHITECTURES "arm64;x86_64"
  CXX_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)
target_link_libraries(opengl PRIVATE rsmsl)

add_executable(msl_unit test/msl_unit.c)
target_link_libraries(msl_unit PRIVATE rsmsl)
set_target_properties(msl_unit PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME msl.unit COMMAND msl_unit "${CMAKE_CURRENT_BINARY_DIR}/msl_unit_corpus.bin")
set_tests_properties(msl.unit PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+ passed, 0 failed")

add_executable(msl_verify test/msl_verify.m)
target_compile_options(msl_verify PRIVATE -fobjc-arc)
target_link_libraries(msl_verify PRIVATE rsmsl
  "-framework Foundation" "-framework Metal" "-framework OpenGL" "-framework CoreGraphics" "-framework ImageIO")
set_target_properties(msl_verify PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME msl.verify.builtin COMMAND msl_verify --builtin)

set(RS_MSL_SCALEFORM_DUMPS "${CMAKE_SOURCE_DIR}/analysis/opengl/shaders" CACHE PATH
  "Scaleform GLSL dumps (RS_GL_DUMP_SHADERS output) for the shader corpus")
set(RS_MSL_CG_DUMPS "${CMAKE_SOURCE_DIR}/build/metal/corpus/cg-glsl" CACHE PATH
  "Cg shim GLSL dumps (cg_corpus_load --dump-glsl output) for the shader corpus")
set(msl_corpus_dir "${CMAKE_CURRENT_BINARY_DIR}/msl-corpus")
add_custom_target(msl_corpus
  COMMAND "${CMAKE_SOURCE_DIR}/tools/metal/build-msl-corpus.sh"
          --verify "$<TARGET_FILE:msl_verify>" --out "${msl_corpus_dir}"
          --scaleform "${RS_MSL_SCALEFORM_DUMPS}" --cg "${RS_MSL_CG_DUMPS}"
  DEPENDS msl_verify
  COMMENT "Translating and checking the shader dumps against GL -> ${msl_corpus_dir}/rsmsl-corpus.bin"
  USES_TERMINAL VERBATIM)
# Skipped (exit 77) when no dumps are present.
add_test(NAME msl.verify.corpus
  COMMAND "${CMAKE_SOURCE_DIR}/tools/metal/build-msl-corpus.sh"
          --verify "$<TARGET_FILE:msl_verify>" --out "${CMAKE_CURRENT_BINARY_DIR}/msl-corpus-test"
          --scaleform "${RS_MSL_SCALEFORM_DUMPS}" --cg "${RS_MSL_CG_DUMPS}")
set_tests_properties(msl.verify.corpus PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
