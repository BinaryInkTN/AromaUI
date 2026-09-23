# First-party contacts package: dialer + contact list + PBAP sync, plus the
# owned HFP telephony stack (bt_speaker_hfp.c). Self-contained: the only
# host coupling left is UI-chrome coordination (documented first-party
# privilege); all BT logic lives in this plugin.so.
cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include" CACHE PATH
        "AromaUI include directory")
endif()
if(NOT DEFINED AromaApp_INCLUDE_DIR)
    set(AromaApp_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../car_infotainment" CACHE PATH
        "Host app include directory")
endif()

# D-Bus (BlueZ/oFono) is a system lib: the plugin links it itself, like
# any third-party native app would. The host does not provide BT symbols.
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)

add_library(contacts_plugin MODULE
    src/contacts_ui.c
    src/contacts_bt_service.c
    src/bt_speaker_hfp.c
)
set_target_properties(contacts_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "plugin"
)
target_include_directories(contacts_plugin PRIVATE
    "${AromaUI_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}"
    "${AromaApp_INCLUDE_DIR}/core"
    "${AromaApp_INCLUDE_DIR}/vehicle"
    "${AromaApp_INCLUDE_DIR}/pkg"
    "${AromaApp_INCLUDE_DIR}/setup"
    "${AromaApp_INCLUDE_DIR}/voice"
    "${AromaApp_INCLUDE_DIR}/apak"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../media/src"
    ${DBUS_INCLUDE_DIRS}
)
target_link_libraries(contacts_plugin PRIVATE
    ${DBUS_LIBRARIES}
)
# NOTE: deliberately NOT linked against libaroma or the host - the host
# process provides all aroma_* and app symbols at dlopen time (it links
# with -rdynamic).
