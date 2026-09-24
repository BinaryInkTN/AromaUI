



cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include" CACHE PATH
        "AromaUI include directory")
endif()
if(NOT DEFINED AromaApp_INCLUDE_DIR)
    set(AromaApp_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../car_infotainment" CACHE PATH
        "Host app include directory")
endif()




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



