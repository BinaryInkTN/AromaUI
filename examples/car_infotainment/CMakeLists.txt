cmake_minimum_required(VERSION 3.15)
project(settings_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_VOICE_CONTROL "Enable voice control (requires libvosk)" OFF)
option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

add_executable(infotainment
    main.c
    cJSON.c
)

if(ENABLE_VOICE_CONTROL)
    find_library(VOSK_LIB vosk HINTS "${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib")
    if(NOT VOSK_LIB)
        message(WARNING "libvosk not found - disabling voice control")
        set(ENABLE_VOICE_CONTROL OFF)
    endif()
endif()

if(ENABLE_VOICE_CONTROL)
    target_sources(infotainment PRIVATE voice_control.c)
    target_compile_definitions(infotainment PRIVATE AROMA_USE_VOICE_CONTROL)
    target_link_libraries(infotainment PRIVATE vosk)
    message(STATUS "Voice control: ENABLED")
else()
    message(STATUS "Voice control: DISABLED")
endif()

target_include_directories(infotainment PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vosk_lib"
)

if(EMSCRIPTEN)
    target_link_libraries(infotainment PRIVATE aroma)
    target_link_options(infotainment PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sEXPORTED_FUNCTIONS=['_main','_aroma_event_handle_touch','_aroma_event_handle_pointer_move','_aroma_emscripten_dispatch_mouse','_aroma_test_get_last_mouse_event_type','_aroma_test_get_last_mouse_event_x','_aroma_test_get_last_mouse_event_y','_aroma_test_get_last_mouse_event_target']"
        "-sEXPORTED_RUNTIME_METHODS=['callMain','ccall','cwrap','FS','addRunDependency','removeRunDependency']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sFETCH=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

elseif(ENABLE_GLFW)
    target_link_libraries(infotainment PRIVATE
        aroma
        m
        pthread
        asound
        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLFW (Native)")

else()
    target_link_libraries(infotainment PRIVATE
        aroma
        m
        pthread
        asound
        curl
        Vulkan::Vulkan
    )
    message(STATUS "Windowing Backend: GLPS (Native)")
endif()