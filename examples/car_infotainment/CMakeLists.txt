cmake_minimum_required(VERSION 3.15)
project(settings_app C CXX)

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

add_executable(infotainment
    main.c
    cJSON.c
    voice_control.c
)


find_program(PICO2WAVE_BIN pico2wave)
find_program(APLAY_BIN aplay)

find_package(Vulkan REQUIRED)


target_link_libraries(infotainment PRIVATE
    aroma
    GLPS
    GLESv2
    EGL
    X11
    freetype
    m
    pthread
    asound
    curl
    curl
    vosk
    Vulkan::Vulkan
)