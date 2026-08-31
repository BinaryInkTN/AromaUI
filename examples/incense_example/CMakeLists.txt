cmake_minimum_required(VERSION 3.15)
project(incense_example C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

add_executable(incense_example
    main.c
)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/test.aroma"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/ui.aroma"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/settings.aroma"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

if(EMSCRIPTEN)
    target_link_libraries(incense_example PRIVATE aroma)
    target_link_options(incense_example PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sEXPORTED_FUNCTIONS=['_main','_aroma_event_handle_touch','_aroma_event_handle_pointer_move','_aroma_emscripten_dispatch_mouse']"
        "-sEXPORTED_RUNTIME_METHODS=['callMain','ccall','cwrap','FS','addRunDependency','removeRunDependency']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sFETCH=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/test.aroma@/test.aroma"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/ui.aroma@/ui.aroma"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/settings.aroma@/settings.aroma"
    )
    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

else()
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_link_libraries(incense_example PRIVATE
            aroma
            m
            pthread
            asound
            curl
            Vulkan::Vulkan
        )
        message(STATUS "Windowing Backend: GLPS/GLFW (Native, Vulkan)")
    else()
        target_link_libraries(incense_example PRIVATE
            aroma
            m
            pthread
            asound
            curl
        )
        message(STATUS "Windowing Backend: GLPS/GLFW (Native, software fallback)")
    endif()

    if(ENABLE_GLFW)
        message(STATUS "  Using GLFW for windowing")
    else()
        message(STATUS "  Using GLPS for windowing")
    endif()
endif()

target_include_directories(incense_example PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

if(NOT EMSCRIPTEN)
    target_include_directories(incense_example PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/glps/include"
    )
endif()
