cmake_minimum_required(VERSION 3.15)

# Guarded so projects that also add this directory themselves (with a
# different binary directory) do not create a duplicate target.
if(NOT TARGET nfd)
    project(nfd C)

    add_library(nfd STATIC
        src/nfd_common.c
        src/nfd_linux.c
    )

    target_include_directories(nfd PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    set_target_properties(nfd PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nfd PRIVATE -Wall -Wextra)
    endif()
endif()
