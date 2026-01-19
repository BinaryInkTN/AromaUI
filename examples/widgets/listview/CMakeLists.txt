cmake_minimum_required(VERSION 3.15)
project(widget_listview C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(AROMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
include_directories(
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
)

link_directories(
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/lib
)

add_executable(widget_listview main.c)

target_link_libraries(widget_listview
    aroma
    GLPS
    GLESv2
    EGL
    X11
    freetype
    m
    pthread
)
