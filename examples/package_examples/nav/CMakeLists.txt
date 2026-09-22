# First-party nav package: built from this directory into plugin.so,
# packed to com.aroma.nav.apak, preinstalled into the infotainment image.
# Needs the AromaUI include dir plus the car_infotainment dir (host API:
# app_state.h, vehicle_view.h, theme_manager.h, media_controls.h).
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
)
# NOTE: deliberately NOT linked against libaroma or the host - the host
# process provides all aroma_* and app symbols at dlopen time (it links
# with -rdynamic).

# Stage plugin.so next to manifest.json (the pack step reads this dir).
if(DEFINED AROMA_PKG_LEAF_DIR)
    add_custom_command(TARGET nav_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:nav_plugin>"
                "${AROMA_PKG_LEAF_DIR}/plugin.so"
        COMMENT "Staging com.aroma.nav plugin"
        VERBATIM
    )
endif()
