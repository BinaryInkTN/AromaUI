find_package(Freetype REQUIRED)

# Test sources, grouped by framework category:
#   core/    - foundations: memory, scene graph, events, draw list, timing,
#              style/theme (headless, no GPU).
#   widgets/ - every widget, headless (no GPU, no display).
#   markup/  - Incense UI markup loading, headless.
#   gpu/     - real-GPU tests (separate binary, Linux + EGL only, see below).
add_executable(aroma_tests
    test_main.c
    core/test_aroma_slab_alloc.c
    core/test_aroma_node.c
    core/test_aroma_event_system.c
    core/test_aroma_drawlist.c
    core/test_aroma_timing.c
    core/test_aroma_color.c
    core/test_aroma_theme.c
    core/test_aroma_font_measure.c
    widgets/test_aroma_button.c
    widgets/test_aroma_container.c
    widgets/test_aroma_label.c
    widgets/test_aroma_glass.c
    widgets/test_aroma_inputs.c
    widgets/test_aroma_slider.c
    widgets/test_aroma_progress.c
    widgets/test_aroma_chip.c
    widgets/test_aroma_selection.c
    widgets/test_aroma_overlays.c
    widgets/test_aroma_display.c
    widgets/test_aroma_forms.c
    widgets/test_aroma_canvas.c
    widgets/test_aroma_table.c
    markup/test_aroma_incense.c
)

# AROMA_ROOT must work whether tests are added from the top-level build
# (CMAKE_SOURCE_DIR == repo root) or nested via
# examples/car_infotainment (CMAKE_SOURCE_DIR == example dir).
get_filename_component(AROMA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Incense + package helpers now live in the framework (libaroma);
# tests link `aroma` directly with no example sources.
target_link_libraries(aroma_tests PRIVATE aroma)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_compile_definitions(aroma_tests PRIVATE AROMA_HAS_ZLIB=1)
    target_link_libraries(aroma_tests PRIVATE ZLIB::ZLIB)
endif()

if(UNIX)
    target_link_libraries(aroma_tests PRIVATE ${CMAKE_DL_LIBS})
endif()

target_include_directories(aroma_tests 
    # Public API first: bare "aroma_*.h" must resolve to include/,
    # not to the src/core/ forwarding wrappers of the same name.
    PRIVATE ${AROMA_ROOT}/include
    PRIVATE ${AROMA_ROOT}/src
    PRIVATE ${AROMA_ROOT}/src/core
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/markup
)

if(ENABLE_ASAN AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aroma_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(aroma_tests PRIVATE -fsanitize=address)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "AppleClang")
        target_compile_options(aroma_tests PRIVATE -fsanitize-address-use-after-scope)
    endif()
endif()

add_test(NAME aroma_tests COMMAND aroma_tests)

# Headless GPU tests for backdrop blur: only on Linux desktops with EGL +
# GLESv2 available (Mesa software rendering is fine). Skipped elsewhere so
# embedded/web/CI builds stay portable.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(EGL_INCLUDE_DIR EGL/egl.h QUIET)
    find_library(EGL_LIB EGL QUIET)
    find_library(GLESV2_LIB GLESv2 QUIET)
    if(EGL_INCLUDE_DIR AND EGL_LIB AND GLESV2_LIB)
        add_executable(aroma_blur_gl_tests test_aroma_blur_gl.c)
        target_link_libraries(aroma_blur_gl_tests PRIVATE aroma ${EGL_LIB} ${GLESV2_LIB})
        target_include_directories(aroma_blur_gl_tests
            PRIVATE ${EGL_INCLUDE_DIR}
            PRIVATE ${AROMA_ROOT}/include
            PRIVATE ${AROMA_ROOT}/src/
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_test(NAME aroma_blur_gl_tests COMMAND aroma_blur_gl_tests)
    endif()
endif()