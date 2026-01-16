#include "test_aroma_slab_alloc.h"
#include "aroma_slab_alloc.h"
#include "aroma_logger.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_global_state(void) {

    __reset_node_id_counter();
}

static void test_memory_system_init(void) {
    reset_global_state();
    aroma_memory_system_init();

    aroma_memory_system_stats();

    aroma_memory_system_destroy();
    tests_passed++;
}

static void test_widget_allocation(void) {
    aroma_memory_system_init();

    void* widget1 = aroma_widget_alloc(10);   

    void* widget2 = aroma_widget_alloc(100);  

    void* widget3 = aroma_widget_alloc(300);  

    assert(widget1 != NULL);
    assert(widget2 != NULL);
    assert(widget3 != NULL);

    int* int_widget = (int*)widget1;
    *int_widget = 42;
    assert(*int_widget == 42);

    aroma_widget_free(widget1);
    void* widget4 = aroma_widget_alloc(10);
    assert(widget4 != NULL);

    aroma_widget_free(widget2);
    aroma_widget_free(widget3);
    aroma_widget_free(widget4);

    aroma_memory_system_destroy();
    tests_passed++;
}

static void test_widget_allocation_stress(void) {
    aroma_memory_system_init();

    const int NUM_WIDGETS = 100;
    void* widgets[NUM_WIDGETS];

    for (int i = 0; i < NUM_WIDGETS; i++) {
        size_t size = (i % 5) * 64 + 32;  

        widgets[i] = aroma_widget_alloc(size);
        assert(widgets[i] != NULL);

        memset(widgets[i], i, size);
    }

    for (int i = 0; i < NUM_WIDGETS; i += 2) {
        aroma_widget_free(widgets[i]);
        widgets[i] = NULL;
    }

    for (int i = 0; i < NUM_WIDGETS; i += 2) {
        size_t size = ((i + 1) % 5) * 64 + 32;
        widgets[i] = aroma_widget_alloc(size);
        assert(widgets[i] != NULL);
    }

    for (int i = 0; i < NUM_WIDGETS; i++) {
        if (widgets[i]) {
            aroma_widget_free(widgets[i]);
        }
    }

    aroma_memory_system_destroy();
    tests_passed++;
}

static void test_node_allocation(void) {
    aroma_memory_system_init();

    AromaNode* nodes[20];

    for (int i = 0; i < 20; i++) {
        nodes[i] = __slab_pool_alloc(&global_memory_system.node_pool);
        assert(nodes[i] != NULL);

        nodes[i]->node_id = i + 1;
        nodes[i]->node_type = NODE_TYPE_WIDGET;
        nodes[i]->child_count = 0;
    }

    for (int i = 0; i < 20; i++) {
        assert(nodes[i]->node_id == i + 1);
    }

    for (int i = 0; i < 20; i++) {
        __slab_pool_free(&global_memory_system.node_pool, nodes[i]);
    }

    AromaNode* new_node = __slab_pool_alloc(&global_memory_system.node_pool);
    assert(new_node != NULL);
    __slab_pool_free(&global_memory_system.node_pool, new_node);

    aroma_memory_system_destroy();
    tests_passed++;
}

static void test_mixed_allocation(void) {
    aroma_memory_system_init();

    void* root_widget = aroma_widget_alloc(64);
    void* button_widget = aroma_widget_alloc(128);
    void* label_widget = aroma_widget_alloc(256);

    AromaNode* root = __slab_pool_alloc(&global_memory_system.node_pool);
    AromaNode* button = __slab_pool_alloc(&global_memory_system.node_pool);
    AromaNode* label = __slab_pool_alloc(&global_memory_system.node_pool);

    assert(root != NULL);
    assert(button != NULL);
    assert(label != NULL);
    assert(root_widget != NULL);
    assert(button_widget != NULL);
    assert(label_widget != NULL);

    root->node_id = 1;
    root->node_type = NODE_TYPE_ROOT;
    root->node_widget_ptr = root_widget;

    button->node_id = 2;
    button->node_type = NODE_TYPE_WIDGET;
    button->node_widget_ptr = button_widget;

    label->node_id = 3;
    label->node_type = NODE_TYPE_WIDGET;
    label->node_widget_ptr = label_widget;

    aroma_widget_free(root_widget);
    aroma_widget_free(button_widget);
    aroma_widget_free(label_widget);

    __slab_pool_free(&global_memory_system.node_pool, root);
    __slab_pool_free(&global_memory_system.node_pool, button);
    __slab_pool_free(&global_memory_system.node_pool, label);

    aroma_memory_system_destroy();
    tests_passed++;
}

static void test_memory_statistics(void) {
    aroma_memory_system_init();

    LOG_INFO("Initial statistics:\n");
    aroma_memory_system_stats();

    void* widgets[5];
    for (int i = 0; i < 5; i++) {
        widgets[i] = aroma_widget_alloc(32 * (i + 1));
        assert(widgets[i] != NULL);
    }

    LOG_INFO("\nAfter allocating 5 widgets:\n");
    aroma_memory_system_stats();

    for (int i = 0; i < 3; i++) {
        aroma_widget_free(widgets[i]);
        widgets[i] = NULL;
    }

    LOG_INFO("\nAfter freeing 3 widgets:\n");
    aroma_memory_system_stats();

    for (int i = 3; i < 5; i++) {
        if (widgets[i]) {
            aroma_widget_free(widgets[i]);
        }
    }

    LOG_INFO("\nFinal statistics:\n");
    aroma_memory_system_stats();

    aroma_memory_system_destroy();
    tests_passed++;
}

static void test_invalid_widget_allocation(void) {
    aroma_memory_system_init();

    void* widget = aroma_widget_alloc(0);
    assert(widget == NULL);

    aroma_widget_free(NULL);

    aroma_memory_system_destroy();
    tests_passed++;
}

void run_slab_allocator_tests(int* passed, int* failed) {
    tests_passed = 0;
    tests_failed = 0;
    set_minimum_log_level(DEBUG_LEVEL_CRITICAL);


    printf("=== Multi-Cache Slab Allocator Tests ===\n");

    LOG_PERFORMANCE(NULL);
    test_memory_system_init();
    LOG_PERFORMANCE("test_memory_system_init");

    LOG_PERFORMANCE(NULL);
    test_widget_allocation();
    LOG_PERFORMANCE("test_widget_allocation");

    LOG_PERFORMANCE(NULL);
    test_widget_allocation_stress();
    LOG_PERFORMANCE("test_widget_allocation_stress");

    LOG_PERFORMANCE(NULL);
    test_node_allocation();
    LOG_PERFORMANCE("test_node_allocation");

    LOG_PERFORMANCE(NULL);
    test_mixed_allocation();
    LOG_PERFORMANCE("test_mixed_allocation");

    LOG_PERFORMANCE(NULL);
    test_memory_statistics();
    LOG_PERFORMANCE("test_memory_statistics");

    LOG_PERFORMANCE(NULL);
    test_invalid_widget_allocation();
    LOG_PERFORMANCE("test_invalid_widget_allocation");

    printf("\nMulti-Cache Slab Allocator: %d passed, %d failed\n\n", tests_passed, tests_failed);

    if (passed) *passed = tests_passed;
    if (failed) *failed = tests_failed;
}