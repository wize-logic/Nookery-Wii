# Nookery for the Wii: the game's sources built with devkitPPC over the SDK's
# own GX (libs/dolphin) and the port's OS, VI, PAD, DVD, CARD, ARAM and AI over
# libogc and wiilib (wii/src). Configure with the "wii" preset.
include_guard(GLOBAL)

get_filename_component(NK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(NK_WII "${NK_ROOT}/wii")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# No unwind tables: nothing here unwinds (the game's C++ has no exceptions)
add_compile_options(-fno-asynchronous-unwind-tables -fno-unwind-tables)

option(NOOKERY_WII_NET "Build the online play (lobbies, P2P sessions)" ON)

# =============================================================================
# What every game and SDK source is compiled with
# =============================================================================
add_library(nk_wii_flags INTERFACE)
# The SDK's declarations are Aurora's (extern/aurora/include), as the PC
# port's: they give the GameCube SDK's own signatures where TARGET_PC is not
# defined, and are what the game's sources are kept against. The SDK's
# sources built below keep their own (libs/dolphin/include).
target_include_directories(nk_wii_flags INTERFACE
    ${NK_WII}/include
    ${NK_ROOT}/pc/lib/kyureki
    ${NK_ROOT}/include
    ${NK_ROOT}/extern/aurora/include
    ${NK_ROOT}/src
    ${NK_ROOT}
    ${NK_ROOT}/pc/include
)
target_compile_definitions(nk_wii_flags INTERFACE
    TARGET_WII=1
    ACGC_PORT=1
    VERSION=0
    F3DEX_GBI_2
    NDEBUG
    BUGFIXES
    PC_ENHANCEMENTS
    # the PC port's pointer in the game's menus and for walking (a mouse
    # there; the Wii Remote's pointer here: wii/src/ogc/pointer.c, its hand
    # drawn by src/game/m_hand_ovl.c)
    MOUSE_INPUT
    _LANGUAGE_C
)
target_compile_options(nk_wii_flags INTERFACE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20 -fno-exceptions -fno-rtti -fpermissive>
    -fsigned-char
    -fno-strict-aliasing
    -fwrapv
    -fno-common
    # The game's small variables would overflow the 64 KiB r13 reaches, which
    # libogc's and newlib's small data need
    -G0
    # The decompiled sources do what MWCC compiled as written: GCC is not to
    # optimise on their undefined behaviour (reads past an array's end,
    # uninitialised locals, null checks)
    "SHELL:-include ${NK_WII}/include/nookery_wii/game_compat.h"
    -fno-aggressive-loop-optimizations
    -fno-delete-null-pointer-checks
    -ftrivial-auto-var-init=zero
    -O2
    -g
)

# =============================================================================
# The game (src/): what the PC build compiles, less its host replacements
# =============================================================================
file(GLOB_RECURSE NK_GAME_C CONFIGURE_DEPENDS "${NK_ROOT}/src/*.c")
file(GLOB_RECURSE NK_GAME_CXX CONFIGURE_DEPENDS "${NK_ROOT}/src/*.cpp")
foreach(_re
        ".*/AUS/.*"
        ".*/TRK_MINNOW_DOLPHIN/.*"
        ".*/Runtime\\.PPCEABI\\.H/.*"
        ".*/MSL_C\\.PPCEABI\\.bare\\.H/.*"
        # f_furniture.c includes every src/furniture/*.c as one unit
        ".*/src/furniture/.*"
        ".*/src/data/model/obj_e_boat/.*"
        ".*/ac_npc_shop_common\\.c$"
        ".*/ac_animal_logo_misc\\.c$"
        ".*/m_item_debug\\.c$"
        ".*/actor/npc/.*_anime\\.c$"
        ".*/jaudio_NES/internal/dsp_cardunlock\\.c$"
        ".*/jaudio_NES/internal/dsp_GBAKey\\.c$"
        # newlib's malloc and printf family serve the game, as on PC
        ".*/libc64/malloc\\.c$"
        ".*/libc64/sprintf\\.c$"
        # (newlib has bcopy and the printf family the N64 library's provide)
        ".*/libultra/ultra\\.c$"
        ".*/libultra/xprintf\\.c$"
        ".*/libultra/xlitob\\.c$"
        ".*/libultra/xldtob\\.c$"
        ".*/ReconfigBATs\\.c$"
        # The port's OSReport (wii/src/os.c) writes the log, as the PC's does
        ".*/libforest/osreport\\.c$"
        # The program's start is the port's (wii/src/main.c)
        ".*/executor\\.c$"
        ".*/ef_effect_lib\\.c$"
        ".*/emu64_utility\\.c$"
        # The calendar is the PC port's (pc/lib/kyureki: dates to the year 3000)
        ".*/src/lb_reki\\.c$")
    list(FILTER NK_GAME_C EXCLUDE REGEX "${_re}")
endforeach()
foreach(_re
        ".*/Runtime\\.PPCEABI\\.H/.*"
        ".*/emu64_print\\.cpp$"
        ".*/jsyswrapper_ext\\.cpp$"
        ".*/jsyswrapper_main\\.cpp$")
    list(FILTER NK_GAME_CXX EXCLUDE REGEX "${_re}")
endforeach()

set(NK_KYUREKI
    ${NK_ROOT}/pc/lib/kyureki/kyureki.c
    ${NK_ROOT}/pc/lib/kyureki/astro_builtin.c
    ${NK_ROOT}/pc/lib/kyureki/lb_reki_adapter.c)

# The port's shared pieces from the PC layer: the asset loader (the game's
# tables read from the player's disc image) and the GCI helpers
set(NK_PC_SHARED
    ${NK_ROOT}/pc/src/pc_assets.c)

# src/data apart: its tables go to MEM2 (wii/nookery.ld)
set(NK_DATA_C ${NK_GAME_C})
list(FILTER NK_DATA_C INCLUDE REGEX "${NK_ROOT}/src/data/.*")
list(FILTER NK_GAME_C EXCLUDE REGEX "${NK_ROOT}/src/data/.*")
add_library(nk_data STATIC ${NK_DATA_C})
target_link_libraries(nk_data PRIVATE nk_wii_flags)
target_compile_options(nk_data PRIVATE -w -O1)

add_library(nk_game STATIC ${NK_GAME_C} ${NK_GAME_CXX} ${NK_KYUREKI} ${NK_PC_SHARED})
target_link_libraries(nk_game PRIVATE nk_wii_flags)
# The decompiled sources read past some tables' ends where the GameCube's
# compiler laid the next one out (e.g. m_huusui_room_ovl.c's, BUGFIXES):
# GCC keeps each file's statics in their order
target_compile_options(nk_game PRIVATE -w -fno-toplevel-reorder)
set_source_files_properties(${NK_KYUREKI} PROPERTIES COMPILE_OPTIONS -fno-fast-math)
set_source_files_properties(
    "${NK_ROOT}/src/static/libforest/emu64/emu64.c"
    "${NK_ROOT}/src/static/jaudio_NES/internal/ja_calc.c"
    "${NK_ROOT}/src/static/jaudio_NES/internal/jammain_2.c"
    "${NK_ROOT}/src/static/jaudio_NES/game/game64.c"
    PROPERTIES LANGUAGE CXX)
# m_card.c reaches the SDK's CARD internals (as the decomp's include paths
# give them)
set_source_files_properties("${NK_ROOT}/src/game/m_card.c" PROPERTIES
    INCLUDE_DIRECTORIES "${NK_ROOT}/extern/aurora/include/dolphin;${NK_ROOT}/libs/dolphin/src")
# The two mains: the REL's (main.c) and the DOL's (boot.c)
set_source_files_properties("${NK_ROOT}/src/main.c"
    PROPERTIES COMPILE_DEFINITIONS "main=ac_entry")
set_source_files_properties("${NK_ROOT}/src/static/boot.c"
    PROPERTIES COMPILE_DEFINITIONS "main=boot_main")

# =============================================================================
# The SDK's own sources, where they run on the Wii unchanged: GX drives the
# same registers and FIFO; the rest is plain C. Functions written in MWCC's
# assembly are left out of the copies built here (wii/tools/strip_asm.py) and
# provided by wii/src.
# =============================================================================
set(_sdk_root "${NK_ROOT}/libs/dolphin/src")
set(_sdk_sources
    gx/GXAttr.c gx/GXBump.c gx/GXDisplayList.c gx/GXFifo.c
    gx/GXFrameBuf.c gx/GXGeometry.c gx/GXInit.c gx/GXLight.c gx/GXMisc.c
    gx/GXPerf.c gx/GXPixel.c gx/GXSave.c gx/GXStubs.c gx/GXTev.c
    gx/GXTexture.c gx/GXTransform.c gx/GXVert.c
    os/OSAlloc.c os/OSTime.c os/OSMessage.c
    pad/Padclamp.c)
set(_sdk_built)
foreach(_src IN LISTS _sdk_sources)
    set(_out "${CMAKE_BINARY_DIR}/sdk/${_src}")
    set(_drop)
    if(DEFINED _sdk_drop_${_src})
        set(_drop --drop ${_sdk_drop_${_src}})
    endif()
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND Python3::Interpreter "${NK_WII}/tools/strip_asm.py"
                "${_sdk_root}/${_src}" "${_out}" ${_drop}
        DEPENDS "${NK_WII}/tools/strip_asm.py" "${_sdk_root}/${_src}"
        COMMENT "Stripping MWCC assembly from ${_src}")
    list(APPEND _sdk_built "${_out}")
endforeach()
add_library(nk_sdk STATIC ${_sdk_built})
target_link_libraries(nk_sdk PRIVATE nk_wii_flags)
target_include_directories(nk_sdk BEFORE PRIVATE
    ${NK_ROOT}/libs/dolphin/include
    ${_sdk_root} ${_sdk_root}/gx ${_sdk_root}/os ${_sdk_root}/mtx)
target_compile_options(nk_sdk PRIVATE -w -ffp-contract=off
    "SHELL:-include ${NK_WII}/include/nookery_wii/sdk_compat.h")

# The matrix library: Aurora's C one (the SDK's is paired-single assembly;
# wii/src/mtx_ps.c gives its PS names)
file(GLOB NK_MTX CONFIGURE_DEPENDS "${NK_ROOT}/extern/aurora/lib/dolphin/mtx/*.c")
add_library(nk_mtx STATIC ${NK_MTX})
target_link_libraries(nk_mtx PRIVATE nk_wii_flags)
target_compile_options(nk_mtx PRIVATE -w)

# =============================================================================
# wiilib: files on SD and USB, the log, the system buttons, the stop screen,
# the GameCube disc image reader
# =============================================================================
add_subdirectory("${NK_WII}/lib/wiilib" wiilib EXCLUDE_FROM_ALL)

# =============================================================================
# The port: the SDK's OS, VI, PAD, DVD, CARD, ARAM and AI over libogc
# (wii/src/*.c include the SDK's headers; wii/src/ogc/*.c libogc's)
# =============================================================================
file(GLOB NK_PORT_SDK CONFIGURE_DEPENDS "${NK_WII}/src/*.c" "${NK_WII}/src/*.cpp")
add_library(nk_port STATIC ${NK_PORT_SDK})
target_link_libraries(nk_port PRIVATE nk_wii_flags wiilib::platform wiilib::gcm)
target_include_directories(nk_port PRIVATE ${NK_WII}/src)
target_compile_options(nk_port PRIVATE -Wall -Wno-unused-function -Wno-unused-variable)

file(GLOB NK_PORT_OGC CONFIGURE_DEPENDS "${NK_WII}/src/ogc/*.c" "${NK_WII}/src/launcher/*.c")
list(REMOVE_ITEM NK_PORT_OGC "${NK_WII}/src/ogc/main.c")
add_library(nk_port_ogc STATIC ${NK_PORT_OGC})
target_include_directories(nk_port_ogc PRIVATE ${NK_WII}/src ${NK_WII}/include ${NK_ROOT}/pc/include)
target_compile_options(nk_port_ogc PRIVATE -std=gnu17 -O2 -g -Wall)
target_link_libraries(nk_port_ogc PRIVATE wiilib::platform wiilib::gcm)

# =============================================================================
# The NES emulator: the PC port's fixNES (pc/lib/fixnes, MIT, by FIX94) and
# its glue (pc/src/pc_nes_fixnes.c), in the GameCube core's place (its MWCC
# assembly: src/static/Famicom/ks_nes_core.cpp). The PC's list and flags
# (CMakeLists.txt), less COL_TEX_BSWAP and COL_GL_BSWAP: those are for a
# little-endian host; here fixNES writes the native RGB565 GX reads. Its
# buffers go to MEM2 (wii/nookery.ld).
# =============================================================================
set(_fixnes_dir "${NK_ROOT}/pc/lib/fixnes")
file(GLOB NK_FIXNES_MAPPERS CONFIGURE_DEPENDS "${_fixnes_dir}/mapper/*.c")
# (cpu.c, ppu.c, apu.c, mapper.c and mem.c in wii/src/nes/nk_fixnes_core.c)
set(NK_FIXNES_SOURCES
    ${NK_WII}/src/nes/nk_fixnes_core.c
    ${_fixnes_dir}/input.c
    ${_fixnes_dir}/mapperList.c ${_fixnes_dir}/vrc_irq.c ${_fixnes_dir}/audio.c
    ${_fixnes_dir}/fm2play.c ${_fixnes_dir}/audio_fds.c ${_fixnes_dir}/audio_mmc5.c
    ${_fixnes_dir}/audio_vrc6.c ${_fixnes_dir}/audio_vrc7.c ${_fixnes_dir}/audio_n163.c
    ${_fixnes_dir}/audio_s5b.c
    ${NK_FIXNES_MAPPERS})
add_library(nk_fixnes STATIC ${NK_FIXNES_SOURCES} "${NK_ROOT}/pc/src/pc_nes_fixnes.c")
target_link_libraries(nk_fixnes PRIVATE nk_wii_flags)
# (after the game's, as on PC: pc_nes_fixnes.c's "audio.h" is the game's)
target_include_directories(nk_fixnes PRIVATE ${_fixnes_dir})
# Third-party code without the decompilation's undefined behaviour: none of
# the game's safety flags' costs (zeroed locals), -O3; the APU at its lower
# rate (55.9 kHz, a quarter of the work, still above the AI's 32 kHz): at
# the full rate a frame took longer than the Wii's 16.7 ms
set_source_files_properties(${NK_FIXNES_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w;-O3;-ftrivial-auto-var-init=uninitialized"
    COMPILE_DEFINITIONS "DO_INLINE_ATTRIBS=1;AUDIO_LOWERFREQ=1")
set_source_files_properties("${NK_ROOT}/pc/src/pc_nes_fixnes.c" PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O3;-ftrivial-auto-var-init=uninitialized"
    COMPILE_DEFINITIONS "AUDIO_LOWERFREQ=1")

set(_nk_libs nk_game nk_data nk_sdk nk_mtx nk_port nk_port_ogc nk_fixnes)

if(NOOKERY_WII_NET)
    # The session and the lobby (libogc's sockets, ENet): libogc's headers
    file(GLOB NK_NET CONFIGURE_DEPENDS "${NK_WII}/src/net/*.c" "${NK_WII}/src/net/enet/*.c")
    add_library(nk_net STATIC ${NK_NET})
    target_include_directories(nk_net PRIVATE ${NK_WII}/src ${NK_WII}/src/net ${NK_WII}/src/net/enet/include)
    target_compile_definitions(nk_net PRIVATE ENET_IOS)
    target_compile_options(nk_net PRIVATE -std=gnu17 -O2 -g -Wall -Wno-unused-function)
    file(GLOB NK_ENET_SRC CONFIGURE_DEPENDS "${NK_WII}/src/net/enet/*.c")
    set_source_files_properties(${NK_ENET_SRC} PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(nk_net PRIVATE wiilib::platform)
    # The game's side (wii/src/netgame): the game's headers
    file(GLOB NK_NETGAME CONFIGURE_DEPENDS "${NK_WII}/src/netgame/*.c")
    if(NK_NETGAME)
        add_library(nk_netgame STATIC ${NK_NETGAME})
        target_link_libraries(nk_netgame PRIVATE nk_wii_flags)
        target_include_directories(nk_netgame PRIVATE ${NK_WII}/src ${NK_WII}/src/net)
        target_compile_options(nk_netgame PRIVATE -Wall -Wno-unused-function -Wno-unused-variable)
        list(APPEND _nk_libs nk_netgame)
    endif()
    list(APPEND _nk_libs nk_net)
    target_include_directories(nk_port_ogc PRIVATE ${NK_WII}/src/net)
    target_compile_definitions(nk_port_ogc PRIVATE NOOKERY_WII_NET)
    target_compile_definitions(nk_wii_flags INTERFACE NOOKERY_WII_NET)
endif()

# =============================================================================
# The program
# =============================================================================
add_executable(nookery "${NK_WII}/src/ogc/main.c")
target_include_directories(nookery PRIVATE ${NK_WII}/src ${NK_WII}/include ${CMAKE_BINARY_DIR}/generated)
# The build's id (the commit), for the log and crash reports: worked out at every build
add_custom_target(nk_build_id
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/generated/nk_build_id.h
            -P ${NK_WII}/cmake/build_id.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/nk_build_id.h
    COMMENT "Build id")
add_dependencies(nookery nk_build_id)
target_compile_options(nookery PRIVATE -std=gnu17 -O2 -g -Wall)
target_link_libraries(nookery PRIVATE
    -Wl,--start-group ${_nk_libs} -Wl,--end-group
    wiilib::platform wiilib::gcm wiiuse bte fat ogc m)
set(_nk_specs "${CMAKE_BINARY_DIR}/nookery.specs")
file(WRITE "${_nk_specs}" "*link_start_rvl:\n-T ${NK_WII}/nookery.ld\n")
target_link_options(nookery PRIVATE
    "-specs=${_nk_specs}"
    -Wl,-Map,${CMAKE_BINARY_DIR}/nookery.map)
set_property(TARGET nookery APPEND PROPERTY LINK_DEPENDS "${NK_WII}/nookery.ld")
# The DOL, without the MEM2 section (zeros, cleared at the start)
add_custom_command(TARGET nookery POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -R .mem2bss "$<TARGET_FILE:nookery>" "${CMAKE_BINARY_DIR}/nookery_dol.elf"
    COMMAND "${ELF2DOL_EXE}" "${CMAKE_BINARY_DIR}/nookery_dol.elf" "${CMAKE_BINARY_DIR}/boot.dol"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/nookery_dol.elf" "${CMAKE_BINARY_DIR}/boot.dol"
    COMMENT "Making boot.dol")

# The release zip (wii/tools/make_release.py): cmake --build build/wii --target release
add_custom_target(release
    COMMAND Python3::Interpreter "${NK_WII}/tools/make_release.py" --build "${CMAKE_BINARY_DIR}"
            --out "${CMAKE_BINARY_DIR}/release"
    DEPENDS nookery
    COMMENT "Making the release zip")
