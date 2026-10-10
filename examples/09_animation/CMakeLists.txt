cmake_minimum_required(VERSION 3.15)

project(09_animation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
    set(AROMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

message(STATUS "Using Aroma SDK at: ${AROMA_DIR}")

if(NOT TARGET aroma)
    add_subdirectory(${AROMA_DIR} aroma_sdk)
endif()

add_executable(09_animation
    src/main.c
)

target_link_libraries(09_animation
    PRIVATE
    aroma
)

if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)

    if(Vulkan_FOUND)
        target_link_libraries(09_animation
            PRIVATE
            Vulkan::Vulkan
        )
    endif()
endif()

if(EMSCRIPTEN)

    set_target_properties(09_animation
        PROPERTIES
        SUFFIX ".html"
    )

    target_link_options(09_animation PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        target_link_options(09_animation PRIVATE
            "--preload-file"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        )
    endif()

    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

elseif(ANDROID)

    message(STATUS "Windowing Backend: Android Native")

elseif(ENABLE_GLFW)

    message(STATUS "Windowing Backend: GLFW")

else()

    message(STATUS "Windowing Backend: GLPS")

endif()

add_custom_command(TARGET 09_animation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/ui"
            "$<TARGET_FILE_DIR:09_animation>/ui")
