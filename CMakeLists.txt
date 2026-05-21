cmake_minimum_required(VERSION 3.15)
project(aroma LANGUAGES C)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

find_package(Vulkan QUIET)
if(Vulkan_FOUND AND NOT ENABLE_GLFW AND NOT EMSCRIPTEN)
    set(GLPS_INCLUDE_VULKAN ON CACHE BOOL "Enable Vulkan support in GLPS" FORCE)
endif()

if(NOT ENABLE_GLFW AND NOT EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/glps/CMakeLists.txt")
        add_subdirectory(vendors/glps)
    endif()
endif()

add_subdirectory(src)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests" AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

