cmake_minimum_required(VERSION 3.15)
project(HelloButton C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(AROMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
include_directories(
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
)

link_directories(
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/vendors/glps
)

add_executable(hello_button main.c)

target_link_libraries(hello_button
    aroma
    GLPS
    GLESv2
    EGL
    X11
    freetype
    m
    pthread
)

message(STATUS "=== Hello Button Example ===")
message(STATUS "This example demonstrates Windows 7 Aero style buttons")
message(STATUS "")
message(STATUS "To build:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")
message(STATUS "")
message(STATUS "To run:")
message(STATUS "  ./hello_button")

