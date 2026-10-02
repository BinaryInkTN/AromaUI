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

#include "test_aroma_selection.h"
#include "widgets/aroma_tabs.h"
#include "widgets/aroma_sidebar.h"
#include "widgets/aroma_listview.h"
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

static void test_tabs(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    const char *labels[] = {"Home", "Music", "Maps"};

    CHECK(aroma_tabs_create(NULL, 0, 0, 300, 60, labels, 3) == NULL,
          "tabs reject NULL parent");

    AromaNode *tabs = aroma_tabs_create(root, 0, 0, 300, 60, labels, 3);
    CHECK(tabs != NULL, "tabs created");
    if (!tabs)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_tabs_get_selected(tabs) == 0, "tabs start on first tab");
    CHECK(aroma_tabs_get_selected(NULL) == -1, "tabs get NULL returns -1");

    aroma_tabs_set_selected(tabs, 2);
    CHECK(aroma_tabs_get_selected(tabs) == 2, "tabs select third");
    aroma_tabs_set_selected(tabs, 2); /* idempotent */
    CHECK(aroma_tabs_get_selected(tabs) == 2, "tabs reselect stable");
    aroma_tabs_set_selected(tabs, 99);
    CHECK(aroma_tabs_get_selected(tabs) == 2, "tabs ignore overflow index");
    aroma_tabs_set_selected(tabs, -1);
    CHECK(aroma_tabs_get_selected(tabs) == 2, "tabs ignore negative index");
    aroma_tabs_set_selected(NULL, 1); /* no crash */
    aroma_tabs_set_on_change(tabs, NULL, NULL);
    aroma_tabs_set_transition(tabs, 0, 300);
    aroma_tabs_set_font(tabs, NULL);

    aroma_tabs_destroy(NULL);
    aroma_tabs_destroy(tabs);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_sidebar(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    const char *labels[] = {"Nav", "Phone"};

    AromaNode *bar = aroma_sidebar_create(root, 0, 0, 80, 600, labels, 2);
    CHECK(bar != NULL, "sidebar created");
    if (!bar)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_sidebar_get_selected(bar) == 0, "sidebar starts first");
    aroma_sidebar_set_selected(bar, 1);
    CHECK(aroma_sidebar_get_selected(bar) == 1, "sidebar selects second");
    aroma_sidebar_set_selected(bar, 7);
    CHECK(aroma_sidebar_get_selected(bar) == 1, "sidebar ignores overflow");
    aroma_sidebar_set_selected(NULL, 0); /* no crash */
    CHECK(aroma_sidebar_get_selected(NULL) == -1, "sidebar get NULL returns -1");
    aroma_sidebar_set_on_select(bar, NULL, NULL);
    aroma_sidebar_set_retracted(bar, true);
    aroma_sidebar_set_retracted(bar, false);

    aroma_sidebar_destroy(NULL);
    aroma_sidebar_destroy(bar);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_listview(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_listview_create(NULL, 0, 0, 200, 300) == NULL,
          "listview rejects NULL parent");

    AromaNode *list = aroma_listview_create(root, 0, 0, 200, 300);
    CHECK(list != NULL, "listview created");
    if (!list)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_listview_get_count(list) == 0, "listview starts empty");

    int marker_a = 1, marker_b = 2;
    aroma_listview_add_item(list, "One", "1st", &marker_a);
    aroma_listview_add_item_with_icon(list, "Two", "2nd", "icon", &marker_b);
    aroma_listview_add_header(list, "Group");
    aroma_listview_add_item(list, "Three", NULL, NULL);
    aroma_listview_add_separator(list);
    CHECK(aroma_listview_get_count(list) == 5, "listview counts all rows");
    CHECK(aroma_listview_get_item_data(list, 0) == &marker_a,
          "listview item data roundtrips");
    CHECK(aroma_listview_get_item_data(list, 1) == &marker_b,
          "listview icon item data roundtrips");
    CHECK(aroma_listview_get_item_data(list, 99) == NULL,
          "listview out-of-range data NULL");

    aroma_listview_add_item(NULL, "x", NULL, NULL); /* no crash */
    aroma_listview_add_item(list, NULL, NULL, NULL); /* guarded */
    CHECK(aroma_listview_get_count(list) == 5, "NULL item not added");

    aroma_listview_clear(list);
    CHECK(aroma_listview_get_count(list) == 0, "listview clear empties");
    CHECK(aroma_listview_get_count(NULL) == 0, "count NULL-safe");
    aroma_listview_clear(NULL); /* no crash */
    aroma_listview_set_callback(list, NULL, NULL);

    aroma_listview_destroy(NULL);
    aroma_listview_destroy(list);
    __destroy_node(root);
    __node_system_destroy();
}

void run_selection_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Selection Tests ===\n");
    test_tabs();
    test_sidebar();
    test_listview();
    printf("Aroma Selection: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
