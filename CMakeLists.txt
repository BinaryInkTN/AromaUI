cmake_minimum_required(VERSION 3.15)
project(aroma LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)

add_subdirectory(vendors/glps)
add_subdirectory(src)

target_link_libraries(aroma PRIVATE GLPS)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests
    AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()
