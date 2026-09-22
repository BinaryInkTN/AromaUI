# First-party contacts package: dialer + contact list + PBAP sync.
cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include" CACHE PATH
        "AromaUI include directory")
endif()
if(NOT DEFINED AromaApp_INCLUDE_DIR)
    set(AromaApp_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../car_infotainment" CACHE PATH
        "Host app include directory")
endif()

add_library(contacts_plugin MODULE
    src/contacts_ui.c
)
set_target_properties(contacts_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "plugin"
)
target_include_directories(contacts_plugin PRIVATE
    "${AromaUI_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}"
)
# NOTE: deliberately NOT linked against libaroma or the host.

if(DEFINED AROMA_PKG_LEAF_DIR)
    add_custom_command(TARGET contacts_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:contacts_plugin>"
                "${AROMA_PKG_LEAF_DIR}/plugin.so"
        COMMENT "Staging com.aroma.contacts plugin"
        VERBATIM
    )
endif()
