cmake_minimum_required(VERSION 3.15)
project(map_example C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(AROMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
include_directories(
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
)

link_directories(
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/lib
)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

add_executable(map_example main.c)

find_package(Vulkan QUIET)

if(ENABLE_GLFW)
    target_compile_definitions(map_example PRIVATE AROMA_USE_GLFW)
    
    # We no longer hard-require find_package(glfw3) here since libaroma 
    # might embed glfw statically from the submodule. If glfw3 is found, 
    # we link against it; otherwise, we assume libaroma passes along the glfw symbols.
    find_package(glfw3 QUIET)
    if (TARGET glfw OR TARGET glfw3 OR glfw3_FOUND)
        target_link_libraries(map_example PRIVATE glfw)
    endif()

    target_link_libraries(map_example PRIVATE
        aroma
        GLESv2
        freetype
        m
        pthread
        curl
        $<$<BOOL:${Vulkan_FOUND}>:Vulkan::Vulkan>
    )
else()
    target_link_libraries(map_example PRIVATE
        aroma
        GLPS
        GLESv2
        EGL
        X11
        freetype
        m
        pthread
        curl
        $<$<BOOL:${Vulkan_FOUND}>:Vulkan::Vulkan>
    )
endif()
