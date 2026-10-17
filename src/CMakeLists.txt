add_library(aroma
    core/aroma_ui_impl.c
    core/aroma_logger.c
    core/aroma_node.c
    core/aroma_animation.c
    core/aroma_layout.c
    core/aroma_slab_alloc.c
    core/aroma_event.c
    core/aroma_gesture.c
    core/aroma_font.c
    core/aroma_ubuntu_font.c
    core/aroma_graphics_wrapper.c
    core/aroma_style.c
    core/aroma_time.c
    core/aroma_timer.c
    core/aroma_velocity.c
    core/aroma_drawlist.c
    core/aroma_test.c
    core/aroma_native_utils.c
    core/utils/mpc.c
    core/aroma_3d.c
    core/cJSON.c
    core/aroma_incense.c
    core/aroma_incense_loader.c
    core/aroma_http.c

    backends/graphics/aroma_graphics_gles3.c
    backends/graphics/utils/helpers_gles3.c
    backends/graphics/utils/aroma_gles3_text.c

    backends/aroma_abi.c

    widgets/aroma_window.c
    widgets/aroma_container.c
    widgets/aroma_button.c
    widgets/aroma_slider.c
    widgets/aroma_switch.c
    widgets/aroma_textbox.c
    widgets/aroma_dropdown.c
    widgets/aroma_checkbox.c
    widgets/aroma_radiobutton.c
    widgets/aroma_progressbar.c
    widgets/aroma_label.c
    widgets/aroma_divider.c
    widgets/aroma_iconbutton.c
    widgets/aroma_dialog.c
    widgets/aroma_listview.c
    widgets/aroma_menu.c
    widgets/aroma_tooltip.c
    widgets/aroma_card.c
    widgets/aroma_chip.c
    widgets/aroma_snackbar.c
    widgets/aroma_tabs.c
    widgets/aroma_segmented.c
    widgets/aroma_sidebar.c
    widgets/aroma_debug_overlay.c
    widgets/aroma_image.c
    widgets/aroma_icon.c
    widgets/aroma_canvas.c
    widgets/aroma_gif.c
    widgets/aroma_video.c
    widgets/aroma_video_codec.c
    widgets/aroma_audio_out.c
    widgets/aroma_webview.c
    widgets/aroma_loading.c
    widgets/aroma_table.c
    widgets/aroma_map.c
    widgets/aroma_gauge.c
    widgets/aroma_3d_viewer.c
    widgets/aroma_calendar.c
    widgets/aroma_datepicker.c
    widgets/aroma_timepicker.c
    widgets/aroma_stepper.c
    widgets/aroma_carousel.c
)

target_include_directories(aroma
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/backends"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/backends/graphics/utils"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/internal"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/sqlite3"
    PRIVATE "$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>"
)

target_link_libraries(aroma PRIVATE freetype m SQLite3)
if(NOT EMSCRIPTEN)
    target_link_libraries(aroma PRIVATE GLESv2 EGL)
endif()

# ---- Vendored FFmpeg: decode all major video formats from source ----
# Builds minimal static libavformat/libavcodec/libswscale/libavutil from
# vendors/ffmpeg (git submodule, no system FFmpeg required). Desktop Linux
# only; all other platforms fall back to the built-in MPEG-1 decoder.
option(AROMA_ENABLE_FFMPEG "Build vendored FFmpeg for full video format support" ON)
set(AROMA_HAS_FFMPEG FALSE)
if(AROMA_ENABLE_FFMPEG AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND NOT EMSCRIPTEN)
    set(FFMPEG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/ffmpeg")
    if(EXISTS "${FFMPEG_SOURCE_DIR}/configure")
        include(ExternalProject)
        include(ProcessorCount)
        ProcessorCount(AROMA_FFMPEG_JOBS)
        if(NOT AROMA_FFMPEG_JOBS)
            set(AROMA_FFMPEG_JOBS 4)
        endif()
        set(FFMPEG_PREFIX "${CMAKE_BINARY_DIR}/ffmpeg")
        set(FFMPEG_INSTALL_DIR "${FFMPEG_PREFIX}/install")
        # Imported targets require their include dir to exist at configure
        # time; the FFmpeg install step creates it during the build.
        file(MAKE_DIRECTORY "${FFMPEG_INSTALL_DIR}/include")
        ExternalProject_Add(ffmpeg_external
            SOURCE_DIR "${FFMPEG_SOURCE_DIR}"
            BINARY_DIR "${FFMPEG_PREFIX}/build"
            INSTALL_DIR "${FFMPEG_INSTALL_DIR}"
            CONFIGURE_COMMAND "${FFMPEG_SOURCE_DIR}/configure"
                --prefix=${FFMPEG_INSTALL_DIR}
                --disable-everything
                --disable-programs --disable-doc --disable-debug
                --disable-avdevice --disable-avfilter
                --disable-network --disable-x86asm
                --disable-zlib --disable-bzlib --disable-lzma
                --disable-libdrm --disable-vaapi --disable-vdpau --disable-vulkan
                --disable-hwaccels
                --enable-pic --enable-static --disable-shared
                --cc=${CMAKE_C_COMPILER}
                --extra-cflags=-O2\ -fPIC
                --enable-demuxer=asf,avi,flv,gif,matroska,mov,mp3,mpegps,mpegts,mpegvideo,ogg,wav,aac,flac
                --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg1video,mpeg2video,mpeg4,h263,msmpeg4v3,wmv3,vc1,vp6,vp6a,vp6f,flv,theora,mjpeg,gif,aac,ac3,alac,flac,mp3,pcm_s16be,pcm_s16le,vorbis
                --enable-parser=aac,ac3,av1,flac,h264,hevc,mjpeg,mpegaudio,mpeg4video,mpegvideo,opus,png,vorbis,vp9,h263,vc1,gif
                --enable-protocol=file
            BUILD_COMMAND make -j${AROMA_FFMPEG_JOBS}
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS
                "${FFMPEG_INSTALL_DIR}/lib/libavformat.a"
                "${FFMPEG_INSTALL_DIR}/lib/libavcodec.a"
                "${FFMPEG_INSTALL_DIR}/lib/libswscale.a"
                "${FFMPEG_INSTALL_DIR}/lib/libswresample.a"
                "${FFMPEG_INSTALL_DIR}/lib/libavutil.a"
            UPDATE_COMMAND ""
            UPDATE_DISCONNECTED TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
        )
        foreach(_lib avformat avcodec swscale swresample avutil)
            add_library(FFmpeg::${_lib} STATIC IMPORTED GLOBAL)
            set_target_properties(FFmpeg::${_lib} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_INSTALL_DIR}/lib/lib${_lib}.a")
            target_include_directories(FFmpeg::${_lib} INTERFACE "${FFMPEG_INSTALL_DIR}/include")
            add_dependencies(FFmpeg::${_lib} ffmpeg_external)
        endforeach()
        target_link_libraries(aroma PRIVATE
            FFmpeg::avformat FFmpeg::avcodec FFmpeg::swscale FFmpeg::swresample FFmpeg::avutil)
        target_compile_definitions(aroma PRIVATE AROMA_HAS_FFMPEG=1)
        # Decoder sources need the vendored headers too.
        target_include_directories(aroma PRIVATE "${FFMPEG_INSTALL_DIR}/include")
        set(AROMA_HAS_FFMPEG TRUE)
        message(STATUS "AromaUI: building vendored FFmpeg (full video format support)")
    else()
        message(STATUS "AromaUI: vendors/ffmpeg missing (submodule not initialized) - video falls back to MPEG-1 only")
    endif()
endif()

# ---- Vendored litehtml: software HTML/CSS webview, no system browser ----
option(AROMA_ENABLE_WEBVIEW "Build vendored litehtml HTML renderer for the webview widget" ON)
set(AROMA_HAS_WEBVIEW FALSE)
if(AROMA_ENABLE_WEBVIEW AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID) AND NOT EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/litehtml/CMakeLists.txt")
        enable_language(CXX)
        # NOTE: CACHE FORCE is required here. litehtml's own option() calls
        # clear normal variables under CMP0077-OLD, which silently re-enabled
        # its clang-tidy lint (fails Android builds: incompatible .clang-tidy
        # plus VirtualCall warnings-as-errors) and its test suite.
        set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
        set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendors/litehtml" litehtml_build)
        target_sources(aroma PRIVATE widgets/aroma_webview_render.cpp)
        set_source_files_properties(widgets/aroma_webview_render.cpp PROPERTIES
            COMPILE_OPTIONS "-fexceptions;-Wno-c99-designator")
        # Vendored code: silence warnings we don't own (NDK clang is far
        # pickier than desktop GCC; e.g. QuickJS-style macros are fine).
        foreach(_vendored litehtml gumbo)
            if(TARGET ${_vendored})
                target_compile_options(${_vendored} PRIVATE -w)
            endif()
        endforeach()
        # NOTE: CXX_STANDARD is a target property — setting it on a source
        # file is silently ignored. Desktop GCC defaults to C++17 so this
        # went unnoticed, but NDK clang defaults to C++14 and litehtml
        # needs C++17 (optional/variant/clamp, folds, inline variables).
        # (Mirrors vendors/litehtml's own set_target_properties.)
        set_target_properties(aroma PROPERTIES CXX_STANDARD 17)
        target_link_libraries(aroma PRIVATE litehtml)
        # Consumers link with the C driver, so export the C++ runtime.
        # (Android provides libc++ via the NDK toolchain — do not force libstdc++.)
        if(NOT ANDROID)
            target_link_libraries(aroma PUBLIC stdc++)
        endif()
        # QuickJS: embeddable JS engine (scripts, DOM, timers, bridges).
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/quickjs/quickjs.c")
            target_sources(aroma PRIVATE
                ../vendors/quickjs/quickjs.c
                ../vendors/quickjs/dtoa.c
                ../vendors/quickjs/libregexp.c
                ../vendors/quickjs/libunicode.c
                ../vendors/quickjs/cutils.c)
            target_include_directories(aroma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/quickjs")
            set_source_files_properties(
                ../vendors/quickjs/quickjs.c
                ../vendors/quickjs/dtoa.c
                ../vendors/quickjs/libregexp.c
                ../vendors/quickjs/libunicode.c
                ../vendors/quickjs/cutils.c PROPERTIES
                COMPILE_DEFINITIONS "_GNU_SOURCE;CONFIG_VERSION=\"2026-06-04\""
                COMPILE_OPTIONS "-w")
            target_compile_definitions(aroma PRIVATE AROMA_WEBVIEW_JS=1)
            message(STATUS "AromaUI: JavaScript enabled in webview (vendored QuickJS)")
        else()
            message(STATUS "AromaUI: vendors/quickjs missing - webview without JavaScript")
        endif()
        target_compile_definitions(aroma PRIVATE AROMA_HAS_WEBVIEW=1)
        set(AROMA_HAS_WEBVIEW TRUE)
        message(STATUS "AromaUI: building vendored litehtml (webview support)")
    else()
        message(STATUS "AromaUI: vendors/litehtml missing (submodule not initialized) - webview disabled")
    endif()
endif()






include(CheckIncludeFile)
check_include_file(pthread.h HAVE_PTHREAD_H)
if(HAVE_PTHREAD_H AND NOT EMSCRIPTEN)
    target_compile_definitions(aroma PRIVATE AROMA_THREAD_SAFE=1)
endif()
check_include_file(alsa/asoundlib.h HAVE_ALSA_H)
if(HAVE_ALSA_H AND UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_compile_definitions(aroma PRIVATE AROMA_HAS_ALSA=1)
endif()

if(UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_link_libraries(aroma PRIVATE curl pthread)
    find_library(ALSA_LIB asound)
    if(ALSA_LIB)
        target_link_libraries(aroma PRIVATE "${ALSA_LIB}")
    endif()
endif()

find_package(Vulkan QUIET)

if(ANDROID AND NOT Vulkan_FOUND)
    find_library(ANDROID_VULKAN_LIB vulkan)
    if(ANDROID_VULKAN_LIB)
        set(Vulkan_FOUND TRUE)
        set(ANDROID_VULKAN_NATIVE TRUE)
    endif()
endif()

if(Vulkan_FOUND AND NOT EMSCRIPTEN)
    target_sources(aroma PRIVATE
        backends/graphics/aroma_graphics_vulkan.c
        backends/graphics/utils/helpers_vulkan.c
        backends/graphics/utils/aroma_vulkan_text.c
    )
    if(ANDROID_VULKAN_NATIVE)
        target_link_libraries(aroma PUBLIC "${ANDROID_VULKAN_LIB}")
    else()
        target_include_directories(aroma PUBLIC "${Vulkan_INCLUDE_DIRS}")
        target_link_libraries(aroma PUBLIC Vulkan::Vulkan)
    endif()
    target_compile_definitions(aroma PRIVATE AROMA_HAS_VULKAN=1)
endif()


if(ANDROID)
    target_sources(aroma PRIVATE backends/platforms/aroma_platform_android.c)
    target_link_libraries(aroma PRIVATE android EGL log GLESv3 mediandk OpenSLES)
    target_include_directories(aroma PRIVATE "${ANDROID_NDK}/sources/android/native_app_glue")
elseif(EMSCRIPTEN)
    target_sources(aroma PRIVATE
        backends/platforms/aroma_platform_emscripten.c
        backends/graphics/aroma_graphics_emscripten_stub.c
    )
    target_compile_definitions(aroma PUBLIC AROMA_USE_EMSCRIPTEN)
    target_link_options(aroma PUBLIC "-sUSE_WEBGL2=1" "-sASYNCIFY=1")
elseif(ENABLE_GLFW)
    target_sources(aroma PRIVATE backends/platforms/aroma_platform_glfw.c)
    target_link_libraries(aroma PRIVATE glfw)
    target_compile_definitions(aroma PUBLIC AROMA_USE_GLFW)
else()
    target_sources(aroma PRIVATE backends/platforms/aroma_platform_glps.c)

    if(TARGET GLPS)
        get_target_property(_glps_type GLPS TYPE)
        if(_glps_type STREQUAL "SHARED_LIBRARY")
            message(FATAL_ERROR "GLPS must be statically linked, but it was configured as a SHARED library.")
        endif()
    endif()

    target_link_libraries(aroma PRIVATE GLPS)

    get_target_property(_glps_defs GLPS COMPILE_DEFINITIONS)
    if(_glps_defs)
        foreach(_def IN LISTS _glps_defs)
            if(_def MATCHES "^GLPS_USE_")
                target_compile_definitions(aroma PRIVATE "${_def}")
            endif()
        endforeach()
    endif()
endif()

set_target_properties(aroma PROPERTIES
    VERSION 0.0.1
    SOVERSION 1
)