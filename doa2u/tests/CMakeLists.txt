# Host tests: pure modules only, no game code. Build: cmake --build build --config Release --target host_tests
add_executable(frame_pacer_test frame_pacer_test.c ${CMAKE_SOURCE_DIR}/src/d3d/frame_pacer.c)
target_include_directories(frame_pacer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(apu_downmix_test apu_downmix_test.c ${CMAKE_SOURCE_DIR}/src/apu/apu_downmix.c)
target_include_directories(apu_downmix_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(apu_resample_test apu_resample_test.c ${CMAKE_SOURCE_DIR}/src/apu/apu_resample.c)
target_include_directories(apu_resample_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(apu_hrtf_tap_test apu_hrtf_tap_test.c ${CMAKE_SOURCE_DIR}/src/apu/apu_hrtf_tap.c)
target_include_directories(apu_hrtf_tap_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(movie_names_test movie_names_test.c ${CMAKE_SOURCE_DIR}/src/game/movie_names.c)
target_include_directories(movie_names_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(save_journal_test save_journal_test.c ${CMAKE_SOURCE_DIR}/src/kernel/save_journal.c)
target_include_directories(save_journal_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(apu_reverb_align_test apu_reverb_align_test.c ${CMAKE_SOURCE_DIR}/src/apu/apu_reverb_align.c)
target_include_directories(apu_reverb_align_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_custom_target(host_tests DEPENDS apu_reverb_align_test frame_pacer_test apu_downmix_test apu_resample_test apu_hrtf_tap_test movie_names_test
                  save_journal_test)
