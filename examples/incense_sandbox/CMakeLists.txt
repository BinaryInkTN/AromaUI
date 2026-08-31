cmake_minimum_required(VERSION 3.15)
project(incense_sandbox C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

add_executable(incense_sandbox
    sandbox.c
    aroma_map_stub.c
)

target_include_directories(incense_sandbox PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

if(EMSCRIPTEN)
    target_link_libraries(incense_sandbox PRIVATE aroma)
    target_link_options(incense_sandbox PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sINVOKE_RUN=0"
        "-sEXPORTED_FUNCTIONS=['_main','_aroma_sandbox_init','_aroma_sandbox_reload','_aroma_sandbox_get_last_error','_aroma_sandbox_has_error','_aroma_sandbox_get_width','_aroma_sandbox_get_height','_aroma_event_handle_touch','_aroma_event_handle_pointer_move','_aroma_emscripten_dispatch_mouse']"
        "-sEXPORTED_RUNTIME_METHODS=['callMain','ccall','cwrap','FS','addRunDependency','removeRunDependency']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sFETCH=1"
    )
    file(COPY
        ${CMAKE_CURRENT_SOURCE_DIR}/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/default.aroma
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
    )
    file(COPY
        ${CMAKE_CURRENT_SOURCE_DIR}/showcase/
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/showcase/
    )
    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")
else()
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_link_libraries(incense_sandbox PRIVATE
            aroma
            m
            pthread
            asound
            curl
            Vulkan::Vulkan
        )
        message(STATUS "Windowing Backend: GLPS/GLFW (Native, Vulkan)")
    else()
        target_link_libraries(incense_sandbox PRIVATE
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

if(NOT EMSCRIPTEN)
    target_include_directories(incense_sandbox PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/glps/include"
    )
endif()
