if(NOT ANDROID AND NOT EMSCRIPTEN)
    if(NOT TARGET GLPS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/CMakeLists.txt")
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps" glps_build)
        else()
            message(STATUS "GLPS vendor not found at ../vendors/glps")
        endif()
    endif()

    if(ENABLE_GLFW AND NOT TARGET glfw)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glfw/CMakeLists.txt")
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glfw" glfw_build)
        else()
            message(FATAL_ERROR "glfw3 not found and vendor submodule missing.")
        endif()
    endif()
endif()

if(NOT TARGET freetype)
    find_package(Freetype QUIET)
    if(FREETYPE_FOUND)
        add_library(freetype ALIAS Freetype::Freetype)
    else()
        set(FT_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/freetype")
        if(EXISTS "${FT_VENDOR_DIR}/CMakeLists.txt")
            set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
            add_subdirectory("${FT_VENDOR_DIR}" freetype_build)
        else()
            message(FATAL_ERROR "Freetype not found and vendor submodule missing.")
        endif()
    endif()
endif()

add_library(aroma
    core/aroma_ui_impl.c
    core/aroma_logger.c
    core/aroma_node.c
    core/aroma_animation.c
    core/aroma_layout.c
    core/aroma_slab_alloc.c
    core/aroma_event.c
    core/aroma_font.c
    core/aroma_ubuntu_font.c
    core/aroma_graphics_wrapper.c
    core/aroma_style.c
    core/aroma_time.c
    core/aroma_timer.c
    core/aroma_drawlist.c
    core/aroma_native_utils.c

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
    widgets/aroma_fab.c
    widgets/aroma_snackbar.c
    widgets/aroma_tabs.c
    widgets/aroma_sidebar.c
    widgets/aroma_debug_overlay.c
    widgets/aroma_image.c
    widgets/aroma_icon.c
    widgets/aroma_canvas.c
    widgets/aroma_gif.c
    widgets/aroma_loading.c
    widgets/aroma_table.c
)

target_include_directories(aroma
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/backends"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/internal"
    PRIVATE "$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>"
)

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
endif()

target_link_libraries(aroma PRIVATE freetype m)

if(NOT EMSCRIPTEN)
    target_link_libraries(aroma PRIVATE GLESv2)
endif()

if(UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_link_libraries(aroma PRIVATE curl pthread)
endif()

if(Vulkan_FOUND)
    if(ANDROID_VULKAN_NATIVE)
        target_link_libraries(aroma PUBLIC "${ANDROID_VULKAN_LIB}")
    else()
        target_include_directories(aroma PUBLIC "${Vulkan_INCLUDE_DIRS}")
        target_link_libraries(aroma PUBLIC Vulkan::Vulkan)
    endif()
    target_compile_definitions(aroma PRIVATE AROMA_HAS_VULKAN=1)
endif()

if(NOT EMSCRIPTEN)
    target_sources(aroma PRIVATE widgets/aroma_map.c)
endif()

if(ANDROID)
    target_sources(aroma PRIVATE backends/platforms/aroma_platform_android.c)
    target_link_libraries(aroma PRIVATE android EGL log GLESv3)
    target_include_directories(aroma PRIVATE "${ANDROID_NDK}/sources/android/native_app_glue")
elseif(EMSCRIPTEN)
    target_sources(aroma PRIVATE
        backends/platforms/aroma_platform_emscripten.c
        backends/graphics/aroma_graphics_emscripten_stub.c
    )
    target_compile_definitions(aroma PUBLIC AROMA_USE_EMSCRIPTEN)
elseif(ENABLE_GLFW)
    target_sources(aroma PRIVATE backends/platforms/aroma_platform_glfw.c)
    if(TARGET glfw)
        target_link_libraries(aroma PRIVATE glfw)
    else()
        target_link_libraries(aroma PRIVATE glfw3)
    endif()
    target_compile_definitions(aroma PUBLIC AROMA_USE_GLFW)
else()
    target_sources(aroma PRIVATE backends/platforms/aroma_platform_glps.c)
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