cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0184)
    cmake_policy(SET CMP0184 NEW)
endif()
project(doa2u_recomp VERSION 1.0 LANGUAGES C CXX)

# Release identity, read by Check for Updates and exchanged by online players.
# CI passes its run number; a local build is 0 (a self-built exe). The netplay
# branch publishes to its own channel of GitHub pre-releases, tagged
# "<channel>-v<major>.<minor>.<run>", so both players can download one build.
set(DOA2U_BUILD_NUMBER 0 CACHE STRING "CI run number (0 = a local build)")
set(DOA2U_RELEASE_CHANNEL "netplay" CACHE STRING "GitHub release channel (tag prefix); empty = the main releases")
set(DOA2U_RELEASE_REPO "parkerallan/doa2u-recomp" CACHE STRING "owner/repo whose releases Check for Updates reads")
set(DOA2U_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${DOA2U_BUILD_NUMBER}")
if(DOA2U_RELEASE_CHANNEL)
    set(DOA2U_RELEASE_TAG "${DOA2U_RELEASE_CHANNEL}-v${DOA2U_VERSION}")
else()
    set(DOA2U_RELEASE_TAG "v${DOA2U_VERSION}")
endif()

if(MSVC AND POLICY CMP0184)
    foreach(language C CXX)
        string(REPLACE "/RTC1" "" debug_flags "${CMAKE_${language}_FLAGS_DEBUG}")
        set(CMAKE_${language}_FLAGS_DEBUG "${debug_flags}" CACHE STRING
            "Flags used by the ${language} compiler during Debug builds" FORCE)
    endforeach()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Compatibility layers (static libraries)
add_subdirectory(src/kernel)   # Xbox kernel imports -> Win32, memory layout, fibers
add_subdirectory(src/d3d)      # D3D8 -> D3D11
add_subdirectory(src/audio)    # DirectSound host backend
add_subdirectory(src/input)    # Xbox input -> XInput / keyboard
add_subdirectory(src/apu)      # MCPX APU emulation (from xemu)
add_subdirectory(src/ui)       # Overlay menu (Esc key)
add_subdirectory(src/online)   # Online play (join codes, lockstep sessions)

# Recompiled game executable
add_subdirectory(src/game)
