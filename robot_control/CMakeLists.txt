cmake_minimum_required(VERSION 3.10)
project(robot_control C)

set(CMAKE_C_STANDARD 11)

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR $ENV{AROMA_SDK_PATH})
else()
    set(AROMA_DIR "/home/yassine/AromaUI")
endif()

message("Using Aroma SDK at: ${AROMA_DIR}")

add_subdirectory(${AROMA_DIR}/src aroma_sdk)

add_executable(robot_control src/main.c)

target_include_directories(robot_control PRIVATE ${AROMA_DIR}/include)
target_link_libraries(robot_control PRIVATE aroma)
