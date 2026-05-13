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

option(ENABLE_VOICE_CONTROL "Enable voice control (requires libvosk)" OFF)
option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

add_executable(infotainment
    main.c
    cJSON.c
)

find_package(Vulkan REQUIRED)

if(ENABLE_VOICE_CONTROL)
    find_library(VOSK_LIB vosk HINTS ${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib)
    if(NOT VOSK_LIB)
        message(WARNING "libvosk not found — disabling voice control")
        set(ENABLE_VOICE_CONTROL OFF)
    endif()
endif()

if(ENABLE_VOICE_CONTROL)
    target_sources(infotainment PRIVATE voice_control.c)
    target_compile_definitions(infotainment PRIVATE AROMA_USE_VOICE_CONTROL)
    target_link_libraries(infotainment PRIVATE vosk)
    message(STATUS "Voice control: ENABLED")
else()
    message(STATUS "Voice control: DISABLED")
endif()

if(ENABLE_GLFW)
    find_package(glfw3 REQUIRED)
    target_compile_definitions(infotainment PRIVATE AROMA_USE_GLFW)
    target_link_libraries(infotainment PRIVATE
        aroma
        glfw
        GLESv2
        freetype
        m
        pthread
        asound
        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLFW")
else()
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
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLPS")
endif()