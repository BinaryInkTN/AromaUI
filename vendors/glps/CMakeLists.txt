cmake_minimum_required(VERSION 3.10)

project(GLPS VERSION 1.0 LANGUAGES C)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


option(GLPS_INCLUDE_VULKAN "Enable Vulkan support in GLPS" OFF)
option(GLPS_FORCE_WAYLAND "Force Wayland backend" OFF)


# ==================================================
# Vulkan
# ==================================================

if(GLPS_INCLUDE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()



# ==================================================
# Platform
# ==================================================

if(WIN32)

    message(STATUS "Building Windows backend")


    add_library(GLPS

        src/glps_wgl_context.c
        src/glps_win32.c
        src/glps_window_manager.c

        src/utils/logger/pico_logger.c
        src/glps_timer.c
    )


    target_compile_definitions(GLPS
        PRIVATE

            GLPS_USE_WIN32
            _CRT_SECURE_NO_WARNINGS
    )


    target_link_libraries(GLPS
        PRIVATE

            opengl32
            gdi32
            user32
    )



elseif(UNIX AND NOT APPLE)


    message(STATUS "Configuring Linux backend")


    find_package(PkgConfig REQUIRED)


    pkg_check_modules(EGL REQUIRED egl)
    pkg_check_modules(ALSA REQUIRED alsa)



    # ==================================================
    # Wayland
    # ==================================================

    if(GLPS_FORCE_WAYLAND)


        message(STATUS "Building Wayland backend")


        pkg_check_modules(WAYLAND REQUIRED
            wayland-client
            wayland-egl
            wayland-protocols
        )


        find_program(
            WAYLAND_SCANNER
            wayland-scanner
            REQUIRED
        )


        execute_process(

            COMMAND
                ${PKG_CONFIG_EXECUTABLE}
                --variable=pkgdatadir
                wayland-protocols

            OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR

            OUTPUT_STRIP_TRAILING_WHITESPACE
        )


        set(XDG_PROTOCOL_XML
            ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
        )


        if(NOT EXISTS ${XDG_PROTOCOL_XML})

            message(FATAL_ERROR
                "Missing xdg-shell.xml: ${XDG_PROTOCOL_XML}"
            )

        endif()



        # -------------------------------
        # Generate xdg-shell protocol
        # -------------------------------

        set(GENERATED_XDG_DIR
            ${CMAKE_CURRENT_BINARY_DIR}/generated/xdg
        )


        set(GENERATED_XDG_HEADER
            ${GENERATED_XDG_DIR}/xdg-shell.h
        )


        set(GENERATED_XDG_SOURCE
            ${GENERATED_XDG_DIR}/xdg-shell-protocol.c
        )



        add_custom_command(

            OUTPUT
                ${GENERATED_XDG_HEADER}

            COMMAND
                ${CMAKE_COMMAND}
                -E
                make_directory
                ${GENERATED_XDG_DIR}

            COMMAND
                ${WAYLAND_SCANNER}
                client-header
                ${XDG_PROTOCOL_XML}
                ${GENERATED_XDG_HEADER}

            DEPENDS
                ${XDG_PROTOCOL_XML}

        )



        add_custom_command(

            OUTPUT
                ${GENERATED_XDG_SOURCE}

            COMMAND
                ${CMAKE_COMMAND}
                -E
                make_directory
                ${GENERATED_XDG_DIR}

            COMMAND
                ${WAYLAND_SCANNER}
                public-code
                ${XDG_PROTOCOL_XML}
                ${GENERATED_XDG_SOURCE}

            DEPENDS
                ${XDG_PROTOCOL_XML}

        )



        add_custom_target(

            generate_wayland_protocols

            DEPENDS

                ${GENERATED_XDG_HEADER}

                ${GENERATED_XDG_SOURCE}
        )



        add_library(GLPS

            src/glps_wayland.c
            src/glps_egl_context.c
            src/glps_window_manager.c

            src/utils/logger/pico_logger.c

            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c


            ${GENERATED_XDG_SOURCE}
            ${GENERATED_XDG_HEADER}

        )


        add_dependencies(GLPS
            generate_wayland_protocols
        )



        target_compile_definitions(GLPS

            PRIVATE

                GLPS_USE_WAYLAND
        )



        target_include_directories(GLPS

            PUBLIC

                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>

                $<BUILD_INTERFACE:${GENERATED_XDG_DIR}>

                $<INSTALL_INTERFACE:include/GLPS>


            PRIVATE

                ${PROJECT_SOURCE_DIR}/internal

                ${WAYLAND_INCLUDE_DIRS}

                ${EGL_INCLUDE_DIRS}

                ${ALSA_INCLUDE_DIRS}

        )



        target_link_libraries(GLPS

            PRIVATE

                ${WAYLAND_LIBRARIES}

                ${EGL_LIBRARIES}

                ${ALSA_LIBRARIES}

                GLESv2

                xkbcommon

                pthread

                rt

                m
        )



    # ==================================================
    # X11
    # ==================================================

    else()


        message(STATUS "Building X11 backend")


        pkg_check_modules(X11 REQUIRED x11)



        add_library(GLPS

            src/glps_x11.c
            src/glps_egl_context.c
            src/glps_window_manager.c

            src/utils/logger/pico_logger.c

            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
        )



        target_compile_definitions(GLPS

            PRIVATE

                GLPS_USE_X11
        )


        target_include_directories(GLPS

            PUBLIC

                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>

                $<INSTALL_INTERFACE:include/GLPS>


            PRIVATE

                ${PROJECT_SOURCE_DIR}/internal

                ${X11_INCLUDE_DIRS}

                ${EGL_INCLUDE_DIRS}

                ${ALSA_INCLUDE_DIRS}
        )



        target_link_libraries(GLPS

            PRIVATE

                ${X11_LIBRARIES}

                ${EGL_LIBRARIES}

                ${ALSA_LIBRARIES}

                GLESv2

                pthread

                rt

                m
        )


    endif()



else()

    message(FATAL_ERROR "Unsupported platform")

endif()



# ==================================================
# Install
# ==================================================

install(

    DIRECTORY include/

    DESTINATION include/GLPS
)


install(

    TARGETS GLPS

    EXPORT GLPSConfig

    LIBRARY DESTINATION lib

    ARCHIVE DESTINATION lib

    RUNTIME DESTINATION bin
)


install(

    EXPORT GLPSConfig

    FILE GLPSConfig.cmake

    NAMESPACE GLPS::

    DESTINATION lib/cmake/GLPS
)


set_target_properties(GLPS

    PROPERTIES

        VERSION ${PROJECT_VERSION}

        SOVERSION 1
)


enable_testing()