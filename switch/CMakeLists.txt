cmake_minimum_required(VERSION 3.15)

# Saikou Switch uses the same CMake-first Borealis integration as NX-torrent-player.
# The newer Borealis revision is CMake-native; do not adapt it to the legacy
# libnx recursive Makefile path.

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/borealis/library)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(SaikouSwitch)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PROJECT_AUTHOR "Gozen0410")
set(APP_TITLE "Saikou Switch")
set(PROJECT_TITLEID "0x010000000000A456")
set(PROJECT_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/borealis/resources")

add_subdirectory(${BOREALIS_LIBRARY} borealis)

find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_USE_STATIC_LIBS ON)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

file(GLOB_RECURSE MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${BOREALIS_LIBRARY}/include/borealis/extern
    ${BOREALIS_LIBRARY}/lib/extern/glfw/include
    ${BOREALIS_LIBRARY}/lib/extern/glfw/deps)

target_compile_options(${PROJECT_NAME} PRIVATE
    -ffunction-sections
    -fdata-sections
    ${APP_PLATFORM_OPTION})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}")

target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)

target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    PkgConfig::CURL
    mbedtls
    mbedx509
    mbedcrypto
    z
    ${APP_PLATFORM_LIB})

if (PLATFORM_SWITCH)
    set(SAIKOU_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/resources)

    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create
            "${APP_TITLE}" "${PROJECT_AUTHOR}"
            "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            ${PROJECT_NAME}.nacp
            --titleid=${PROJECT_TITLEID}
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SAIKOU_RESOURCES}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SAIKOU_RESOURCES}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${SAIKOU_RESOURCES}
        # Match the useful part of NX-torrent-player's Borealis packaging:
        # keep system images/material/i18n, but drop demo/sample resources that
        # only inflate the NRO and are unrelated to Saikou.
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SAIKOU_RESOURCES}/font
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SAIKOU_RESOURCES}/img/pokemon
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SAIKOU_RESOURCES}/xml
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SAIKOU_RESOURCES}/icon
        COMMAND ${CMAKE_COMMAND} -E remove -f
            ${SAIKOU_RESOURCES}/img/tiles.png
            ${SAIKOU_RESOURCES}/img/demo_icon.jpg
            ${SAIKOU_RESOURCES}/img/borealis_256.png
            ${SAIKOU_RESOURCES}/img/borealis_96.png
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/romfs ${SAIKOU_RESOURCES}
        COMMAND ${NX_ELF2NRO_EXE}
            ${PROJECT_NAME}.elf
            ${PROJECT_NAME}.nro
            --nacp=${PROJECT_NAME}.nacp
            --romfsdir=${SAIKOU_RESOURCES})
endif()
