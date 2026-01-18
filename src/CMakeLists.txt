option(ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)

add_library(aroma
    aroma_ui_impl.c
    aroma_logger.c
    aroma_node.c
    aroma_slab_alloc.c
    aroma_event.c
    aroma_font.c
    aroma_graphics_wrapper.c
    aroma_style.c
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

add_executable(aroma_app aroma_app.c)
target_link_libraries(aroma_app PRIVATE aroma)
target_include_directories(aroma_app PRIVATE ${FREETYPE_INCLUDE_DIRS})

if(ENABLE_ASAN AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")

    target_link_options(aroma_app PRIVATE -fsanitize=address)
endif()