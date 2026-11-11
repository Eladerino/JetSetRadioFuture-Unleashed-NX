cmake_minimum_required(VERSION 3.20)

# JSRF Unleashed -- the game, built from this repository.
#
# The generated code is not in the repository (it is derived from the game's
# executable): produce it from your own default.xbe with the pipeline in
# tools/recomp, into port/recomp/gen (the folder .gitignore keeps out of git),
# or point JSRF_GEN_DIR at a folder named recomp/gen elsewhere. Then:
#
#   cmake -S port -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# On Linux add -DNV2A_GL_CONTEXT=sdl (a window, sound and pads through SDL2);
# the Mac uses its own frameworks by default.
#
# The version is the release tag this tree is at (git describe: "0.1.2", or
# "0.1.2-3-gabc1234-dirty" between releases); a tree without its git history
# (an export) passes it with -DJSRF_VERSION=. The engine prints it on its
# first line and answers --version; the launchers take it from there.

# The Mac build is Apple Silicon only. Said before project(), so that a cmake
# running under Rosetta (an Intel Homebrew's) does not quietly build x86_64.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Mac architecture" FORCE)
endif()

project(jsrf_recomp C)

# The OpenGL in-game menu (src/menu) is the tree's only C++: Dear ImGui and
# its SDL/OpenGL back ends. The Deko3D Switch menu is native C in nv2a_dk.
# Keep C++ enabled here for builds which include the ImGui menu.
enable_language(CXX)

# Nintendo Switch (devkitPro toolchain), as in the runtime's CMakeLists.
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    set(SWITCH TRUE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Two diagnostics the generated code can carry at every call site.
# RECOMP_ABI_CHECK compares ebx, esi and edi around every call (and with
# RECOMP_ABI_RESTORE=1 at run time puts them back); RECOMP_ICALL_FEEDBACK
# records each indirect call's targets for tools/recomp (icall_targets.dump at
# exit). Compiled in, they cost code at every call whether or not anything
# turns them on at run time: spriteice's JSRF-PC measured 46.0 MB of code with
# them and 36.2 MB without. So a Debug build carries them and any other build
# does not, unless asked for here (-DJSRF_ABI_CHECK=ON, -DJSRF_ICALL_FEEDBACK=ON).
# Changing either recompiles every generated file.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_jsrf_diagnostics ON)
else()
    set(_jsrf_diagnostics OFF)
endif()
option(JSRF_ABI_CHECK "Check callee-saved registers around every call" ${_jsrf_diagnostics})
option(JSRF_ICALL_FEEDBACK "Record indirect-call targets for tools/recomp" ${_jsrf_diagnostics})
if(JSRF_ABI_CHECK)
    add_compile_definitions(RECOMP_ABI_CHECK)
endif()
if(JSRF_ICALL_FEEDBACK)
    add_compile_definitions(RECOMP_ICALL_FEEDBACK)
endif()

get_filename_component(JSRF_REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(JSRF_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recomp/gen" CACHE PATH
    "The generated code: a folder named recomp/gen")
if(NOT EXISTS "${JSRF_GEN_DIR}/recomp_types.h")
    message(FATAL_ERROR "No generated code in ${JSRF_GEN_DIR}. Generate it from your "
                        "own default.xbe with tools/recomp (see docs/GETTING_STARTED.md), "
                        "or set JSRF_GEN_DIR.")
endif()
# recomp_manual.c includes "recomp/gen/recomp_types.h": the folder two
# levels above the generated code goes on the include path.
get_filename_component(JSRF_GEN_ROOT "${JSRF_GEN_DIR}/../.." ABSOLUTE)

set(JSRF_VERSION "" CACHE STRING "Version (default: git describe)")
if(NOT JSRF_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --dirty
                        WORKING_DIRECTORY "${JSRF_REPO}"
                        OUTPUT_VARIABLE _jsrf_describe OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE _jsrf_describe_rc)
        if(_jsrf_describe_rc EQUAL 0)
            string(REGEX REPLACE "^v" "" JSRF_VERSION "${_jsrf_describe}")
        endif()
    endif()
endif()
if(NOT JSRF_VERSION)
    set(JSRF_VERSION "dev")
endif()
message(STATUS "JSRF Unleashed version: ${JSRF_VERSION}")

# Windows (MinGW): the runtime's OpenGL / SDL2 host layers, as on Linux and the
# Mac, rather than its Direct3D 11 ones -- the title draws through its own D3D
# and the pushbuffer, which is what nv2a_gl renders everywhere else.
if(WIN32)
    set(XBOXRECOMP_WIN_GL ON)
    add_compile_definitions(XBOXRECOMP_WIN_GL=1)
endif()

# Android: plain char is signed on x86 and on Apple's arm64, everywhere this
# has run so far; the Arm ABI that Android follows makes it unsigned. Signed,
# as it was tested.
if(ANDROID OR SWITCH)
    add_compile_options(-fsigned-char)
endif()

# Switch: thread-local variables through TPIDR_EL0 rather than libnx's soft
# thread pointer. The guest registers are __thread, and the soft one is a
# function call on every access (switch/jsrf_switch.c, JSRF_TP_EL0). Set
# before the runtime's subdirectories so its libraries get it too (the first
# build gave it to the generated code only, and xbox_GuestCpuPreempt's
# thread-locals still went through the call). libnx, SDL and Mesa keep
# -mtp=soft, and the same pointer serves both. JSRF_SOFT_TP=ON goes back.
set(JSRF_TP_EL0 OFF)
if(SWITCH)
    option(JSRF_SOFT_TP "Switch: keep libnx's soft thread pointer" OFF)
    if(NOT JSRF_SOFT_TP)
        include(CheckCCompilerFlag)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        check_c_compiler_flag(-mtp=tpidr_el0 JSRF_HAVE_MTP_EL0)
        unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
        if(JSRF_HAVE_MTP_EL0)
            set(JSRF_TP_EL0 ON)
            add_compile_options(-mtp=tpidr_el0)
            add_compile_definitions(JSRF_TP_EL0=1)
        else()
            message(STATUS "Switch: compiler has no -mtp=tpidr_el0; soft thread pointer")
        endif()
    endif()
endif()
# Fast guest memory in the generated code (templates/runtime/recomp_types.h,
# RECOMP_FAST_MEM): on by default on the Switch, JSRF_SLOW_MEM=ON for the
# old code generation, to compare.
if(SWITCH)
    option(JSRF_SLOW_MEM "Switch: generated code without the fast guest memory path" OFF)
    if(JSRF_SLOW_MEM)
        add_compile_definitions(RECOMP_NO_FAST_MEM=1)
    endif()
endif()

# What the compiler is told, in the build output: the generated code is most
# of the frame, and its optimisation flags come from the toolchain file.
string(TOUPPER "${CMAKE_BUILD_TYPE}" JSRF_BT)
message(STATUS "C flags: ${CMAKE_C_FLAGS} | ${CMAKE_BUILD_TYPE}: ${CMAKE_C_FLAGS_${JSRF_BT}}")
# JSRF_GEN_OPT: options for the generated code only, after the flags above,
# so a later -O wins: -O3 to try the compiler's wider inlining and
# vectorising on the game's own code (devkitPro's Release is -O2). Empty by
# default, and tools/switch/build.sh sets it each time from the environment
# so a value never lingers in the cache from an earlier build.
set(JSRF_GEN_OPT "" CACHE STRING "Compile options added for the generated code (e.g. -O3)")
if(JSRF_GEN_OPT)
    message(STATUS "generated code: the flags above, then ${JSRF_GEN_OPT} (JSRF_GEN_OPT)")
else()
    message(STATUS "generated code: the flags above only (JSRF_GEN_OPT unset)")
endif()

# JSRF_GEN_OPT_O3_TUS: generated TUs raised to -O3 over the JSRF_GEN_OPT
# base. The Switch default raises every generated TU except recomp_0008:
# recomp_0008 is the sole carrier TU of the -O3 dual-access-path crash
# (the campaign bisect record, Phase 3, proven in both directions), so
# it stays at the -O2 base -- belt-and-suspenders behind the structural
# fix, at zero measured cost. tools/switch/build.sh forwards the list
# on every build so no value lingers in the cache from an earlier one.
if(SWITCH)
    set(JSRF_GEN_OPT_O3_TUS_DEFAULT "recomp_0000 recomp_0001 recomp_0002 recomp_0003 recomp_0004 recomp_0005 recomp_0006 recomp_0007 recomp_0009 recomp_dispatch recomp_stubs_unresolved recomp_tables_none")
else()
    set(JSRF_GEN_OPT_O3_TUS_DEFAULT "")
endif()
set(JSRF_GEN_OPT_O3_TUS "${JSRF_GEN_OPT_O3_TUS_DEFAULT}" CACHE STRING
    "Generated TUs additionally compiled -O3 over JSRF_GEN_OPT")
set(JSRF_GEN_OPT_O3_TUS_LIST "${JSRF_GEN_OPT_O3_TUS}")
string(REPLACE ";" " " JSRF_GEN_OPT_O3_TUS_LIST "${JSRF_GEN_OPT_O3_TUS_LIST}")
separate_arguments(JSRF_GEN_OPT_O3_TUS_LIST NATIVE_COMMAND "${JSRF_GEN_OPT_O3_TUS_LIST}")

# JSRF_GUEST_REGS (FIX2): the guest integer register file as AArch64
# global register variables -- templates/runtime/recomp_guest_regs.h,
# force-included ahead of the guest-code TUs, with
# src/kernel/recomp_guest_regs.c as the call seam. Defaults ON for
# Switch builds, OFF elsewhere; with it OFF every change it gates is
# inert and the register file is the shipped thread-local one.
if(SWITCH)
    set(JSRF_GUEST_REGS_DEFAULT ON)
else()
    set(JSRF_GUEST_REGS_DEFAULT OFF)
endif()
set(JSRF_GUEST_REGS ${JSRF_GUEST_REGS_DEFAULT} CACHE BOOL
    "Guest registers as AArch64 global register variables (FIX2)")

# JSRF_PGO_GENERATE / JSRF_PGO_USE (gen-PGO campaign): instrument the
# generated TUs with -fprofile-generate, or rebuild them against a
# collected profile with -fprofile-use. Mutually exclusive. Host TUs
# are never instrumented. The run's DESC gains " PGO[gen]".
option(JSRF_PGO_GENERATE "Instrument the generated TUs for profile collection" OFF)
option(JSRF_PGO_USE "Build the generated TUs against a collected profile" OFF)
if(JSRF_PGO_GENERATE AND JSRF_PGO_USE)
    message(FATAL_ERROR "JSRF_PGO_GENERATE and JSRF_PGO_USE are mutually exclusive")
endif()

# The runtime: everything in this repository outside port/ and launcher/.
add_subdirectory("${JSRF_REPO}" "${CMAKE_BINARY_DIR}/xboxrecomp")

file(GLOB JSRF_GEN_SOURCES CONFIGURE_DEPENDS "${JSRF_GEN_DIR}/*.c")

# recomp_manual.c wraps some generated functions: it declares the generated
# body as sub_X_gen ("extern void sub_X_gen(void);") and defines sub_X itself.
# tools/recomp names such a body sub_X_gen when it generates the code; code
# generated before a wrap was added still calls it sub_X. A generated file
# like that is compiled from a copy in the build folder with the body renamed,
# so older generated code -- the release builds' included -- needs no editing
# by hand. Files that need nothing are compiled where they are.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/recomp_manual.c" _jsrf_wrap_lines
     REGEX "^extern void sub_[0-9A-Fa-f]+_gen\\(void\\);")
set(_jsrf_wraps "")
foreach(_l IN LISTS _jsrf_wrap_lines)
    string(REGEX REPLACE "^extern void (sub_[0-9A-Fa-f]+)_gen\\(void\\);.*$" "\\1" _n "${_l}")
    list(APPEND _jsrf_wraps "${_n}")
endforeach()
set(_jsrf_gen_sources "")
foreach(_src IN LISTS JSRF_GEN_SOURCES)
    file(READ "${_src}" _txt)
    set(_renamed "")
    foreach(_n IN LISTS _jsrf_wraps)
        string(FIND "${_txt}" "\nvoid ${_n}(void)\n" _at)
        if(NOT _at EQUAL -1)
            string(REPLACE "\nvoid ${_n}(void)\n" "\nvoid ${_n}_gen(void)\n" _txt "${_txt}")
            list(APPEND _renamed "${_n}")
        endif()
    endforeach()
    if(_renamed)
        get_filename_component(_name "${_src}" NAME)
        set(_copy "${CMAKE_CURRENT_BINARY_DIR}/gen_wrapped/${_name}")
        file(WRITE "${_copy}.new" "${_txt}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_copy}.new" "${_copy}")
        file(REMOVE "${_copy}.new")
        list(APPEND _jsrf_gen_sources "${_copy}")
        message(STATUS "Generated code: ${_name} compiled with ${_renamed} renamed _gen (wrapped in recomp_manual.c)")
    else()
        list(APPEND _jsrf_gen_sources "${_src}")
    endif()
    unset(_txt)
endforeach()
set(JSRF_GEN_SOURCES ${_jsrf_gen_sources})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/recomp_manual.c")

# The release builds' generated code carries two tables of functions an
# earlier discovery pass missed (recomp_seeded.c, recomp_holes.c; see
# recomp_manual.c). Code generated with today's tools/recomp has neither:
# empty ones stand in, so both kinds of generated code build.
set(JSRF_TABLES_NONE "")
if(NOT EXISTS "${JSRF_GEN_DIR}/recomp_seeded.c")
    list(APPEND JSRF_TABLES_NONE JSRF_NO_SEEDED)
endif()
if(NOT EXISTS "${JSRF_GEN_DIR}/recomp_holes.c")
    list(APPEND JSRF_TABLES_NONE JSRF_NO_HOLES)
endif()
if(JSRF_TABLES_NONE)
    list(APPEND JSRF_GEN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/recomp_tables_none.c")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/recomp_tables_none.c"
        PROPERTIES COMPILE_DEFINITIONS "${JSRF_TABLES_NONE}")
endif()
# The generated code always gets the semantic flags first: the lifted
# code assumes x86 semantics -- wrapping overflow, type punning, and a
# rounding at every step (no fused multiply-add). skeammer's build
# established these three; the fp/simd locals passes require the first
# outright, because with values in registers the compiler would
# otherwise fuse a multiply and an add the Xbox rounded separately.
# JSRF_GEN_OPT's options are appended after them when set, so a later
# -O there still wins.
set(JSRF_GEN_SEMANTIC_FLAGS "-ffp-contract=off;-fwrapv;-fno-strict-aliasing")
set(JSRF_GEN_COMPILE_OPTIONS "${JSRF_GEN_SEMANTIC_FLAGS}")
if(JSRF_GEN_OPT)
    separate_arguments(JSRF_GEN_OPT_LIST NATIVE_COMMAND "${JSRF_GEN_OPT}")
    list(APPEND JSRF_GEN_COMPILE_OPTIONS ${JSRF_GEN_OPT_LIST})
endif()
set_source_files_properties(${JSRF_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "${JSRF_GEN_COMPILE_OPTIONS}")
# And the run's own log says it ([MANUAL] generated code: ..., from
# recomp_manual.c), so a test run's log tells which build it came from.
set(JSRF_GEN_OPT_DESC "${JSRF_GEN_OPT}")
if(JSRF_GEN_OPT_O3_TUS_LIST)
    string(REPLACE ";" "," _jsrf_o3_csv "${JSRF_GEN_OPT_O3_TUS_LIST}")
    set(JSRF_GEN_OPT_DESC "${JSRF_GEN_OPT_DESC} O3[${_jsrf_o3_csv}]")
endif()
if(JSRF_PGO_GENERATE OR JSRF_PGO_USE)
    set(JSRF_GEN_OPT_DESC "${JSRF_GEN_OPT_DESC} PGO[gen]")
endif()
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/recomp_manual.c" APPEND PROPERTY
    COMPILE_DEFINITIONS "JSRF_GEN_OPT_DESC=\"${JSRF_GEN_OPT_DESC}\"")

# JSRF_GEN_OPT_O3_TUS application: -O3 appended after the base options
# for the listed TUs only, so it wins over JSRF_GEN_OPT there and
# nowhere else.
foreach(_src IN LISTS JSRF_GEN_SOURCES)
    get_filename_component(_n "${_src}" NAME_WE)
    if(_n IN_LIST JSRF_GEN_OPT_O3_TUS_LIST)
        set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS "-O3")
    endif()
endforeach()

# JSRF_GUEST_REGS application: the register declarations force-included
# ahead of everything else in the guest-code TUs, the define beside
# them, appended last so a gen object's recorded options read
# ..., -O3, -include ..., -DJSRF_GUEST_REGS. The runtime TUs under
# src/ carry the same block in their own CMakeLists.
if(JSRF_GUEST_REGS)
    set(JSRF_GUEST_REGS_HDR "${JSRF_REPO}/templates/runtime/recomp_guest_regs.h")
    foreach(_src IN LISTS JSRF_GEN_SOURCES)
        set_property(SOURCE "${_src}" APPEND PROPERTY
            COMPILE_OPTIONS "-include;${JSRF_GUEST_REGS_HDR};-DJSRF_GUEST_REGS")
    endforeach()
    set_property(SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/recomp_manual.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/switch/jsrf_switch.c"
        APPEND PROPERTY
        COMPILE_OPTIONS "-include;${JSRF_GUEST_REGS_HDR};-DJSRF_GUEST_REGS")
endif()

# JSRF_PGO application: the profile flags go on the generated TUs
# only, appended after every other per-TU option (the recorded
# per-object order is ..., -O3, -include ..., -DJSRF_GUEST_REGS,
# -fprofile-generate, ...), and the generate flag on the link.
if(JSRF_PGO_GENERATE OR JSRF_PGO_USE)
    if(JSRF_PGO_GENERATE)
        set(_jsrf_pgo_flag "-fprofile-generate")
    else()
        # -fprofile-correction: the gen TUs execute on multiple worker
        # threads with -fprofile-update=single counters, so harvested
        # counts tear slightly and are not flow-consistent; let GCC
        # repair them instead of rejecting the profile outright.
        set(_jsrf_pgo_flag "-fprofile-use;-fprofile-correction")
    endif()
    foreach(_src IN LISTS JSRF_GEN_SOURCES)
        set_property(SOURCE "${_src}" APPEND PROPERTY
            COMPILE_OPTIONS "${_jsrf_pgo_flag};-fno-profile-values;-fprofile-update=single")
    endforeach()
endif()

# The gcda path strip is baked from the build directory's depth, and
# the pgo runtime TU joins the Switch glue whenever this tree builds
# for the console; its start call is gated on JSRF_PGO_GENERATE.
# _jsrf_bin_n counts one component too many: splitting an absolute
# path on "/" yields a leading empty component, so the strip
# subtracts 2, not 1.  (At components - 1 the strip also ate the
# build-switch component and the .gcda landed one level shallow:
# pgo/CMakeFiles/... instead of pgo/build-switch/CMakeFiles/....)
string(REPLACE "/" ";" _jsrf_bin_parts "${CMAKE_BINARY_DIR}")
list(LENGTH _jsrf_bin_parts _jsrf_bin_n)
math(EXPR JSRF_PGO_GCOV_STRIP "${_jsrf_bin_n} - 2")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/switch/jsrf_pgo.c" APPEND PROPERTY
    COMPILE_DEFINITIONS "JSRF_PGO_GCOV_STRIP=${JSRF_PGO_GCOV_STRIP}")
if(JSRF_PGO_GENERATE)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/switch/jsrf_switch.c" APPEND PROPERTY
        COMPILE_DEFINITIONS "JSRF_PGO_GENERATE=1")
endif()

if(ANDROID)
    # Android: the engine is libmain.so, which SDL's Java side loads and whose
    # SDL_main it calls (main.c; android/jsrf_android.c sets the scene). The
    # app around it -- launcher/android -- is built by launcher/android/build_apk.sh.
    add_library(jsrf_recomp SHARED
        main.c
        recomp_manual.c
        camera_walls.c
        resource_load.c
        xbe_check.c
        android/jsrf_android.c
        ios/xiso.c
        ${JSRF_GEN_SOURCES}
    )
    set_target_properties(jsrf_recomp PROPERTIES OUTPUT_NAME main)
else()
    add_executable(jsrf_recomp
        main.c
        recomp_manual.c
        camera_walls.c
        resource_load.c
        xbe_check.c
        ios/xiso.c
        ${JSRF_GEN_SOURCES}
    )
endif()
# The shader seed (src/nv2a_gl/nv2a_gl.c, seed_import): the states of the
# shaders the game draws with, collected by playing it, carried in the engine
# as bytes made from port/shader_seed.dat at configure time -- none without it.
# Only rewritten when it changes, so a reconfigure does not rebuild it.
set(JSRF_SEED_DAT "${CMAKE_CURRENT_SOURCE_DIR}/shader_seed.dat")
set(JSRF_SEED_C "${CMAKE_CURRENT_BINARY_DIR}/shader_seed.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${JSRF_SEED_DAT}")
if(EXISTS "${JSRF_SEED_DAT}")
    file(READ "${JSRF_SEED_DAT}" _jsrf_seed_hex HEX)
    string(LENGTH "${_jsrf_seed_hex}" _jsrf_seed_n)
    math(EXPR _jsrf_seed_len "${_jsrf_seed_n} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _jsrf_seed_bytes "${_jsrf_seed_hex}")
    # 24 bytes a line (CMake's expressions have no {n}: the pattern spelt out)
    string(REPEAT "0x[0-9a-f][0-9a-f]," 24 _jsrf_seed_line)
    string(REGEX REPLACE "(${_jsrf_seed_line})" "\\1\n" _jsrf_seed_bytes "${_jsrf_seed_bytes}")
    message(STATUS "Shader seed: ${_jsrf_seed_len} bytes")
else()
    set(_jsrf_seed_len 0)
    set(_jsrf_seed_bytes "0")
    message(STATUS "Shader seed: none (no port/shader_seed.dat)")
endif()
file(WRITE "${JSRF_SEED_C}.new"
    "/* Made by port/CMakeLists.txt from port/shader_seed.dat. */\n"
    "#include <stddef.h>\n"
    "const unsigned char jsrf_shader_seed[] = {\n${_jsrf_seed_bytes}\n};\n"
    "const size_t jsrf_shader_seed_len = ${_jsrf_seed_len};\n")
configure_file("${JSRF_SEED_C}.new" "${JSRF_SEED_C}" COPYONLY)
target_sources(jsrf_recomp PRIVATE "${JSRF_SEED_C}")

if(IOS)
    # The iPhone / iPad app around the engine (ios/jsrf_ios.m: the window,
    # the controls, the game in Documents; the disc-image unpacker, ios/xiso.c,
    # is in every build -- the Mac and Linux apps use it through the engine). The
    # bundle -- Info.plist, icons, signature -- is put together by
    # launcher/ios/package_ios.sh from this executable.
    target_sources(jsrf_recomp PRIVATE ios/jsrf_ios.m)
    set_source_files_properties(ios/jsrf_ios.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(jsrf_recomp PROPERTIES MACOSX_BUNDLE OFF)
    target_link_libraries(jsrf_recomp PRIVATE
        "-framework UIKit" "-framework GameController" "-framework AVFoundation"
        "-framework Metal" "-framework QuartzCore" "-framework CoreGraphics"
        "-framework Foundation")
endif()
if(SWITCH)
    # The .nro's glue (switch/jsrf_switch.c: SD layout, log, environment,
    # first-launch unpack, 16 MB host thread), and the include for
    # "xiso.h" the unpacker shares with Android, plus src/ for main.c's
    # "platform/switch_backtrace.h".
    target_sources(jsrf_recomp PRIVATE switch/jsrf_switch.c
                                       switch/switch_posix.c
        switch/jsrf_pgo.c)

# The profile flag also goes on the link (see the PGO block above);
# here, after the target exists.
if(JSRF_PGO_GENERATE)
    target_link_options(jsrf_recomp PRIVATE -fprofile-generate)
elseif(JSRF_PGO_USE)
    target_link_options(jsrf_recomp PRIVATE -fprofile-use)
endif()
    if(XBOXRECOMP_DEKO3D)
        target_compile_definitions(jsrf_recomp PRIVATE JSRF_DEKO3D=1)
    endif()
    target_include_directories(jsrf_recomp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ios"
        "${JSRF_REPO}/src")
endif()
target_compile_definitions(jsrf_recomp PRIVATE JSRF_VERSION="${JSRF_VERSION}")
target_link_libraries(jsrf_recomp PRIVATE xboxrecomp)
if(JSRF_TP_EL0)
    # Every thread copies the pointer into TPIDR_EL0 as it starts.
    target_link_options(jsrf_recomp PRIVATE -Wl,--wrap=threadCreate)
endif()
target_include_directories(jsrf_recomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${JSRF_GEN_ROOT}"
    "${JSRF_GEN_DIR}"
    "${JSRF_REPO}/include"
)
if(WIN32 AND XBOXRECOMP_WIN_GL)
    # The icon, a manifest (UTF-8 file names, a sharp window on a high-DPI
    # screen) and version information, shared with the launcher
    # (launcher/windows).
    enable_language(RC)
    string(REGEX MATCH "^([0-9]+)[.]([0-9]+)[.]([0-9]+)" _jsrf_v "${JSRF_VERSION}")
    if(_jsrf_v)
        set(_jsrf_vnum "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
    else()
        set(_jsrf_vnum "0,0,0,0")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsrf_version.h"
         "#define JSRF_VER_NUM ${_jsrf_vnum}\n#define JSRF_VER_STR \"${JSRF_VERSION}\"\n")
    target_sources(jsrf_recomp PRIVATE "${JSRF_REPO}/launcher/windows/resources.rc")
    target_include_directories(jsrf_recomp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    set_source_files_properties("${JSRF_REPO}/launcher/windows/resources.rc" PROPERTIES
        COMPILE_DEFINITIONS "JSRF_ENGINE")
    # An 8 MB stack for the main thread and for the threads the title creates
    # (CreateThread with size 0 takes the image's), as a Linux main thread has:
    # recompiled functions nest as deeply as the guest's calls, and Windows'
    # default is 1 MB.
    find_package(Threads REQUIRED)
    target_link_libraries(jsrf_recomp PRIVATE Threads::Threads winmm dbghelp)
    target_link_options(jsrf_recomp PRIVATE -Wl,--stack,0x800000)
    # The linker map names the function at any jsrf_recomp.exe+offset a crash
    # report prints (kept with a release build, not shipped).
    target_link_options(jsrf_recomp PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/jsrf_recomp.map")
elseif(WIN32)
    target_link_libraries(jsrf_recomp PRIVATE d3d11 dxgi dxguid xinput winmm dbghelp)
elseif(ANDROID)
    # pthreads are in bionic's libc; log and android for the glue.
    target_include_directories(jsrf_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ios")
    target_link_libraries(jsrf_recomp PRIVATE m dl log android)
    # -Bsymbolic: the game's functions call one another directly rather than
    # through the PLT, as in an executable (nothing ever interposes on them);
    # they stay in the dynamic symbol table, which is what names them in a
    # crash report.
    target_link_options(jsrf_recomp PRIVATE -Wl,--no-undefined -Wl,-Bsymbolic)
elseif(SWITCH)
    # pthreads and the C library are inside libnx/newlib; there is no
    # libdl and no -rdynamic (statically linked .nro). The map file is
    # kept, as on Windows: it is what a crash offset resolves against.
    target_link_libraries(jsrf_recomp PRIVATE m)
    target_link_options(jsrf_recomp PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/jsrf_recomp.map")
else()
    target_link_libraries(jsrf_recomp PRIVATE pthread m dl)
    target_link_options(jsrf_recomp PRIVATE -rdynamic)
endif()
if(SWITCH)
    # devkitPro's Switch.cmake turns the linked ELF into the .nro. The
    # NACP generated here is what the homebrew menu shows for it: with
    # none, the tools fall back to their defaults (author "Unknown",
    # version "1.0.0"). The display version is the release's numeric
    # core; JSRF_VERSION itself, suffix and describe tail and all, is
    # what the engine prints in its log and About.
    string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" JSRF_DISPLAY_VERSION "${JSRF_VERSION}")
    if(NOT JSRF_DISPLAY_VERSION)
        set(JSRF_DISPLAY_VERSION "0.1.0")
    endif()
    nx_generate_nacp(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/jsrf_recomp.nacp"
        NAME "JSRF Unleashed NX"
        AUTHOR "Eladerino & JuMatt"
        VERSION "${JSRF_DISPLAY_VERSION}")
    # An icon only when one is handed to the build from outside the
    # repo (JSRF_ICON, a 256x256 baseline JPEG): the icon is part of
    # the distributed .nro, so no game artwork ships in it. Without
    # one, devkitPro's default icon is used, as before.
    set(JSRF_ICON "" CACHE FILEPATH "Optional .nro icon (256x256 JPEG) kept outside the repo")
    if(JSRF_ICON AND EXISTS "${JSRF_ICON}")
        message(STATUS "NRO icon (from outside the repo): ${JSRF_ICON}")
        nx_create_nro(jsrf_recomp
            NACP "${CMAKE_CURRENT_BINARY_DIR}/jsrf_recomp.nacp"
            ICON "${JSRF_ICON}")
    else()
        nx_create_nro(jsrf_recomp
            NACP "${CMAKE_CURRENT_BINARY_DIR}/jsrf_recomp.nacp")
    endif()
endif()
if(MSVC)
    target_link_options(jsrf_recomp PRIVATE /LARGEADDRESSAWARE)
    target_compile_options(jsrf_recomp PRIVATE /bigobj)
endif()
