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
)
# NOTE: deliberately NOT linked against libaroma or the host.

if(DEFINED AROMA_PKG_LEAF_DIR)
    add_custom_command(TARGET tictactoe_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:tictactoe_plugin>"
                "${AROMA_PKG_LEAF_DIR}/plugin.so"
        COMMENT "Staging com.aroma.game.tictactoe plugin"
        VERBATIM
    )
endif()
