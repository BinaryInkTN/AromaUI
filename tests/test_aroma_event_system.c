/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "test_aroma_event_system.h"
#include "aroma_event.h"
#include "aroma_slab_alloc.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static int handler_call_count = 0;
static int last_event_type = -1;
static uint64_t last_node_id = 0;

static bool test_event_handler(AromaEvent* ev, void* user_data) {
    (void)user_data;
    handler_call_count++;
    last_event_type = ev->event_type;
    last_node_id = ev->target_node_id;
    return false;
}

static bool consuming_handler(AromaEvent* ev, void* user_data) {
    (void)user_data;
    handler_call_count++;
    aroma_event_consume(ev);
    return true;
}

static void init_test_environment(void) {
    __node_system_init();
    aroma_event_system_init();
}

static void cleanup_test_environment(void) {
    aroma_event_system_shutdown();
    __node_system_destroy();
}

static AromaNode* create_basic_tree(void) {
    void* root_widget = aroma_widget_alloc(32);
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    void* child_widget = aroma_widget_alloc(32);
    __add_child_node(NODE_TYPE_WIDGET, root, child_widget);

    aroma_event_set_root(root);
    return root;
}

static void test_event_system_init_shutdown(void) {
    init_test_environment();
    cleanup_test_environment();
    tests_passed++;
}

static void test_event_creation_and_destruction(void) {
    init_test_environment();
    AromaNode* root = create_basic_tree();

    AromaEvent* ev = aroma_event_create(EVENT_TYPE_MOUSE_MOVE, root->node_id);
    assert(ev != NULL);
    assert(ev->event_type == EVENT_TYPE_MOUSE_MOVE);
    assert(ev->target_node == root);

    aroma_event_destroy(ev);
    cleanup_test_environment();
    tests_passed++;
}

static void test_event_subscription_and_dispatch(void) {
    init_test_environment();
    AromaNode* root = create_basic_tree();

    handler_call_count = 0;

    bool ok = aroma_event_subscribe(
        root->node_id,
        EVENT_TYPE_MOUSE_MOVE,
        test_event_handler,
        NULL,
        0
    );
    assert(ok);

    AromaEvent* ev = aroma_event_create(EVENT_TYPE_MOUSE_MOVE, root->node_id);
    assert(ev);

    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);

    assert(handler_call_count == 1);
    assert(last_event_type == EVENT_TYPE_MOUSE_MOVE);
    assert(last_node_id == root->node_id);

    cleanup_test_environment();
    tests_passed++;
}

static void test_event_bubbling(void) {
    init_test_environment();

    void* root_widget = aroma_widget_alloc(32);
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    void* container_widget = aroma_widget_alloc(32);
    AromaNode* container = __add_child_node(NODE_TYPE_CONTAINER, root, container_widget);

    void* button_widget = aroma_widget_alloc(32);
    AromaNode* button = __add_child_node(NODE_TYPE_WIDGET, container, button_widget);

    aroma_event_set_root(root);

    handler_call_count = 0;

    aroma_event_subscribe(
        root->node_id,
        EVENT_TYPE_MOUSE_CLICK,
        test_event_handler,
        NULL,
        0
    );

    AromaEvent* ev = aroma_event_create(EVENT_TYPE_MOUSE_CLICK, button->node_id);
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);

    assert(handler_call_count == 1);
    assert(last_node_id == button->node_id);

    cleanup_test_environment();
    tests_passed++;
}

static void test_event_consumption_stops_bubbling(void) {
    init_test_environment();

    void* root_widget = aroma_widget_alloc(32);
    AromaNode* root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    void* child_widget = aroma_widget_alloc(32);
    AromaNode* child = __add_child_node(NODE_TYPE_WIDGET, root, child_widget);

    aroma_event_set_root(root);

    handler_call_count = 0;

    aroma_event_subscribe(
        child->node_id,
        EVENT_TYPE_MOUSE_CLICK,
        consuming_handler,
        NULL,
        10
    );

    aroma_event_subscribe(
        root->node_id,
        EVENT_TYPE_MOUSE_CLICK,
        test_event_handler,
        NULL,
        0
    );

    AromaEvent* ev = aroma_event_create(EVENT_TYPE_MOUSE_CLICK, child->node_id);
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);

    assert(handler_call_count == 1);

    cleanup_test_environment();
    tests_passed++;
}

static void test_event_queue_processing(void) {
    init_test_environment();
    AromaNode* root = create_basic_tree();

    handler_call_count = 0;

    aroma_event_subscribe(
        root->node_id,
        EVENT_TYPE_KEY_PRESS,
        test_event_handler,
        NULL,
        0
    );

    AromaEvent* ev = aroma_event_create_key(
        EVENT_TYPE_KEY_PRESS,
        root->node_id,
        42,
        0
    );

    aroma_event_queue(ev);
    aroma_event_process_queue();

    assert(handler_call_count == 1);
    assert(last_event_type == EVENT_TYPE_KEY_PRESS);

    cleanup_test_environment();
    tests_passed++;
}

static void test_unsubscribe_listener(void) {
    init_test_environment();
    AromaNode* root = create_basic_tree();

    handler_call_count = 0;

    aroma_event_subscribe(
        root->node_id,
        EVENT_TYPE_MOUSE_MOVE,
        test_event_handler,
        NULL,
        0
    );

    bool removed = aroma_event_unsubscribe(
        root->node_id,
        EVENT_TYPE_MOUSE_MOVE,
        test_event_handler
    );
    assert(removed);

    AromaEvent* ev = aroma_event_create(EVENT_TYPE_MOUSE_MOVE, root->node_id);
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);

    assert(handler_call_count == 0);

    cleanup_test_environment();
    tests_passed++;
}

static void test_invalid_event_parameters(void) {
    init_test_environment();

    AromaEvent* ev = aroma_event_create(EVENT_TYPE_MOUSE_MOVE, 0);
    assert(ev == NULL);

    bool ok = aroma_event_subscribe(
        0,
        EVENT_TYPE_MOUSE_MOVE,
        test_event_handler,
        NULL,
        0
    );
    assert(ok == false);

    cleanup_test_environment();
    tests_passed++;
}

void run_event_tests(int* passed, int* failed) {
    set_minimum_log_level(DEBUG_LEVEL_CRITICAL);
    tests_passed = 0;
    tests_failed = 0;

    printf("=== Aroma Event System Tests ===\n");

    LOG_PERFORMANCE(NULL);
    test_event_system_init_shutdown();
    LOG_PERFORMANCE("test_event_system_init_shutdown");

    LOG_PERFORMANCE(NULL);
    test_event_creation_and_destruction();
    LOG_PERFORMANCE("test_event_creation_and_destruction");

    LOG_PERFORMANCE(NULL);
    test_event_subscription_and_dispatch();
    LOG_PERFORMANCE("test_event_subscription_and_dispatch");

    LOG_PERFORMANCE(NULL);
    test_event_bubbling();
    LOG_PERFORMANCE("test_event_bubbling");

    LOG_PERFORMANCE(NULL);
    test_event_consumption_stops_bubbling();
    LOG_PERFORMANCE("test_event_consumption_stops_bubbling");

    LOG_PERFORMANCE(NULL);
    test_event_queue_processing();
    LOG_PERFORMANCE("test_event_queue_processing");

    LOG_PERFORMANCE(NULL);
    test_unsubscribe_listener();
    LOG_PERFORMANCE("test_unsubscribe_listener");

    LOG_PERFORMANCE(NULL);
    test_invalid_event_parameters();
    LOG_PERFORMANCE("test_invalid_event_parameters");

    printf("\nAroma Event System: %d passed, %d failed\n\n",
           tests_passed, tests_failed);

    if (passed) *passed = tests_passed;
    if (failed) *failed = tests_failed;
}
