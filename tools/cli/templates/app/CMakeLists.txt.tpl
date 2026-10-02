cmake_minimum_required(VERSION 3.15)

project({{PROJECT_NAME}} C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

#
# Default to GLFW on desktop Linux
#
if(NOT ANDROID AND NOT EMSCRIPTEN)
    option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)
else()
    option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)
endif()

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR "$ENV{AROMA_SDK_PATH}")
else()
    set(AROMA_DIR "{{AROMA_ROOT}}")
endif()

message(STATUS "Using Aroma SDK at: ${AROMA_DIR}")

#
# Build Aroma SDK
#
if(NOT TARGET aroma)
    add_subdirectory(${AROMA_DIR} aroma_sdk)
endif()

add_executable({{PROJECT_NAME}}
    src/main.c
)

target_link_libraries({{PROJECT_NAME}}
    PRIVATE
    aroma
)

#
# Optional Vulkan support on native platforms
#
if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)

    if(Vulkan_FOUND)
        target_link_libraries({{PROJECT_NAME}}
            PRIVATE
            Vulkan::Vulkan
        )
    endif()
endif()

#
# Emscripten
#
if(EMSCRIPTEN)

    set_target_properties({{PROJECT_NAME}}
        PROPERTIES
        SUFFIX ".html"
    )

    target_link_options({{PROJECT_NAME}} PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        target_link_options({{PROJECT_NAME}} PRIVATE
            "--preload-file"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        )
    endif()

    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

#
# Android
#
elseif(ANDROID)

    message(STATUS "Windowing Backend: Android Native")

#
# Desktop
#
elseif(ENABLE_GLFW)

    message(STATUS "Windowing Backend: GLFW")

else()

    message(STATUS "Windowing Backend: GLPS")

endif()