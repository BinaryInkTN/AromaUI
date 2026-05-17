cmake_minimum_required(VERSION 3.15)
project(settings_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(AROMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

include_directories(
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib
)

link_directories(
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib
)

option(ENABLE_VOICE_CONTROL "Enable voice control (requires libvosk)" OFF)
option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

add_executable(data_table_example
    main.c
)

if(NOT EMSCRIPTEN)
    find_package(Vulkan REQUIRED)
else()
    message(STATUS "Building for Emscripten — skipping Vulkan requirement")
endif()

if(ENABLE_VOICE_CONTROL)
    find_library(VOSK_LIB vosk HINTS ${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib)
    if(NOT VOSK_LIB)
        message(WARNING "libvosk not found — disabling voice control")
        set(ENABLE_VOICE_CONTROL OFF)
    endif()
endif()

if(ENABLE_VOICE_CONTROL)
    target_sources(data_table_example PRIVATE voice_control.c)
    target_compile_definitions(data_table_example PRIVATE AROMA_USE_VOICE_CONTROL)
    target_link_libraries(data_table_example PRIVATE vosk)
    message(STATUS "Voice control: ENABLED")
else()
    message(STATUS "Voice control: DISABLED")
endif()

if(EMSCRIPTEN)
    if(NOT TARGET aroma)
        add_subdirectory(${AROMA_ROOT}/src aroma_sdk)
    endif()

    target_link_libraries(data_table_example PRIVATE
        aroma
        freetype
        m
    )
    target_link_options(data_table_example PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=1"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sEXPORTED_FUNCTIONS=['_main','_aroma_event_handle_touch','_aroma_event_handle_pointer_move','_aroma_emscripten_dispatch_mouse','_aroma_test_get_last_mouse_event_type','_aroma_test_get_last_mouse_event_x','_aroma_test_get_last_mouse_event_y','_aroma_test_get_last_mouse_event_target']"
        "-sEXPORTED_RUNTIME_METHODS=['callMain','ccall','cwrap']"
    )
    # Preload the example assets (fonts/images) into the WASM virtual FS at /assets
    target_link_options(data_table_example PRIVATE
        "--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")
elseif(ENABLE_GLFW)
    target_compile_definitions(data_table_example PRIVATE AROMA_USE_GLFW)
    
    # We no longer hard-require find_package(glfw3) here since libaroma 
    # might embed glfw statically from the submodule. If glfw3 is found, 
    # we link against it; otherwise, we assume libaroma passes along the glfw symbols.
    find_package(glfw3 QUIET)
    if (TARGET glfw OR TARGET glfw3 OR glfw3_FOUND)
        target_link_libraries(data_table_example PRIVATE
            glfw
        )
    endif()

    target_link_libraries(data_table_example PRIVATE
        aroma
        GLESv2
        freetype
        m
        pthread
        asound
        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLFW")
else()
    target_link_libraries(data_table_example PRIVATE
        aroma
        GLPS
        GLESv2
        EGL
        X11
        freetype
        m
        pthread
        asoundcmake_minimum_required(VERSION 3.15)
project(data_table_example C)

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

add_executable(data_table_example main.c)

find_package(Vulkan QUIET)

target_link_libraries(data_table_example

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

        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLPS")
endif()