# Xbox Input → XInput Compatibility Layer

set(XBOX_INPUT_SOURCES xinput_device.c)
if(APPLE)
    # GameController.framework is in the system on both macOS and iOS and
    # speaks to a Bluetooth DualSense with nothing installed, which SDL2
    # cannot do here -- Homebrew only ships an x86_64 build of it.
    list(APPEND XBOX_INPUT_SOURCES xinput_gc.m)
endif()

add_library(xbox_input STATIC ${XBOX_INPUT_SOURCES})

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# xinput_device.c carries the in-game menu's pad handling (opening it
# from the pad, and keeping the pad from the game while it is open)
# under XBOXRECOMP_MENU. That define is xbox_menu's to give and it
# reaches the modules that link xbox_menu; this one does not, so take
# it here under the same condition that builds the menu. NV2A_GL_MENU
# is an option, so it reads the same in this directory.
if(NV2A_GL_MENU)
    target_compile_definitions(xbox_input PRIVATE XBOXRECOMP_MENU=1)
endif()

if(WIN32 AND NOT XBOXRECOMP_WIN_GL)
    target_link_libraries(xbox_input PUBLIC platform xinput)
elseif(APPLE)
    target_link_libraries(xbox_input PUBLIC platform
        "-framework GameController" "-framework Foundation")
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(xbox_input PUBLIC platform SDL2::SDL2)
endif()

if(MSVC)
    target_compile_options(xbox_input PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
endif()
