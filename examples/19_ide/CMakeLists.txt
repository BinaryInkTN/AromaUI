cmake_minimum_required(VERSION 3.15)

project(19_ide C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" OFF)

if(ENABLE_GLFW AND EMSCRIPTEN)
    message(FATAL_ERROR "ENABLE_GLFW and EMSCRIPTEN are mutually exclusive.")
endif()

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR "$ENV{AROMA_SDK_PATH}")
else()
    set(AROMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

message(STATUS "Using Aroma SDK at: ${AROMA_DIR}")

if(NOT TARGET aroma)
    add_subdirectory(${AROMA_DIR} aroma_sdk)
endif()

add_executable(19_ide
    src/main.c
    src/ide.c
    src/ide_prelude.c
    src/ide_command_palette.c
    src/ide_dialogs.c
    src/ide_rendering.c
    src/ide_events.c
    src/ide_home.c
    src/ide_layout.c
    src/ide_internal.h
    src/designer.c
    src/ide_lsp.c
    src/workspace.c
)

set_source_files_properties(
    src/ide_prelude.c
    src/ide_command_palette.c
    src/ide_dialogs.c
    src/ide_rendering.c
    src/ide_events.c
    src/ide_home.c
    src/ide_layout.c
    src/ide_internal.h
    PROPERTIES HEADER_FILE_ONLY TRUE
)

target_include_directories(19_ide PRIVATE src ${AROMA_DIR}/include)

target_compile_definitions(19_ide PRIVATE
    AROMA_IDE_SDK_ROOT="${AROMA_DIR}"
)

target_link_libraries(19_ide
    PRIVATE
    aroma
)

if(NOT EMSCRIPTEN AND NOT ANDROID)
    target_link_libraries(19_ide PRIVATE pthread)
endif()

if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)

    if(Vulkan_FOUND)
        target_link_libraries(19_ide
            PRIVATE
            Vulkan::Vulkan
        )
    endif()
endif()

if(EMSCRIPTEN)

    set_target_properties(19_ide
        PROPERTIES
        SUFFIX ".html"
    )

    target_link_options(19_ide PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        target_link_options(19_ide PRIVATE
            "--preload-file"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        )
    endif()

    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")

elseif(ANDROID)

    message(STATUS "Windowing Backend: Android Native")

elseif(ENABLE_GLFW)

    message(STATUS "Windowing Backend: GLFW")

else()

    message(STATUS "Windowing Backend: GLPS")

endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_custom_command(TARGET 19_ide POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/ui"
                "$<TARGET_FILE_DIR:19_ide>/ui")
    add_custom_command(TARGET 19_ide POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:19_ide>/assets")
endif()
