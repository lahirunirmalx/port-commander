cmake_minimum_required(VERSION 3.16)
project(apcommander C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Native targets get strict warnings; imported instrument tools get
# -Wno-* suppressions per-target below.
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

# -----------------------------------------------------------------------------
# Platform classification.
#
# - LINUX_ONLY tools depend on Linux-only interfaces (lsof + /proc, nmcli,
#   NetworkManager). They build only on Linux.
# - POSIX tools depend on POSIX serial APIs (termios, pthreads). They build
#   on any POSIX system (Linux, macOS) but not Windows without porting.
# - The dashboard (apcommander) is cross-platform via the `compat/` module.
# -----------------------------------------------------------------------------
set(BUILD_LINUX_TOOLS OFF)
set(BUILD_POSIX_TOOLS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BUILD_LINUX_TOOLS ON)
endif()
if(NOT WIN32)
    set(BUILD_POSIX_TOOLS ON)
endif()

# -----------------------------------------------------------------------------
# compat/ — small portable layer used by the dashboard.
# -----------------------------------------------------------------------------
add_library(apcompat STATIC
    compat/compat.c
)
target_include_directories(apcompat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
)
target_include_directories(apcompat PRIVATE
    ${SDL2_INCLUDE_DIRS}
)
target_compile_options(apcompat PRIVATE ${SDL2_CFLAGS_OTHER})

# -----------------------------------------------------------------------------
# apcommander dashboard (cross-platform).
# -----------------------------------------------------------------------------
add_executable(apcommander
    dash/main.c
)
target_include_directories(apcommander PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)
target_link_libraries(apcommander PRIVATE
    apcompat
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)
target_compile_options(apcommander PRIVATE
    ${SDL2_CFLAGS_OTHER}
    ${SDL2_TTF_CFLAGS_OTHER}
)

# -----------------------------------------------------------------------------
# Linux-only native tools.
# -----------------------------------------------------------------------------
if(BUILD_LINUX_TOOLS)
    add_executable(portcommander
        src/main.c
        src/lsof_parse.c
        src/ps_query.c
        src/ui_render.c
    )
    target_include_directories(portcommander PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_link_libraries(portcommander PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
    )
    target_compile_options(portcommander PRIVATE
        ${SDL2_CFLAGS_OTHER}
        ${SDL2_TTF_CFLAGS_OTHER}
    )

    add_executable(wificommander
        wifi/main.c
        wifi/nmcli_run.c
        wifi/nmcli_query.c
        wifi/nmcli_action.c
        wifi/qr_codec.c
        wifi/ui_render.c
    )
    target_include_directories(wificommander PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_link_libraries(wificommander PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
    )
    target_compile_options(wificommander PRIVATE
        ${SDL2_CFLAGS_OTHER}
        ${SDL2_TTF_CFLAGS_OTHER}
    )
endif()

# -----------------------------------------------------------------------------
# Imported instrument tools (POSIX serial via termios, pthreads).
# These came in verbatim from dev-psu-gui and dev-modbus/psu-gui — keep
# their upstream warnings silenced rather than touching the imported code.
# -----------------------------------------------------------------------------
if(BUILD_POSIX_TOOLS)
    find_package(Threads REQUIRED)

    # Imported PSU code uses usleep() but doesn't #define _DEFAULT_SOURCE
    # itself, so add it here. Imported ADC code DOES #define it in the
    # source — adding it via CMake there would warn about redefinition,
    # so PSU targets get a separate macro list (INSTRUMENT_DEFS_USLEEP).
    set(INSTRUMENT_DEFS _POSIX_C_SOURCE=200809L)
    set(INSTRUMENT_DEFS_USLEEP _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    set(INSTRUMENT_OPTS ${SDL2_CFLAGS_OTHER} ${SDL2_TTF_CFLAGS_OTHER}
                        -Wno-unused-parameter -Wno-unused-variable
                        -Wno-unused-but-set-variable -Wno-unused-function
                        -Wno-sign-compare)
    set(INSTRUMENT_LIBS ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} m
                        Threads::Threads)

    # ADC monitor.
    add_executable(adc_gui
        adc/adc_gui.c
        adc/adc_protocol.c
        adc/serial_port.c
    )
    target_include_directories(adc_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/adc
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(adc_gui PRIVATE ${INSTRUMENT_DEFS})
    target_compile_options(adc_gui PRIVATE ${INSTRUMENT_OPTS})
    target_link_libraries(adc_gui PRIVATE ${INSTRUMENT_LIBS})

    # PSU dual-channel full GUI.
    add_executable(psu_gui
        psu/main.c
        psu/psu_protocol.c
        psu/serial_port.c
    )
    target_include_directories(psu_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/psu
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(psu_gui PRIVATE ${INSTRUMENT_DEFS_USLEEP})
    target_compile_options(psu_gui PRIVATE ${INSTRUMENT_OPTS})
    target_link_libraries(psu_gui PRIVATE ${INSTRUMENT_LIBS})

    # PSU single-channel full GUI. main_single.c #includes main.c with `main`
    # renamed, so don't list main.c separately or you get duplicate symbols.
    add_executable(psu_gui_single
        psu/main_single.c
        psu/psu_protocol.c
        psu/serial_port.c
    )
    target_include_directories(psu_gui_single PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/psu
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(psu_gui_single PRIVATE ${INSTRUMENT_DEFS_USLEEP})
    target_compile_options(psu_gui_single PRIVATE ${INSTRUMENT_OPTS})
    target_link_libraries(psu_gui_single PRIVATE ${INSTRUMENT_LIBS})

    # PSU dual-channel toolbar GUI.
    add_executable(psu_gui_toolbar
        psu/main_toolbar.c
        psu/psu_protocol.c
        psu/serial_port.c
    )
    target_include_directories(psu_gui_toolbar PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/psu
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(psu_gui_toolbar PRIVATE ${INSTRUMENT_DEFS_USLEEP})
    target_compile_options(psu_gui_toolbar PRIVATE ${INSTRUMENT_OPTS})
    target_link_libraries(psu_gui_toolbar PRIVATE ${INSTRUMENT_LIBS})

    # PSU single-channel toolbar GUI.
    add_executable(psu_gui_toolbar_single
        psu/main_toolbar_single.c
        psu/psu_protocol.c
        psu/serial_port.c
    )
    target_include_directories(psu_gui_toolbar_single PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/psu
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(psu_gui_toolbar_single PRIVATE ${INSTRUMENT_DEFS_USLEEP})
    target_compile_options(psu_gui_toolbar_single PRIVATE ${INSTRUMENT_OPTS})
    target_link_libraries(psu_gui_toolbar_single PRIVATE ${INSTRUMENT_LIBS})

    # Bench multimeter (DMM). Native to this project (not imported).
    add_executable(dmm_gui
        dmm/main.c
    )
    target_include_directories(dmm_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(dmm_gui PRIVATE ${INSTRUMENT_DEFS})
    target_compile_options(dmm_gui PRIVATE
        ${SDL2_CFLAGS_OTHER} ${SDL2_TTF_CFLAGS_OTHER})
    target_link_libraries(dmm_gui PRIVATE
        ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} m)

    # DC electronic load. Native to this project (not imported).
    add_executable(eload_gui
        eload/main.c
    )
    target_include_directories(eload_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(eload_gui PRIVATE ${INSTRUMENT_DEFS})
    target_compile_options(eload_gui PRIVATE
        ${SDL2_CFLAGS_OTHER} ${SDL2_TTF_CFLAGS_OTHER})
    target_link_libraries(eload_gui PRIVATE
        ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} m)
endif()

# -----------------------------------------------------------------------------
# Convenience: building apcommander pulls in every tool that was enabled
# for this platform, so `cmake --build build --target apcommander` produces
# a working dashboard with as many sibling tools as the platform supports.
# -----------------------------------------------------------------------------
set(_APC_TOOLS "")
foreach(_t portcommander wificommander adc_gui psu_gui psu_gui_single
           psu_gui_toolbar psu_gui_toolbar_single dmm_gui eload_gui)
    if(TARGET ${_t})
        list(APPEND _APC_TOOLS ${_t})
    endif()
endforeach()
if(_APC_TOOLS)
    add_dependencies(apcommander ${_APC_TOOLS})
endif()
