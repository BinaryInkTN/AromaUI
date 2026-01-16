#include "test_aroma_node.h"
#include "test_aroma_slab_alloc.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

typedef struct {
    int id;
    char name[32];
} MockWidgetSmall;  

typedef struct {
    float position[4];
    char text[64];
    int flags;
    double scale;
} MockWidgetMedium;  

typedef struct {
    int data[32];
    float matrix[16];
    char description[128];
} MockWidgetLarge;  

static void init_test_environment(void) {
    __node_system_init();
}

static void cleanup_test_environment(void) {
    __node_system_destroy();
}

static void test_node_id_generation(void) {
    init_test_environment();

    uint64_t id1 = __generate_node_id();
    uint64_t id2 = __generate_node_id();
    uint64_t id3 = __generate_node_id();

    assert(id1 == 1);
    assert(id2 == 2);
    assert(id3 == 3);
    assert(id1 < id2 && id2 < id3);

    cleanup_test_environment();
    tests_passed++;
}

static void test_create_root_node(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    assert(root_widget != NULL);
    root_widget->id = 1;
    strncpy(root_widget->name, "Root", 31);

    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    assert(root != NULL);
    assert(root->node_id != 0);
    assert(root->node_type == NODE_TYPE_ROOT);
    assert(root->parent_node == NULL);
    assert(root->node_widget_ptr == root_widget);
    assert(root->child_count == 0);

    __destroy_node(root);
    cleanup_test_environment();
    tests_passed++;
}

static void test_create_child_nodes_with_different_widgets(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    root_widget->id = 1;
    strncpy(root_widget->name, "Root", 31);
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    MockWidgetMedium* button_widget = (MockWidgetMedium*)aroma_widget_alloc(sizeof(MockWidgetMedium));
    button_widget->position[0] = 10.0f;
    button_widget->position[1] = 20.0f;
    strncpy(button_widget->text, "Click Me", 63);

    MockWidgetLarge* label_widget = (MockWidgetLarge*)aroma_widget_alloc(sizeof(MockWidgetLarge));
    for (int i = 0; i < 32; i++) label_widget->data[i] = i;
    strncpy(label_widget->description, "This is a large label widget", 127);

    AromaNode* button = __add_child_node(NODE_TYPE_WIDGET, root, button_widget);
    AromaNode* label = __add_child_node(NODE_TYPE_WIDGET, root, label_widget);

    assert(button != NULL);
    assert(label != NULL);
    assert(root->child_count == 2);
    assert(button->parent_node == root);
    assert(label->parent_node == root);
    assert(root->child_nodes[0] == button);
    assert(root->child_nodes[1] == label);
    assert(button->node_type == NODE_TYPE_WIDGET);
    assert(label->node_type == NODE_TYPE_WIDGET);

    MockWidgetMedium* retrieved_button = (MockWidgetMedium*)button->node_widget_ptr;
    assert(retrieved_button == button_widget);
    assert(strcmp(retrieved_button->text, "Click Me") == 0);

    MockWidgetLarge* retrieved_label = (MockWidgetLarge*)label->node_widget_ptr;
    assert(retrieved_label == label_widget);
    assert(strcmp(retrieved_label->description, "This is a large label widget") == 0);

    __destroy_node(root);
    cleanup_test_environment();
    tests_passed++;
}

static void test_node_hierarchy(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    MockWidgetMedium* container_widget = (MockWidgetMedium*)aroma_widget_alloc(sizeof(MockWidgetMedium));
    MockWidgetSmall* button_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));

    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);
    AromaNode* container = __add_child_node(NODE_TYPE_CONTAINER, root, container_widget);
    AromaNode* button = __add_child_node(NODE_TYPE_WIDGET, container, button_widget);

    assert(root->child_count == 1);
    assert(container->child_count == 1);
    assert(button->child_count == 0);
    assert(container->parent_node == root);
    assert(button->parent_node == container);

    __destroy_node(root);
    cleanup_test_environment();
    tests_passed++;
}

static void test_remove_child_node(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    AromaNode* children[3];
    for (int i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Child%d", i);
        MockWidgetSmall* w = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
        w->id = i + 1;
        strncpy(w->name, name, 31);
        children[i] = __add_child_node(NODE_TYPE_WIDGET, root, w);
    }

    assert(root->child_count == 3);

    AromaNode* removed = __remove_child_node(root, children[1]->node_id);
    assert(removed == children[1]);
    assert(root->child_count == 2);
    assert(root->child_nodes[0] == children[0]);
    assert(root->child_nodes[1] == children[2]);

    removed = __remove_child_node(root, children[0]->node_id);
    assert(removed == children[0]);
    assert(root->child_count == 1);
    assert(root->child_nodes[0] == children[2]);

    removed = __remove_child_node(root, 99999);
    assert(removed == NULL);
    assert(root->child_count == 1);

    aroma_widget_free(children[0]->node_widget_ptr);
    aroma_widget_free(children[1]->node_widget_ptr);

    __destroy_node(root);
    cleanup_test_environment();
    tests_passed++;
}

static void test_find_node_by_id(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    MockWidgetMedium* a_widget = (MockWidgetMedium*)aroma_widget_alloc(sizeof(MockWidgetMedium));
    MockWidgetSmall* b_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    MockWidgetLarge* c_widget = (MockWidgetLarge*)aroma_widget_alloc(sizeof(MockWidgetLarge));

    AromaNode* node_a = __add_child_node(NODE_TYPE_CONTAINER, root, a_widget);
    AromaNode* node_b = __add_child_node(NODE_TYPE_WIDGET, node_a, b_widget);
    AromaNode* node_c = __add_child_node(NODE_TYPE_WIDGET, node_a, c_widget);

    AromaNode* found = __find_node_by_id(root, root->node_id);
    assert(found == root);

    found = __find_node_by_id(root, node_a->node_id);
    assert(found == node_a);

    found = __find_node_by_id(root, node_b->node_id);
    assert(found == node_b);

    found = __find_node_by_id(root, node_c->node_id);
    assert(found == node_c);

    found = __find_node_by_id(root, 99999);
    assert(found == NULL);

    found = __find_node_by_id(NULL, root->node_id);
    assert(found == NULL);

    __destroy_node(root);
    cleanup_test_environment();
    tests_passed++;
}

static void test_max_children_limit(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Child%d", i);
        MockWidgetSmall* w = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
        w->id = i + 1;
        strncpy(w->name, name, 31);
        AromaNode* child = __add_child_node(NODE_TYPE_WIDGET, root, w);
        assert(child != NULL);
    }

    assert(root->child_count == AROMA_MAX_CHILD_NODES);

    MockWidgetSmall* extra_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    AromaNode* extra = __add_child_node(NODE_TYPE_WIDGET, root, extra_widget);
    assert(extra == NULL);
    assert(root->child_count == AROMA_MAX_CHILD_NODES);

    aroma_widget_free(extra_widget);

    __destroy_node(root);
    cleanup_test_environment();
    tests_passed++;
}

static void test_invalid_parameters(void) {
    init_test_environment();

    MockWidgetSmall* widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));

    AromaNode* node = __create_node(NODE_TYPE_WIDGET, NULL, widget);
    assert(node == NULL);

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    node = __add_child_node(NODE_TYPE_WIDGET, root, NULL);
    assert(node == NULL);

    node = __add_child_node((AromaNodeType)999, root, widget);
    assert(node == NULL);

    AromaNode* removed = __remove_child_node(NULL, 1);
    assert(removed == NULL);

    aroma_widget_free(NULL);

    if (root) __destroy_node(root);
    aroma_widget_free(widget);

    cleanup_test_environment();
    tests_passed++;
}

static void test_destroy_node_tree(void) {
    init_test_environment();

    MockWidgetSmall* root_widget = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    MockWidgetMedium* container1_widget = (MockWidgetMedium*)aroma_widget_alloc(sizeof(MockWidgetMedium));
    MockWidgetMedium* container2_widget = (MockWidgetMedium*)aroma_widget_alloc(sizeof(MockWidgetMedium));
    AromaNode* container1 = __add_child_node(NODE_TYPE_CONTAINER, root, container1_widget);
    AromaNode* container2 = __add_child_node(NODE_TYPE_CONTAINER, root, container2_widget);

    for (int i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Button%d-%d", 1, i);
        MockWidgetSmall* w = (MockWidgetSmall*)aroma_widget_alloc(sizeof(MockWidgetSmall));
        w->id = i + 1;
        strncpy(w->name, name, 31);
        __add_child_node(NODE_TYPE_WIDGET, container1, w);
    }

    for (int i = 0; i < 2; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Label%d-%d", 2, i);
        MockWidgetLarge* w = (MockWidgetLarge*)aroma_widget_alloc(sizeof(MockWidgetLarge));
        for (int j = 0; j < 32; j++) w->data[j] = i * 10 + j;
        __add_child_node(NODE_TYPE_WIDGET, container2, w);
    }

    assert(root->child_count == 2);
    assert(container1->child_count == 3);
    assert(container2->child_count == 2);

    __destroy_node(root);

    cleanup_test_environment();
    tests_passed++;
}

static void test_widget_allocation_buckets(void) {
    init_test_environment();

    void* tiny_widget = aroma_widget_alloc(10);      

    void* small_widget = aroma_widget_alloc(40);     

    void* medium_widget = aroma_widget_alloc(100);   

    void* large_widget = aroma_widget_alloc(200);    

    void* huge_widget = aroma_widget_alloc(400);     

    assert(tiny_widget != NULL);
    assert(small_widget != NULL);
    assert(medium_widget != NULL);
    assert(large_widget != NULL);
    assert(huge_widget != NULL);

    aroma_widget_free(tiny_widget);
    aroma_widget_free(small_widget);
    aroma_widget_free(medium_widget);
    aroma_widget_free(large_widget);
    aroma_widget_free(huge_widget);

    void* min_widget = aroma_widget_alloc(1);
    assert(min_widget != NULL);
    aroma_widget_free(min_widget);

    cleanup_test_environment();
    tests_passed++;
}

void run_node_tests(int* passed, int* failed) {
    set_minimum_log_level(DEBUG_LEVEL_CRITICAL);
    tests_passed = 0;
    tests_failed = 0;

    printf("=== Multi-Cache Node System Tests ===\n");
    LOG_PERFORMANCE(NULL);
    test_node_id_generation();
    LOG_PERFORMANCE("test_node_id_generation");

    LOG_PERFORMANCE(NULL);
    test_create_root_node();
    LOG_PERFORMANCE("test_create_root_node");

    LOG_PERFORMANCE(NULL);
    test_create_child_nodes_with_different_widgets();
    LOG_PERFORMANCE("test_create_child_nodes_with_different_widgets");

    LOG_PERFORMANCE(NULL);
    test_node_hierarchy();
    LOG_PERFORMANCE("test_node_hierarchy");

    LOG_PERFORMANCE(NULL);
    test_remove_child_node();
    LOG_PERFORMANCE("test_remove_child_node");

    LOG_PERFORMANCE(NULL);
    test_find_node_by_id();
    LOG_PERFORMANCE("test_find_node_by_id");

    LOG_PERFORMANCE(NULL);
    test_max_children_limit();
    LOG_PERFORMANCE("test_max_children_limit");

    LOG_PERFORMANCE(NULL);
    test_invalid_parameters();
    LOG_PERFORMANCE("test_invalid_parameters");

    LOG_PERFORMANCE(NULL);
    test_destroy_node_tree();
    LOG_PERFORMANCE("test_destroy_node_tree");

    LOG_PERFORMANCE(NULL);
    test_widget_allocation_buckets();
    LOG_PERFORMANCE("test_widget_allocation_buckets");

    printf("\nMulti-Cache Node System: %d passed, %d failed\n\n", tests_passed, tests_failed);

    if (passed) *passed = tests_passed;
    if (failed) *failed = tests_failed;
}