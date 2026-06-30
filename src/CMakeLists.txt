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
    core/utils/mpc.c
    core/aroma_incense.c
    core/aroma_incense_loader.c

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
    widgets/aroma_sidebar.c
    widgets/aroma_debug_overlay.c
    widgets/aroma_image.c
    widgets/aroma_icon.c
    widgets/aroma_canvas.c
    widgets/aroma_gif.c
    widgets/aroma_loading.c
    widgets/aroma_table.c
    widgets/aroma_map.c
)

target_include_directories(aroma
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/backends"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vendors/glps/internal"
    PRIVATE "$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>"
)

target_link_libraries(aroma PRIVATE freetype m)
if(NOT EMSCRIPTEN)
    target_link_libraries(aroma PRIVATE GLESv2 EGL)
endif()

if(UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_link_libraries(aroma PRIVATE curl pthread)
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
    target_link_libraries(aroma PRIVATE glfw)
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