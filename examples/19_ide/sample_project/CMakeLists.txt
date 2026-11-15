cmake_minimum_required(VERSION 3.15)

project(aroma_notes C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR "$ENV{AROMA_SDK_PATH}")
else()
    set(AROMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endif()

if(NOT TARGET aroma)
    add_subdirectory(${AROMA_DIR} aroma_sdk)
endif()

add_executable(aroma_notes
    src/main.c
    src/notes.c
    src/theme.c
)

target_include_directories(aroma_notes PRIVATE src)
target_link_libraries(aroma_notes PRIVATE aroma)

if(NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_link_libraries(aroma_notes PRIVATE Vulkan::Vulkan)
    endif()
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_custom_command(TARGET aroma_notes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/ui"
                "$<TARGET_FILE_DIR:aroma_notes>/ui")
endif()
