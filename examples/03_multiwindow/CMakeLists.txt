cmake_minimum_required(VERSION 3.15)
project(multiwindow_example)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set(AROMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

include_directories(
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
)

link_directories(
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/vendors/glps
)

add_executable(multiwindow main.c)

target_link_libraries(multiwindow PRIVATE 
    aroma
    GLPS
    GLESv2
    EGL
    X11
    freetype
    m
    pthread
)
