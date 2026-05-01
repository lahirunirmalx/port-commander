cmake_minimum_required(VERSION 3.16)
project(apcommander C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

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

add_executable(apcommander
    dash/main.c
)

target_include_directories(apcommander PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

target_link_libraries(apcommander PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

target_compile_options(apcommander PRIVATE
    ${SDL2_CFLAGS_OTHER}
    ${SDL2_TTF_CFLAGS_OTHER}
)

# -----------------------------------------------------------------------------
# Serial-port instrument tools (adc_gui, psu_gui*).
#
# These were imported from dev-psu-gui and dev-modbus/psu-gui. They use
# pthreads and libm in addition to SDL2/SDL2_ttf. The CFLAGS from the
# upstream Makefile (-Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L
# -pthread) are reproduced here per-target so the upstream code's
# conditional compilation continues to work as it did originally.
# -----------------------------------------------------------------------------
find_package(Threads REQUIRED)

set(INSTRUMENT_DEFS _POSIX_C_SOURCE=200809L)
set(INSTRUMENT_OPTS ${SDL2_CFLAGS_OTHER} ${SDL2_TTF_CFLAGS_OTHER}
                    -Wno-unused-parameter -Wno-unused-variable
                    -Wno-unused-but-set-variable -Wno-unused-function
                    -Wno-sign-compare)

# Shared link libs: SDL2, SDL2_ttf, libm, pthread.
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
target_compile_definitions(psu_gui PRIVATE ${INSTRUMENT_DEFS})
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
target_compile_definitions(psu_gui_single PRIVATE ${INSTRUMENT_DEFS})
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
target_compile_definitions(psu_gui_toolbar PRIVATE ${INSTRUMENT_DEFS})
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
target_compile_definitions(psu_gui_toolbar_single PRIVATE ${INSTRUMENT_DEFS})
target_compile_options(psu_gui_toolbar_single PRIVATE ${INSTRUMENT_OPTS})
target_link_libraries(psu_gui_toolbar_single PRIVATE ${INSTRUMENT_LIBS})

# apcommander launches the tools as siblings; build them all when someone
# builds only apcommander.
add_dependencies(apcommander portcommander wificommander
                 adc_gui psu_gui psu_gui_single
                 psu_gui_toolbar psu_gui_toolbar_single)
