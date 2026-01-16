add_library(aroma
    aroma_app.c
    aroma_logger.c
    aroma_node.c
    aroma_slab_alloc.c
    backends/aroma_glps_opengl.c
)

target_include_directories(aroma
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

set_target_properties(aroma PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

add_executable(aroma_app aroma_app.c)
target_link_libraries(aroma_app PRIVATE aroma)