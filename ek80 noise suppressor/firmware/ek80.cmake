# Hardware-independent sources. Shared by the application and the unit
# tests (tests/) so the same logic runs on the board and on native_sim.
set(EK80_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

target_include_directories(app PRIVATE ${EK80_SRC_DIR})
target_sources(app PRIVATE
  ${EK80_SRC_DIR}/core/linebuf.c
  ${EK80_SRC_DIR}/core/app.c
  ${EK80_SRC_DIR}/core/controller.c
  ${EK80_SRC_DIR}/glider/glider_cmd.c
)
