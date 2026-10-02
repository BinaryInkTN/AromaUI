



cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include" CACHE PATH
        "AromaUI include directory")
endif()
if(NOT DEFINED AromaApp_INCLUDE_DIR)
    set(AromaApp_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../car_infotainment" CACHE PATH
        "Host app include directory")
endif()

add_library(nav_plugin MODULE
    src/nav_app.c
    src/navigation_geo.c
)
set_target_properties(nav_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "plugin"
)
target_include_directories(nav_plugin PRIVATE
    "${AromaUI_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}/core"
    "${AromaApp_INCLUDE_DIR}/vehicle"
    "${AromaApp_INCLUDE_DIR}/pkg"
    "${AromaApp_INCLUDE_DIR}/setup"
    "${AromaApp_INCLUDE_DIR}/voice"
    "${AromaApp_INCLUDE_DIR}/apak"
)




