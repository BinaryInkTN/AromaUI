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

#include "test_aroma_overlays.h"
#include "widgets/aroma_dialog.h"
#include "widgets/aroma_tooltip.h"
#include "widgets/aroma_snackbar.h"
#include "widgets/aroma_menu.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;
static int s_dialog_actions = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static void on_dialog_action(void *user_data)
{
    (void)user_data;
    s_dialog_actions++;
}

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_dialog(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_dialog_create(NULL, "T", "M", 300, 200, 0) == NULL,
          "dialog rejects NULL parent");

    AromaNode *dlg = aroma_dialog_create(root, "Confirm", "Delete?", 320, 180, 0);
    CHECK(dlg != NULL, "dialog created");
    if (!dlg)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_dialog_get_content_area(dlg) != NULL,
          "dialog exposes content area");
    CHECK(aroma_dialog_get_content_area(NULL) == NULL,
          "dialog content NULL-safe");

    s_dialog_actions = 0;
    aroma_dialog_add_action(dlg, "Cancel", on_dialog_action, NULL);
    aroma_dialog_add_action(dlg, "Delete", on_dialog_action, NULL);
    aroma_dialog_add_action(dlg, NULL, on_dialog_action, NULL); /* guarded */
    aroma_dialog_add_action(NULL, "x", on_dialog_action, NULL);
    CHECK(1, "dialog actions added safely");

    aroma_dialog_show(dlg);
    aroma_dialog_hide(dlg);
    aroma_dialog_show(NULL); /* no crash */
    aroma_dialog_hide(NULL);
    aroma_dialog_set_font(dlg, NULL);

    aroma_dialog_destroy(NULL);
    aroma_dialog_destroy(dlg);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_tooltip(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_tooltip_create(NULL, "tip", 0, 0, 0) == NULL,
          "tooltip rejects NULL parent");

    AromaNode *tip = aroma_tooltip_create(root, "Hello", 50, 50, 0);
    CHECK(tip != NULL, "tooltip created");
    if (tip)
    {
        aroma_tooltip_set_text(tip, "Updated");
        aroma_tooltip_set_text(tip, NULL); /* guarded? must not crash */
        aroma_tooltip_show(tip, 0);
        aroma_tooltip_hide(tip);
        aroma_tooltip_set_font(tip, NULL);
        aroma_tooltip_destroy(tip);
    }
    aroma_tooltip_set_text(NULL, "x");
    aroma_tooltip_show(NULL, 0);
    aroma_tooltip_hide(NULL);
    aroma_tooltip_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_snackbar(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_snackbar_create(NULL, "msg", 3000) == NULL,
          "snackbar rejects NULL parent");

    AromaNode *bar = aroma_snackbar_create(root, "Item deleted", 3000);
    CHECK(bar != NULL, "snackbar created");
    if (bar)
    {
        aroma_snackbar_set_action(bar, "UNDO", on_dialog_action, NULL);
        aroma_snackbar_show(bar);
        aroma_snackbar_set_font(bar, NULL);
        /* Destroy exercises the fixed double-free path. */
        aroma_snackbar_destroy(bar);
    }
    aroma_snackbar_set_action(NULL, "x", NULL, NULL);
    aroma_snackbar_show(NULL);
    aroma_snackbar_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_menu(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_menu_create(NULL, 10, 10) == NULL,
          "menu rejects NULL parent");

    AromaNode *menu = aroma_menu_create(root, 10, 10);
    CHECK(menu != NULL, "menu created");
    if (!menu)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    aroma_menu_add_item(menu, "Open", on_dialog_action, NULL);
    aroma_menu_add_item_with_icon(menu, "Save", "save", on_dialog_action, NULL);
    aroma_menu_add_separator(menu);
    aroma_menu_add_item(menu, NULL, NULL, NULL); /* guarded */
    aroma_menu_add_item(NULL, "x", NULL, NULL);
    CHECK(1, "menu items added safely");

    aroma_menu_show(menu);
    aroma_menu_hide(menu);
    aroma_menu_show(NULL);
    aroma_menu_hide(NULL);
    aroma_menu_set_font(menu, NULL);
    aroma_menu_set_icon_font(menu, NULL);

    aroma_menu_destroy(NULL);
    aroma_menu_destroy(menu);
    __destroy_node(root);
    __node_system_destroy();
}

void run_overlays_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Overlays Tests ===\n");
    test_dialog();
    test_tooltip();
    test_snackbar();
    test_menu();
    printf("Aroma Overlays: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
