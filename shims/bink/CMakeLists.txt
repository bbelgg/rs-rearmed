# libBink2Macx64.dylib replacement backed by a static, minimal FFmpeg
# 7.1.2 + the unmerged Bink 2 decoder (third_party/ffmpeg-bink2.patch).

set(ffmpeg_prefix "${CMAKE_BINARY_DIR}/ffmpeg-bink2")
set(ffmpeg_libs avformat avcodec avutil)

set(ffmpeg_outputs)
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
  foreach(lib IN LISTS ffmpeg_libs)
    list(APPEND ffmpeg_outputs "${ffmpeg_prefix}/${arch}/lib/lib${lib}.a")
  endforeach()
endforeach()
add_custom_command(
  OUTPUT ${ffmpeg_outputs}
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build-ffmpeg.sh" "${ffmpeg_prefix}" ${CMAKE_OSX_ARCHITECTURES}
  DEPENDS build-ffmpeg.sh third_party/ffmpeg-bink2.patch
  COMMENT "Building FFmpeg with Bink 2 (${CMAKE_OSX_ARCHITECTURES})"
  USES_TERMINAL
  VERBATIM)

# Fat archives so the shim links the same way for one or two architectures.
set(ffmpeg_fat)
foreach(lib IN LISTS ffmpeg_libs)
  set(inputs)
  foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    list(APPEND inputs "${ffmpeg_prefix}/${arch}/lib/lib${lib}.a")
  endforeach()
  set(fat "${ffmpeg_prefix}/universal/lib${lib}.a")
  add_custom_command(
    OUTPUT "${fat}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ffmpeg_prefix}/universal"
    COMMAND lipo -create ${inputs} -output "${fat}"
    DEPENDS ${inputs}
    VERBATIM)
  list(APPEND ffmpeg_fat "${fat}")
endforeach()
add_custom_target(ffmpeg_bink2 DEPENDS ${ffmpeg_fat})
list(GET CMAKE_OSX_ARCHITECTURES 0 first_arch)

rs_add_shim(bink2
  INSTALL_NAME @executable_path/libBink2Macx64.dylib
  SOURCES bink_shim.c
  LINK ${ffmpeg_fat})
add_dependencies(bink2 ffmpeg_bink2)
# Headers are identical across architectures.
target_include_directories(bink2 PRIVATE "${ffmpeg_prefix}/${first_arch}/include")
# Export only the Bink API; the static FFmpeg stays private.
set_target_properties(bink2 PROPERTIES C_VISIBILITY_PRESET hidden
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")
target_link_options(bink2 PRIVATE "LINKER:-dead_strip"
  "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")

# --- Standalone test: decode every frame (and audio) of a .bk2 through the API ----
add_executable(bink_play test/bink_play.c)
target_link_libraries(bink_play PRIVATE bink2)
target_include_directories(bink_play PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set(bink_test_dir "${CMAKE_SOURCE_DIR}/analysis/video/video")
if(EXISTS "${bink_test_dir}/credits/cr_video_1a.bk2")
  add_test(NAME bink.decode.kb2j COMMAND bink_play --io "${bink_test_dir}/credits/cr_video_1a.bk2")
  add_test(NAME bink.decode.kb2g
    COMMAND bink_play "${bink_test_dir}/firstencounter/beginner_33_tuner/ge_video_84b.bk2")
  add_test(NAME bink.realtime
    COMMAND bink_play --realtime 25 "${bink_test_dir}/firstencounter/beginner_33_tuner/ge_video_84b.bk2")
  set_tests_properties(bink.decode.kb2j bink.decode.kb2g bink.realtime PROPERTIES PASS_REGULAR_EXPRESSION "OK")
endif()
add_test(NAME bink.exports
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/test/check-exports.sh" "$<TARGET_FILE:bink2>" "${RS_GAME_APP}/Contents/MacOS/Rocksmith2014")
