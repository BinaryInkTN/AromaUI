add_library(aroma
    aroma_logger.c
    aroma_node.c
    aroma_slab_alloc.c
)

target_include_directories(aroma
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(aroma PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)