# D3D8→D3D11 Compatibility Layer
# Implements Xbox D3D8 interface backed by Direct3D 11

# SMAA (third_party/smaa, MIT): SMAA.hlsl embedded as bytes for runtime compilation.
file(READ "${CMAKE_SOURCE_DIR}/third_party/smaa/SMAA.hlsl" SMAA_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SMAA_BYTES "${SMAA_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smaa_hlsl.c"
     "const unsigned char g_smaa_hlsl[] = {${SMAA_BYTES}0x00};
const unsigned g_smaa_hlsl_size = sizeof(g_smaa_hlsl) - 1;
")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/smaa/SMAA.hlsl")

add_library(d3d8_compat STATIC
    d3d8_device.c
    d3d8_resources.c
    d3d8_shaders.c
    d3d8_states.c
    d3d8_combiners.c
    d3d8_vsh.c
    frame_pacer.c
    d3d8_smaa.c
    ${CMAKE_CURRENT_BINARY_DIR}/smaa_hlsl.c
)

target_include_directories(d3d8_compat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# D3D11 and DXGI libraries
target_link_libraries(d3d8_compat
    d3d11
    dxgi
    dxguid
    d3dcompiler
    winmm
)

if(MSVC)
    target_compile_options(d3d8_compat PRIVATE
        /W4
        $<$<NOT:$<CONFIG:Debug>>:/O2>
    )
else()
    target_compile_options(d3d8_compat PRIVATE -Wall -Wextra -O2)
endif()
