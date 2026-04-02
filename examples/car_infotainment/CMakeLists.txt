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

find_program(PICO2WAVE_BIN pico2wave)
if(NOT PICO2WAVE_BIN)
    message(WARNING "pico2wave not found! TTS will not work. Please install libttspico-utils (e.g. sudo apt-get install libttspico-utils).")
endif()

find_program(APLAY_BIN aplay)
if(NOT APLAY_BIN)
    message(WARNING "aplay not found! TTS playback will not work. Please install alsa-utils.")
endif()

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
    curl
    vosk
    $<$<BOOL:${Vulkan_FOUND}>:Vulkan::Vulkan>
)
