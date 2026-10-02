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

#include "test_aroma_inputs.h"
#include "widgets/aroma_checkbox.h"
#include "widgets/aroma_switch.h"
#include "widgets/aroma_radiobutton.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_checkbox(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_checkbox_create(NULL, "x", 0, 0, 20, 20) == NULL,
          "checkbox rejects NULL parent");

    AromaNode *cb = aroma_checkbox_create(root, "Accept", 10, 10, 200, 32);
    CHECK(cb != NULL, "checkbox created");
    if (!cb)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(!aroma_checkbox_is_checked(cb), "checkbox starts unchecked");
    CHECK(!aroma_checkbox_is_checked(NULL), "is_checked NULL-safe");

    aroma_checkbox_set_checked(cb, true);
    CHECK(aroma_checkbox_is_checked(cb), "checkbox checks");
    aroma_checkbox_set_checked(cb, false);
    CHECK(!aroma_checkbox_is_checked(cb), "checkbox unchecks");
    aroma_checkbox_set_checked(NULL, true); /* no crash */
    aroma_checkbox_set_callback(cb, NULL, NULL);
    aroma_checkbox_set_callback(NULL, NULL, NULL);

    aroma_checkbox_destroy(NULL);
    aroma_checkbox_destroy(cb);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_switch(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_switch_create(NULL, 0, 0, 60, 32, false) == NULL,
          "switch rejects NULL parent");

    AromaNode *sw = aroma_switch_create(root, 10, 10, 60, 32, false);
    CHECK(sw != NULL, "switch created");
    if (!sw)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(!aroma_switch_get_state(sw), "switch starts off");
    CHECK(!aroma_switch_get_state(NULL), "get_state NULL-safe");

    aroma_switch_set_state(sw, true);
    CHECK(aroma_switch_get_state(sw), "switch turns on");
    aroma_switch_set_state(sw, true); /* idempotent */
    CHECK(aroma_switch_get_state(sw), "switch stays on");
    aroma_switch_set_state(sw, false);
    CHECK(!aroma_switch_get_state(sw), "switch turns off");
    aroma_switch_set_state(NULL, true); /* no crash */
    aroma_switch_set_on_change(sw, NULL, NULL);
    aroma_switch_set_on_change(NULL, NULL, NULL);

    AromaNode *sw_on = aroma_switch_create(root, 0, 0, 60, 32, true);
    CHECK(sw_on && aroma_switch_get_state(sw_on), "switch initial state on");

    aroma_switch_destroy(NULL);
    aroma_switch_destroy(sw);
    aroma_switch_destroy(sw_on);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_radiobutton(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_radiobutton_create(NULL, "x", 0, 0, 20, 20, 17) == NULL,
          "radio rejects NULL parent");

    /* Unique group id per run so global group state cannot leak. */
    AromaNode *a = aroma_radiobutton_create(root, "A", 10, 10, 200, 32, 17);
    AromaNode *b = aroma_radiobutton_create(root, "B", 10, 50, 200, 32, 17);
    CHECK(a && b, "radio pair created");
    if (!a || !b)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(!aroma_radiobutton_is_selected(a), "radio starts unselected");
    CHECK(!aroma_radiobutton_is_selected(NULL), "is_selected NULL-safe");

    aroma_radiobutton_set_selected(a, true);
    CHECK(aroma_radiobutton_is_selected(a), "radio selects");
    aroma_radiobutton_set_selected(b, true);
    CHECK(aroma_radiobutton_is_selected(b), "second radio selects");
    CHECK(!aroma_radiobutton_is_selected(a), "group deselects first");
    aroma_radiobutton_set_selected(b, false);
    CHECK(!aroma_radiobutton_is_selected(b), "radio deselects");
    aroma_radiobutton_set_selected(NULL, true); /* no crash */
    aroma_radiobutton_set_callback(a, NULL, NULL);
    aroma_radiobutton_set_callback(NULL, NULL, NULL);

    aroma_radiobutton_destroy(NULL);
    aroma_radiobutton_destroy(a);
    aroma_radiobutton_destroy(b);
    __destroy_node(root);
    __node_system_destroy();
}

void run_inputs_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Inputs Tests ===\n");
    test_checkbox();
    test_switch();
    test_radiobutton();
    printf("Aroma Inputs: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
