# Online play: lockstep sessions over join codes
# (docs/superpowers/specs/2026-10-26-netplay-design.md).

add_library(doa2u_online STATIC
    netplay_format.c
    netplay_dump.c
    online_settings.c
)

target_include_directories(doa2u_online
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(doa2u_online PRIVATE /W3 /WX-)
    set_property(TARGET doa2u_online PROPERTY MSVC_RUNTIME_CHECKS "")
    target_compile_definitions(doa2u_online PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
