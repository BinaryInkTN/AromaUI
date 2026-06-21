cmake_minimum_required(VERSION 3.15)
project(ev_cluster_display C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLFW "Use GLFW instead of GLPS for windowing" ON)

find_package(PkgConfig REQUIRED)
find_package(Vulkan QUIET)

pkg_check_modules(GSTREAMER REQUIRED 
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

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

if(ENABLE_GLFW AND Vulkan_FOUND)
    target_link_libraries(ev_cluster PRIVATE Vulkan::Vulkan)
endif()

add_executable(aroma_display display.c)

target_include_directories(aroma_display PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(aroma_display PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GLIB_LIBRARIES}
    pthread
    rt
)

target_compile_options(aroma_display PRIVATE
    -Wall -Wextra
    -DG_LOG_DOMAIN="aroma_display"
)

install(TARGETS ev_cluster aroma_display
    RUNTIME DESTINATION bin
)

add_custom_target(run_cluster
    COMMAND ev_cluster
    DEPENDS ev_cluster
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_display
    COMMAND aroma_display
    DEPENDS aroma_display
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_ev
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_ev_cluster.sh
    DEPENDS ev_cluster aroma_display
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)