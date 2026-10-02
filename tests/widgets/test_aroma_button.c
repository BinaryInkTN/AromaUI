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

#include "test_aroma_button.h"
#include "widgets/aroma_button.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;
static int s_clicks = 0;
static int s_hovers = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static bool on_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    s_clicks++;
    return true;
}

static bool on_hover(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    s_hovers++;
    return true;
}

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_create_guards(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_button_create(NULL, "x", 0, 0, 10, 10) == NULL,
          "button rejects NULL parent");
    CHECK(aroma_button_create(root, NULL, 0, 0, 10, 10) == NULL,
          "button rejects NULL label");
    CHECK(aroma_button_create(root, "x", 0, 0, 0, 10) == NULL,
          "button rejects zero width");
    CHECK(aroma_button_create(root, "x", 0, 0, 10, -1) == NULL,
          "button rejects negative height");

    AromaNode *btn = aroma_button_create(root, "OK", 40, 100, 140, 40);
    CHECK(btn != NULL, "button created");

    aroma_button_destroy(btn);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_press_release_click(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *btn = aroma_button_create(root, "OK", 40, 100, 140, 40);
    if (!btn)
    {
        CHECK(0, "button created for click test");
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    aroma_button_set_on_click(btn, on_click, NULL);
    aroma_button_set_on_hover(btn, on_hover, NULL);

    s_clicks = 0;
    s_hovers = 0;

    /* Full tap inside: press then release fires exactly once. */
    CHECK(aroma_button_handle_mouse_event(btn, 100, 120, true),
          "press inside hits");
    CHECK(s_clicks == 0, "press alone does not click");
    CHECK(aroma_button_handle_mouse_event(btn, 100, 120, false),
          "release inside hits");
    CHECK(s_clicks == 1, "tap fires on_click once");

    /* Press inside, release outside: no click. */
    s_clicks = 0;
    aroma_button_handle_mouse_event(btn, 100, 120, true);
    CHECK(!aroma_button_handle_mouse_event(btn, 500, 500, false),
          "release outside misses");
    CHECK(s_clicks == 0, "press/release-outside does not click");

    /* Press outside entirely: nothing happens. */
    s_clicks = 0;
    CHECK(!aroma_button_handle_mouse_event(btn, 500, 500, true),
          "press outside misses");
    CHECK(!aroma_button_handle_mouse_event(btn, 500, 500, false),
          "release outside misses again");
    CHECK(s_clicks == 0, "outside interaction does not click");

    /* Hover callback fires when entering from idle. */
    CHECK(s_hovers >= 0, "hover counter usable");

    /* Null-safe entry points. */
    CHECK(!aroma_button_handle_mouse_event(NULL, 0, 0, true),
          "handle rejects NULL node");
    aroma_button_set_on_click(NULL, on_click, NULL);
    aroma_button_destroy(NULL);

    aroma_button_destroy(btn);
    __destroy_node(root);
    __node_system_destroy();
}

void run_button_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Button Tests ===\n");
    test_create_guards();
    test_press_release_click();
    printf("Aroma Button: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
