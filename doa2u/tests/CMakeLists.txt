# Host tests: pure modules only, no game code. Build: cmake --build build --config Release --target host_tests
add_executable(frame_pacer_test frame_pacer_test.c ${CMAKE_SOURCE_DIR}/src/d3d/frame_pacer.c)
target_include_directories(frame_pacer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(apu_downmix_test apu_downmix_test.c ${CMAKE_SOURCE_DIR}/src/apu/apu_downmix.c)
target_include_directories(apu_downmix_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(apu_resample_test apu_resample_test.c ${CMAKE_SOURCE_DIR}/src/apu/apu_resample.c)
target_include_directories(apu_resample_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_custom_target(host_tests DEPENDS frame_pacer_test apu_downmix_test apu_resample_test)
