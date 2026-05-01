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

# apcommander launches portcommander/wificommander as siblings; ensure they
# are built first when someone builds only apcommander.
add_dependencies(apcommander portcommander wificommander)
