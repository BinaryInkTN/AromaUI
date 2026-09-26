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

#include "test_aroma_forms.h"
#include "widgets/aroma_textbox.h"
#include "widgets/aroma_dropdown.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#include <string.h>

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

static void test_textbox(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_textbox_create(NULL, 0, 0, 200, 40) == NULL,
          "textbox rejects NULL parent");

    AromaNode *tb = aroma_textbox_create(root, 10, 10, 200, 40);
    CHECK(tb != NULL, "textbox created");
    if (!tb)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(strcmp(aroma_textbox_get_text(tb), "") == 0,
          "textbox starts empty");
    CHECK(strcmp(aroma_textbox_get_text(NULL), "") == 0,
          "textbox get NULL-safe");

    aroma_textbox_set_text(tb, "Hello");
    CHECK(strcmp(aroma_textbox_get_text(tb), "Hello") == 0,
          "textbox text roundtrips");
    aroma_textbox_set_text(tb, NULL); /* ignored */
    CHECK(strcmp(aroma_textbox_get_text(tb), "Hello") == 0,
          "textbox NULL set keeps text");
    aroma_textbox_set_text(NULL, "x"); /* no crash */

    aroma_textbox_set_placeholder(tb, "Type here");
    aroma_textbox_set_placeholder(tb, NULL);
    aroma_textbox_set_focused(tb, true);
    aroma_textbox_set_focused(tb, false);
    aroma_textbox_set_focused(NULL, true);
    aroma_textbox_set_font(tb, NULL);
    aroma_textbox_set_on_text_changed(tb, NULL, NULL);
    aroma_textbox_set_on_focus_changed(tb, NULL, NULL);

    aroma_textbox_destroy(NULL);
    aroma_textbox_destroy(tb);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_dropdown(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_dropdown_create(NULL, 0, 0, 200, 40) == NULL,
          "dropdown rejects NULL parent");

    AromaNode *dd = aroma_dropdown_create(root, 10, 10, 200, 40);
    CHECK(dd != NULL, "dropdown created");
    if (!dd)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    aroma_dropdown_add_option(dd, "Red");
    aroma_dropdown_add_option(dd, "Green");
    aroma_dropdown_add_option(dd, "Blue");
    aroma_dropdown_add_option(dd, NULL); /* guarded */
    aroma_dropdown_add_option(NULL, "x");
    CHECK(1, "dropdown options added safely");

    aroma_dropdown_set_on_change(dd, NULL, NULL);
    aroma_dropdown_set_font(dd, NULL);
    aroma_dropdown_set_text_color(dd, 0xFF000000);

    AromaDropdown *priv = (AromaDropdown *)dd->node_widget_ptr;
    CHECK(priv != NULL, "dropdown exposes state");
    CHECK(priv->max_visible_rows == AROMA_DROPDOWN_DEFAULT_MAX_ROWS &&
              priv->scroll_offset == 0,
          "list capped at 6 rows by default, unscrolled");
    aroma_dropdown_set_max_visible_rows(dd, 2);
    CHECK(priv->max_visible_rows == 2, "row cap configurable");
    aroma_dropdown_set_max_visible_rows(dd, 0);
    CHECK(priv->max_visible_rows == 0, "row cap removable");
    aroma_dropdown_set_max_visible_rows(NULL, 3); /* guarded */

    /* Destroy unregisters the overlay: later hit tests must not touch it. */
    aroma_dropdown_destroy(dd);
    aroma_dropdown_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

void run_forms_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Forms Tests ===\n");
    test_textbox();
    test_dropdown();
    printf("Aroma Forms: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
