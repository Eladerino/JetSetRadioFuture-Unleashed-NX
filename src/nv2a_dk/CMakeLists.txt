cmake_minimum_required(VERSION 3.20)
project(nv2a_dk C CXX)

# deko3d renderer for the pushbuffer executor (XBOXRECOMP_DEKO3D=ON), the
# Switch only: deko3d is the console's own GPU interface (devkitPro's
# deko3d package, part of switch-dev).
if(NOT SWITCH)
    message(FATAL_ERROR "nv2a_dk (XBOXRECOMP_DEKO3D) builds for the Nintendo Switch only")
endif()

# dk_compile.c builds the shader pack on the console: dk_xbe_scan.c finds
# the game's shaders, dk_gen.c writes their GLSL from the renderer's shader
# generators, and uam compiles it.
add_library(xbox_nv2a_dk STATIC
    nv2a_dk.c
    nv2a_dk_menu.c
    nv2a_dk_compat.c
    # The shared debug server core (debug_server.*, vendored from
    # SwitchWakerHD through spm-nx: byte-identical, do not fork) and
    # this port's commands for it (jsrf_debug_server.cpp). Off unless
    # dkserver=1 in options.txt; see jsrf_debug_server.cpp.
    debug_server.cpp
    jsrf_debug_server.cpp
    dk_compile.c
    dk_gen.c
    dk_xbe_scan.c
    # The settings menu's front-end: Dear ImGui (vendored under imgui/,
    # the version SwitchWakerHD's overlay runs) on a hand-written
    # deko3d backend, both adapted from that overlay (see imgui/
    # VERSION.txt and the headers of the two .cpp files).
    nv2a_dk_menu_imgui.cpp
    nv2a_dk_imgui_dk.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
target_include_directories(xbox_nv2a_dk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/../d3d
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform
)
target_include_directories(xbox_nv2a_dk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)
# The vendored ImGui translation units are upstream code: silence
# their warnings rather than patch them (they are byte-identical to
# the copy SwitchWakerHD ships).
set_source_files_properties(
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(xbox_nv2a_dk PRIVATE
    IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    # No process control on the Switch: without this, imgui.cpp's
    # default OpenInShellFn references execvp/waitpid and the final
    # link fails (they do not exist in libnx's newlib).
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
option(XBOXRECOMP_DEKO3D_DEBUG "Link deko3d's checking library (deko3dd)" OFF)
if(XBOXRECOMP_DEKO3D_DEBUG)
    target_link_libraries(xbox_nv2a_dk PUBLIC uam deko3dd nx stdc++)
    target_compile_definitions(xbox_nv2a_dk PRIVATE NV2A_DK_CHECKED=1)
else()
    target_link_libraries(xbox_nv2a_dk PUBLIC uam deko3d nx stdc++)
endif()
target_compile_options(xbox_nv2a_dk PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
target_compile_definitions(xbox_nv2a_dk PRIVATE XBOXRECOMP_DEKO3D=1)

# The shader pack's stamp (nv2a_dk_pack.h): a hash of the generators'
# sources, so a pack made by older ones is built anew.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/dk_gen_files.txt _dk_gen_lines)
set(_dk_gen_all "")
set(_dk_gen_deps ${CMAKE_CURRENT_SOURCE_DIR}/dk_gen_files.txt)
foreach(_f IN LISTS _dk_gen_lines)
    if(_f STREQUAL "" OR _f MATCHES "^#")
        continue()
    endif()
    file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/${_f} _h)
    string(APPEND _dk_gen_all "${_h}")
    list(APPEND _dk_gen_deps ${CMAKE_CURRENT_SOURCE_DIR}/${_f})
endforeach()
string(SHA1 _dk_gen_all "${_dk_gen_all}")
string(SUBSTRING "${_dk_gen_all}" 0 16 DK_GEN_STAMP)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_dk_gen_deps})
target_compile_definitions(xbox_nv2a_dk PRIVATE DK_GEN_STAMP=0x${DK_GEN_STAMP}ull)
message(STATUS "deko3d shader generators: stamp ${DK_GEN_STAMP}")
