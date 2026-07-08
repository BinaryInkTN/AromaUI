cmake_minimum_required(VERSION 3.15)
project(aroma LANGUAGES C)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(NOT ENABLE_GLFW AND NOT EMSCRIPTEN AND NOT ANDROID)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/glps/CMakeLists.txt")
        message("Choosing GLPS")
        add_subdirectory(vendors/glps)
    else()
        message(FATAL_ERROR "GLPS vendor submodule missing.")
    endif()
endif()


if(ENABLE_GLFW AND NOT EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/glfw/CMakeLists.txt")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_USE_EGL ON CACHE BOOL "" FORCE)
        add_subdirectory(vendors/glfw)
    else()
        message(FATAL_ERROR "GLFW vendor submodule missing.")
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/freetype/CMakeLists.txt")
    set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
    add_subdirectory(vendors/freetype)
else()
    message(FATAL_ERROR "Freetype vendor submodule missing.")
endif()

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    set(GLPS_INCLUDE_VULKAN ON CACHE BOOL "Enable Vulkan support in GLPS" FORCE)
endif()

add_subdirectory(src)

if(NOT EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests" AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()
