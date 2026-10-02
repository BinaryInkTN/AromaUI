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

#include "test_aroma_slider.h"
#include "widgets/aroma_slider.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;
static int s_changes = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static bool on_change(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    s_changes++;
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

    CHECK(aroma_slider_create(NULL, 0, 0, 200, 40, 0, 100, 50) == NULL,
          "slider rejects NULL parent");

    AromaNode *s = aroma_slider_create(root, 10, 10, 200, 40, 0, 100, 50);
    CHECK(s != NULL, "slider created");
    if (s)
        CHECK(aroma_slider_get_value(s) == 50, "slider initial value kept");
    CHECK(aroma_slider_get_value(NULL) == 0, "get_value NULL-safe");

    aroma_slider_destroy(NULL);
    aroma_slider_destroy(s);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_value_clamping(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *s = aroma_slider_create(root, 0, 0, 200, 40, 10, 90, 50);
    if (!s)
    {
        CHECK(0, "slider created for clamp test");
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    aroma_slider_set_on_change(s, on_change, NULL);
    s_changes = 0;

    aroma_slider_set_value(s, 70);
    CHECK(aroma_slider_get_value(s) == 70, "slider sets value");
    CHECK(s_changes == 1, "on_change fires on real change");

    aroma_slider_set_value(s, 70);
    CHECK(s_changes == 1, "on_change skips no-op set");

    aroma_slider_set_value(s, 500);
    CHECK(aroma_slider_get_value(s) == 90, "slider clamps to max");
    aroma_slider_set_value(s, -500);
    CHECK(aroma_slider_get_value(s) == 10, "slider clamps to min");
    aroma_slider_set_value(NULL, 42); /* no crash */
    aroma_slider_set_on_change(NULL, on_change, NULL);
    aroma_slider_set_on_change(s, NULL, NULL);

    aroma_slider_destroy(s);
    __destroy_node(root);
    __node_system_destroy();
}

void run_slider_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Slider Tests ===\n");
    test_create_guards();
    test_value_clamping();
    printf("Aroma Slider: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
