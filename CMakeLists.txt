cmake_minimum_required(VERSION 3.15)
project(aroma LANGUAGES C)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# For Emscripten builds, avoid requiring system ZLIB/Freetype here.
# `src/CMakeLists.txt` will fall back to building the vendor `freetype` if
# the provider is not found. The top-level project shouldn't force
# a host-system ZLIB when targeting WASM/emscripten.
if(NOT EMSCRIPTEN)
    find_package(ZLIB REQUIRED)
    find_package(Freetype REQUIRED)
endif()

# Enable Vulkan in the GLPS vendor library when Vulkan is available,
# so glps_wm_vk_create_surface is compiled and exported.
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND NOT ENABLE_GLFW)
    set(GLPS_INCLUDE_VULKAN ON CACHE BOOL "Enable Vulkan support in GLPS" FORCE)
endif()

if(NOT ENABLE_GLFW AND NOT EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/glps/CMakeLists.txt")
        add_subdirectory(vendors/glps)
    endif()
endif()

add_subdirectory(src)

if(NOT ENABLE_GLFW)
    if(TARGET GLPS)
        target_link_libraries(aroma PRIVATE GLPS)
    endif()
endif()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests
    AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/car_infotainment
    AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/examples/car_infotainment)
    add_subdirectory(examples/car_infotainment)
endif()
