# EK80 noise suppressor firmware (Raspberry Pi Pico / RP2040, Zephyr)
#
#   west build -b rpi_pico firmware
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ek80)

include(${CMAKE_CURRENT_LIST_DIR}/ek80.cmake)
target_sources(app PRIVATE
  src/main.c
  src/hw/port_uart.c
  src/hw/board_io.c
)
