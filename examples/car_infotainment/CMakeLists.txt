cmake_minimum_required(VERSION 3.15)
project(settings_app C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(AROMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
include_directories(
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib
)

link_directories(
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib
)

add_executable(infotainment main.c cJSON.c voice_control.c)

find_package(Vulkan QUIET)

target_link_libraries(infotainment
    aroma
    GLPS
    GLESv2
    EGL
    X11
    freetype
    m
    pthread
    asound
    vosk
    $<$<BOOL:${Vulkan_FOUND}>:Vulkan::Vulkan>
)
