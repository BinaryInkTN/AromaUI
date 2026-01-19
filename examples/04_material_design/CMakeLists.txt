cmake_minimum_required(VERSION 3.15)
project(material_design_example LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(AROMA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(material_design main.c)

target_include_directories(material_design PRIVATE
    ${AROMA_ROOT}/include
    ${AROMA_ROOT}/vendors/glps/include
)

target_link_directories(material_design PRIVATE
    ${AROMA_ROOT}/build/src
    ${AROMA_ROOT}/build/lib
)

# Link against aroma library and its dependencies
target_link_libraries(material_design PRIVATE
    aroma
    GLPS
    GLESv2
    freetype
    m
)
