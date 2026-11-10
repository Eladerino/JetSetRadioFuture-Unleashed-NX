# USB: MCPX OHCI host controllers, so a title's own XAPI can find a pad

add_library(xbox_usb STATIC
    ohci.c
    usb_gamepad.c
)

target_include_directories(xbox_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xbox_usb PUBLIC platform xbox_input)

if(MSVC)
    target_compile_options(xbox_usb PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_usb PRIVATE -Wall -Wextra -O2)
endif()

# FIX2 (JSRF_GUEST_REGS): the OHCI TU gets the register declarations
# force-included, like the generated code does.
if(JSRF_GUEST_REGS)
    set_source_files_properties(ohci.c
        PROPERTIES COMPILE_OPTIONS
        "-include;${CMAKE_CURRENT_SOURCE_DIR}/../../templates/runtime/recomp_guest_regs.h;-DJSRF_GUEST_REGS")
endif()
