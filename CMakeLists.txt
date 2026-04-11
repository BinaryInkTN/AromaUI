cmake_minimum_required(VERSION 3.15)
project(aroma LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)

# Enable Vulkan in the GLPS vendor library when Vulkan is available,
# so glps_wm_vk_create_surface is compiled and exported.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    set(GLPS_INCLUDE_VULKAN OFF CACHE BOOL "Enable Vulkan support in GLPS" FORCE)
endif()

add_subdirectory(vendors/glps)
add_subdirectory(src)

target_link_libraries(aroma PRIVATE GLPS)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests
    AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/car_infotainment
    AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/examples/car_infotainment)
    add_subdirectory(examples/car_infotainment)
endif()
