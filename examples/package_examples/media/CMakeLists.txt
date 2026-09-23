# First-party media package: full-screen Music app + owned A2DP/AVRCP
# Bluetooth speaker stack (bt_speaker_api.c). Self-contained: the only
# host coupling left is UI-chrome coordination (documented first-party
# privilege); all BT logic and state live in this plugin.so.
cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include" CACHE PATH
        "AromaUI include directory")
endif()
if(NOT DEFINED AromaApp_INCLUDE_DIR)
    set(AromaApp_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../car_infotainment" CACHE PATH
        "Host app include directory")
endif()

# D-Bus (BlueZ) + PulseAudio are system libs: the plugin links them
# itself, like any third-party native app would. The host does not
# provide BT symbols.
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(PULSE REQUIRED libpulse)

add_library(media_plugin MODULE
    src/music_app.c
    src/media_bt_service.c
    src/bt_speaker_api.c
)
set_target_properties(media_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "plugin"
)
target_include_directories(media_plugin PRIVATE
    "${AromaUI_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}/core"
    "${AromaApp_INCLUDE_DIR}/vehicle"
    "${AromaApp_INCLUDE_DIR}/pkg"
    "${AromaApp_INCLUDE_DIR}/setup"
    "${AromaApp_INCLUDE_DIR}/voice"
    "${AromaApp_INCLUDE_DIR}/apak"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${DBUS_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)
target_link_libraries(media_plugin PRIVATE
    ${DBUS_LIBRARIES}
    ${PULSE_LIBRARIES}
)
# NOTE: deliberately NOT linked against libaroma or the host - the host
# process provides all aroma_* and app symbols at dlopen time (it links
# with -rdynamic).
