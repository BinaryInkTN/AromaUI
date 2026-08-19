cmake_minimum_required(VERSION 3.15)
project(audi_rs_cluster C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

add_executable(audi_rs_cluster
    main.c
)

if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan not found.")
    endif()
endif()

target_include_directories(audi_rs_cluster PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/glps/include"
)

if(EMSCRIPTEN)
    target_link_libraries(audi_rs_cluster PRIVATE aroma)
    target_link_options(audi_rs_cluster PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sEXPORTED_FUNCTIONS=['_main']"
        "-sEXPORTED_RUNTIME_METHODS=['callMain','ccall','cwrap','FS','addRunDependency','removeRunDependency']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
    )
    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

elseif(ENABLE_GLFW)
    target_link_libraries(audi_rs_cluster PRIVATE
        aroma
        m
        pthread
        asound
        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLFW (Native)")

else()
    target_link_libraries(audi_rs_cluster PRIVATE
        aroma
        m
        pthread
        asound
        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLPS (Native)")
endif()
