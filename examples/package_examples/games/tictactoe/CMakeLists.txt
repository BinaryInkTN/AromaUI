# Tic Tac Toe game package (host chrome).
cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include" CACHE PATH
        "AromaUI include directory")
endif()

add_library(tictactoe_plugin MODULE
    src/tictactoe_game.c
)
set_target_properties(tictactoe_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "plugin"
)
target_include_directories(tictactoe_plugin PRIVATE
    "${AromaUI_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../car_infotainment/apak"
)
# NOTE: deliberately NOT linked against libaroma or the host.

