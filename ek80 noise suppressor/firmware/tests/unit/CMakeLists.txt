# Unit and scenario tests on the host (native_sim):
#
#   west build -b native_sim/native/64 firmware/tests/unit
#   build/zephyr/zephyr.exe
#
cmake_minimum_required(VERSION 3.20.0)

set(EK80_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(KCONFIG_ROOT ${EK80_ROOT}/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ek80_unit)

include(${EK80_ROOT}/ek80.cmake)
target_sources(app PRIVATE
  src/fake_hw.c
  src/test_protocol.c
)
