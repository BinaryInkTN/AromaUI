cmake_minimum_required(VERSION 3.15)

project(test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR "$ENV{AROMA_SDK_PATH}")
else()
    set(AROMA_DIR "/home/yassine/Projects/AromaUI/examples/19_ide/../..")
endif()

if(NOT TARGET aroma)
    add_subdirectory(${AROMA_DIR} aroma_sdk)
endif()

add_executable(test src/main.c)
target_link_libraries(test PRIVATE aroma)

if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_link_libraries(test PRIVATE Vulkan::Vulkan)
    endif()
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_custom_command(TARGET test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/ui"
                "$<TARGET_FILE_DIR:test>/ui")
endif()
