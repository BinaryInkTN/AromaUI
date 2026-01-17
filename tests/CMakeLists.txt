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

if(ENABLE_ASAN AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aroma_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(aroma_tests PRIVATE -fsanitize=address)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "AppleClang")
        target_compile_options(aroma_tests PRIVATE -fsanitize-address-use-after-scope)
    endif()
endif()

add_test(NAME aroma_tests COMMAND aroma_tests)