# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.13.1)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(BOARD semihalfino)
set(DTC_OVERLAY_FILE src/overlay.dts)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(app PRIVATE src/main.c)

add_subdirectory(drivers)
