add_executable(aroma_tests
    test_main.c
    test_aroma_slab_alloc.c
    test_aroma_node.c
)

target_link_libraries(aroma_tests aroma)

target_include_directories(aroma_tests 
    PRIVATE ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME aroma_tests COMMAND aroma_tests)