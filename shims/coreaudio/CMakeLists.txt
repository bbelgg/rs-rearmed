# The audio code both editions share. Its sources go into the Mac build's override module
# (tools/ws7/recomp/overrides/build.sh) and Learn & Play's libwinaudio.a (win-x86/shims/audio/Makefile); here
# only the tests, on the host's architecture.
add_executable(rs_audio_input_test test/rs_audio_input_test.c rs_audio_input.c rs_audio_host.c)
target_include_directories(rs_audio_input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rs_audio_input_test PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
add_test(NAME audio.input COMMAND rs_audio_input_test)
