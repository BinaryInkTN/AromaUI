cmake_minimum_required(VERSION 3.10)
project(apptest C)

set(CMAKE_C_STANDARD 11)

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR $ENV{AROMA_SDK_PATH})
else()
    set(AROMA_DIR "/home/yassineaa/AromaUI")
endif()

message("Using Aroma SDK at: ${AROMA_DIR}")

add_subdirectory(${AROMA_DIR}/src aroma_sdk)

add_executable(apptest src/main.c)

target_include_directories(apptest PRIVATE ${AROMA_DIR}/include)
target_link_libraries(apptest PRIVATE aroma)
