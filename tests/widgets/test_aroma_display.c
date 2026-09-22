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

#include "test_aroma_display.h"
#include "widgets/aroma_gauge.h"
#include "widgets/aroma_icon.h"
#include "widgets/aroma_iconbutton.h"
#include "widgets/aroma_divider.h"
#include "widgets/aroma_loading.h"
#include "widgets/aroma_table.h"
#include "widgets/aroma_gif.h"
#include "widgets/aroma_image.h"
#include "widgets/aroma_debug_overlay.h"
#include "widgets/aroma_3d_viewer.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;
static int s_icon_clicks = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static void on_icon_click(void *user_data)
{
    (void)user_data;
    s_icon_clicks++;
}

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_gauge(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *g = aroma_ui_gauge(root, 10, 10, 120, 120);
    CHECK(g != NULL, "gauge created");
    if (g)
    {
        aroma_gauge_set_value(g, 0.75f);
        aroma_gauge_set_range(g, 0.0f, 100.0f);
        aroma_gauge_set_value(g, 42.0f);
        aroma_gauge_set_colors(g, 0xFF000000, 0xFFFFFFFF);
        aroma_gauge_set_angles(g, 135.0f, 405.0f);
        aroma_gauge_set_thickness(g, 8, 10);
        aroma_gauge_set_needle(g, true, 0xFFFF0000, 3);
        aroma_gauge_set_needle(g, false, 0, 0);
        aroma_gauge_set_secondary_hand(g, true, 0xFF00FF00, 2, 0.7f);
        aroma_gauge_set_secondary_value(g, 30.0f);
        aroma_gauge_set_extra_hand(g, false, 0, 0, 0.0f);
        aroma_gauge_set_extra_value(g, 0.0f);
        CHECK(1, "gauge setters safe");
    }
    aroma_gauge_set_value(NULL, 1.0f); /* no crash */
    aroma_gauge_set_range(NULL, 0.0f, 1.0f);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_icon(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_icon_create(NULL, 0, 0, 24) == NULL,
          "icon rejects NULL parent");

    AromaNode *icon = aroma_icon_create(root, 10, 10, 24);
    CHECK(icon != NULL, "icon created");
    if (icon)
    {
        aroma_icon_set_text(icon, "home", NULL);
        aroma_icon_set_color(icon, 0xFFFF0000);
        aroma_icon_set_texture(icon, 0);
        aroma_icon_set_image(icon, "/nonexistent/icon.png");
        CHECK(1, "icon setters safe");
        /* Destroy exercises the fixed double-free path. */
        aroma_icon_destroy(icon);
    }
    aroma_icon_set_text(NULL, "x", NULL);
    aroma_icon_set_color(NULL, 0);
    aroma_icon_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_iconbutton(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *btn = aroma_iconbutton_create(root, "close", 10, 10, 40, 0);
    CHECK(btn != NULL, "iconbutton created");
    if (btn)
    {
        s_icon_clicks = 0;
        aroma_iconbutton_set_callback(btn, on_icon_click, NULL);
        aroma_iconbutton_set_colors(btn, 0xFF000000, 0xFFFFFFFF);
        aroma_iconbutton_set_icon(btn, "menu");
        aroma_iconbutton_set_font(btn, NULL);
        CHECK(1, "iconbutton setters safe");
        aroma_iconbutton_destroy(btn);
    }
    aroma_iconbutton_set_callback(NULL, NULL, NULL);
    aroma_iconbutton_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_divider_loading(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *div = aroma_divider_create(root, 0, 0, 200, 0);
    CHECK(div != NULL, "divider created");
    if (div)
    {
        aroma_divider_set_color(div, 0xFF888888);
        aroma_divider_set_thickness(div, 2);
        aroma_divider_destroy(div);
    }
    aroma_divider_set_color(NULL, 0);
    aroma_divider_destroy(NULL);

    AromaNode *spin = aroma_loading_create(root, 10, 10, 20, 4, 0xFF0000FF);
    CHECK(spin != NULL, "loading spinner created");

    aroma_divider_create(NULL, 0, 0, 10, 0);
    CHECK(1, "divider guards safe");

    __destroy_node(root);
    __node_system_destroy();
}

static void test_table(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_table_create(NULL, 0, 0, 300, 200, 3) == NULL,
          "table rejects NULL parent");
    CHECK(aroma_table_create(root, 0, 0, 300, 200, 0) == NULL,
          "table rejects zero columns");

    AromaNode *t = aroma_table_create(root, 0, 0, 300, 200, 3);
    CHECK(t != NULL, "table created");
    if (!t)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    aroma_table_set_col_width(t, 0, 100);
    aroma_table_set_header(t, 0, "Name");
    int r0 = aroma_table_add_row(t);
    int r1 = aroma_table_add_row(t);
    CHECK(r0 == 0 && r1 == 1, "table rows index sequentially");
    aroma_table_set_cell_text(t, 0, 0, "Ada");
    aroma_table_set_cell_text(t, 1, 2, "42");
    aroma_table_set_cell_text(t, 99, 99, "oob"); /* guarded */
    aroma_table_set_cell_text(NULL, 0, 0, "x");
    CHECK(aroma_table_get_selected_row(t) < 0 ||
              aroma_table_get_selected_row(t) >= 0,
          "table selection query safe");
    aroma_table_set_callback(t, NULL, NULL);
    aroma_table_set_font(t, NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_gif_rejections(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_gif_create(NULL, "/nope.gif", 0, 0, 32, 32) == NULL,
          "gif rejects NULL parent");
    CHECK(aroma_gif_create(root, NULL, 0, 0, 32, 32) == NULL,
          "gif rejects NULL path");
    CHECK(aroma_gif_create(root, "/nonexistent/missing.gif", 0, 0, 32, 32) == NULL,
          "gif rejects missing file");
    unsigned char garbage[16] = {0};
    CHECK(aroma_gif_create_from_memory(root, garbage, sizeof(garbage),
                                       0, 0, 32, 32) == NULL,
          "gif rejects garbage bytes");
    CHECK(aroma_gif_create_from_memory(root, NULL, 0, 0, 0, 32, 32) == NULL,
          "gif rejects NULL bytes");

    __destroy_node(root);
    __node_system_destroy();
}

static void test_image_no_load(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_image_create(NULL, NULL, 0, 0, 64, 64) == NULL,
          "image rejects NULL parent");

    /* NULL path: node created, no texture load attempted (headless-safe). */
    AromaNode *img = aroma_image_create(root, NULL, 10, 10, 64, 64);
    CHECK(img != NULL, "image created without path");
    if (img)
        aroma_image_destroy(img);
    aroma_image_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_debug_overlay(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_debug_overlay_create(NULL, 0, 0, 300) == NULL,
          "debug overlay rejects NULL parent");

    AromaNode *ov = aroma_debug_overlay_create(root, 0, 0, 300);
    CHECK(ov != NULL, "debug overlay created");
    if (ov)
        aroma_debug_overlay_destroy(ov);
    aroma_debug_overlay_destroy(NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_3d_viewer_create(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_3d_viewer_create(NULL, 0, 0, 200, 200) == NULL,
          "3d viewer rejects NULL parent");

    AromaNode *v = aroma_3d_viewer_create(root, 0, 0, 200, 200);
    CHECK(v != NULL, "3d viewer created headless");

    __destroy_node(root);
    __node_system_destroy();
}

void run_display_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Display Tests ===\n");
    test_gauge();
    test_icon();
    test_iconbutton();
    test_divider_loading();
    test_table();
    test_gif_rejections();
    test_image_no_load();
    test_debug_overlay();
    test_3d_viewer_create();
    printf("Aroma Display: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
