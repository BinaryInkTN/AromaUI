cmake_minimum_required(VERSION 3.10)
project(android_test C)

set(CMAKE_C_STANDARD 11)

# AromaUI Path (Default to relative, but can be set via env or CLI)
# In this template, we point to the SDK root relative path or env
if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR $ENV{AROMA_SDK_PATH})
else()
    # Fallback/Default
    set(AROMA_DIR "/home/yassineaa/AromaUI")
endif()

message("Using Aroma SDK at: ${AROMA_DIR}")

add_subdirectory(${AROMA_DIR}/src aroma_sdk)

add_executable(android_test src/main.c)

target_include_directories(android_test PRIVATE ${AROMA_DIR}/include)
target_link_libraries(android_test PRIVATE aroma)
