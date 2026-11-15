cmake_minimum_required(VERSION 3.15)

project(19_code_editor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR "$ENV{AROMA_SDK_PATH}")
else()
    set(AROMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

message(STATUS "Using Aroma SDK at: ${AROMA_DIR}")

if(NOT TARGET aroma)
    add_subdirectory(${AROMA_DIR} aroma_sdk)
endif()

add_executable(19_code_editor
    src/main.c
)

target_include_directories(19_code_editor PRIVATE
    "${AROMA_DIR}/include/widgets"
)

target_link_libraries(19_code_editor
    PRIVATE
    aroma
)

if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)

    if(Vulkan_FOUND)
        target_link_libraries(19_code_editor
            PRIVATE
            Vulkan::Vulkan
        )
    endif()
endif()

if(EMSCRIPTEN)

    set_target_properties(19_code_editor
        PROPERTIES
        SUFFIX ".html"
    )

    target_link_options(19_code_editor PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
    )

    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

elseif(ANDROID)

    message(STATUS "Windowing Backend: Android Native")

elseif(ENABLE_GLFW)

    message(STATUS "Windowing Backend: GLFW")

else()

    message(STATUS "Windowing Backend: GLPS")

endif()
