option(ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)

add_library(aroma
    core/aroma_ui_impl.c
    core/aroma_logger.c
    core/aroma_node.c
    core/aroma_slab_alloc.c
    core/aroma_event.c
    core/aroma_font.c
    core/aroma_graphics_wrapper.c
    core/aroma_style.c
    core/aroma_time.c
    core/aroma_timer.c
    backends/platforms/aroma_platform_glps.c
    backends/graphics/aroma_graphics_gles3.c
    backends/graphics/utils/helpers_gles3.c
    backends/graphics/utils/aroma_gles3_text.c
    backends/aroma_abi.c

    widgets/aroma_window.c
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
)

target_include_directories(aroma
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends
    PRIVATE ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(aroma
    PUBLIC GLESv2
    PRIVATE freetype
    PRIVATE m
)

set_target_properties(aroma PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(aroma PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(aroma PRIVATE -fsanitize=address)

        if(NOT CMAKE_C_COMPILER_ID MATCHES "AppleClang")
            target_compile_options(aroma PRIVATE -fsanitize-address-use-after-scope)
        endif()
    elseif(MSVC)
        target_compile_options(aroma PRIVATE /fsanitize=address)
    endif()
endif()

