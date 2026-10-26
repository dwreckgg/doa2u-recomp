# In-game overlay (Esc): Dear ImGui + the pad remapping panel, and the
# first-run setup screen that installs the game files from a disc image.

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(doa2u_ui STATIC
    doa2u_ui.cpp
    doa2u_setup.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(doa2u_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
)

target_link_libraries(doa2u_ui PRIVATE
    input_compat
    d3d11
    dxgi
    xinput
    user32
    dwmapi
    comdlg32
    shell32
)

# ImGui is third-party: build it clean but do not hold it to our warning level.
if(MSVC)
    target_compile_options(doa2u_ui PRIVATE /W3 /WX-)
    set_property(TARGET doa2u_ui PROPERTY MSVC_RUNTIME_CHECKS "")
endif()
