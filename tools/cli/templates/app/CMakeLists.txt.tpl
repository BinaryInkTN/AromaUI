cmake_minimum_required(VERSION 3.10)
project({{PROJECT_NAME}} C)

set(CMAKE_C_STANDARD 11)

if(DEFINED ENV{AROMA_SDK_PATH})
    set(AROMA_DIR $ENV{AROMA_SDK_PATH})
else()
    set(AROMA_DIR "{{AROMA_ROOT}}")
endif()

message("Using Aroma SDK at: ${AROMA_DIR}")

add_subdirectory(${AROMA_DIR}/src aroma_sdk)

add_executable({{PROJECT_NAME}} src/main.c)

target_include_directories({{PROJECT_NAME}} PRIVATE ${AROMA_DIR}/include)
target_link_libraries({{PROJECT_NAME}} PRIVATE aroma)

if(EMSCRIPTEN)
    target_link_libraries({{PROJECT_NAME}} PRIVATE freetype m)
    target_link_options({{PROJECT_NAME}} PRIVATE
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=1"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sEXPORTED_RUNTIME_METHODS=['callMain','ccall','cwrap']"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        target_link_options({{PROJECT_NAME}} PRIVATE
            "--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        )
    endif()

    message(STATUS "Windowing Backend: EMSCRIPTEN/WebGL")
endif()
