cmake_minimum_required(VERSION 3.15)
project(aroma LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(src)
find_package(GLPS REQUIRED)
add_subdirectory(vendors/glps)
target_link_libraries(aroma PRIVATE GLPS)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()