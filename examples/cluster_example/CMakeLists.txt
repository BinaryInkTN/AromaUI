cmake_minimum_required(VERSION 3.15)
project(ev_cluster_display C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


find_package(PkgConfig REQUIRED)
find_package(Vulkan QUIET)


pkg_check_modules(GLIB REQUIRED glib-2.0)

if(NOT TARGET aroma)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aroma_root)
endif()

add_executable(ev_cluster ev_cluster.c)

target_link_libraries(ev_cluster PRIVATE
    aroma
    m
    pthread
    rt
)

target_compile_options(ev_cluster PRIVATE
    -Wall -Wextra
)



install(TARGETS ev_cluster 
    RUNTIME DESTINATION bin
)

add_custom_target(run_cluster
    COMMAND ev_cluster
    DEPENDS ev_cluster
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)


add_custom_target(run_ev
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_ev_cluster.sh
    DEPENDS ev_cluster 
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)