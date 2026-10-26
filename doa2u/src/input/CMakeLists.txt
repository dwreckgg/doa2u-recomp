# Xbox Input → XInput Compatibility Layer

add_library(input_compat STATIC
    xinput_device.c
    dinput_device.c
    pad_mapping.c
)

target_include_directories(input_compat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(input_compat
    xinput
    dinput8
    dxguid
)

if(MSVC)
    target_compile_options(input_compat PRIVATE
        /W4
        $<$<NOT:$<CONFIG:Debug>>:/O2>
    )
else()
    target_compile_options(input_compat PRIVATE -Wall -Wextra -O2)
endif()

# Load dinput8 on first use: linking it at startup shifts the address space and
# the boot stalls in the CRI loader.
if(MSVC)
    target_link_options(input_compat INTERFACE /DELAYLOAD:dinput8.dll)
    target_link_libraries(input_compat delayimp)
endif()
