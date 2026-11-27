# The audio code both editions share. Its sources go into the Mac build's override module
# (tools/ws7/recomp/overrides/build.sh) and Learn & Play's libwinaudio.a (win-x86/shims/audio/Makefile); here
# only the tests, on the host's architecture.
set(RS_AUDIO_SOURCES rs_audio_input.c rs_audio_host.c rs_audio_fake.c)
set(RS_AUDIO_FRAMEWORKS "-framework CoreAudio" "-framework CoreFoundation")
add_executable(rs_audio_input_test test/rs_audio_input_test.c ${RS_AUDIO_SOURCES})
target_include_directories(rs_audio_input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rs_audio_input_test PRIVATE ${RS_AUDIO_FRAMEWORKS})
add_test(NAME audio.input COMMAND rs_audio_input_test)
# the test input (rs_audio_fake.h): runs its device for about a second; --off without the switch
add_executable(rs_audio_fake_test test/rs_audio_fake_test.c ${RS_AUDIO_SOURCES})
target_include_directories(rs_audio_fake_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rs_audio_fake_test PRIVATE ${RS_AUDIO_FRAMEWORKS})
add_test(NAME audio.fake COMMAND rs_audio_fake_test)
add_test(NAME audio.fake.off COMMAND rs_audio_fake_test --off)
