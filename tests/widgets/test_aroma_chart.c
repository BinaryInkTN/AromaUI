/* Chart widget: creation, markup mounting, and the pbPlots raster pipeline.
 *
 * The raster test installs a stub graphics interface that captures whatever
 * the widget uploads, which is the only way to check the two things that are
 * easy to get wrong: that pbPlots' white canvas and black chrome really are
 * keyed out to alpha 0, and that the pixels come out in GL's byte order. */

#include "test_aroma_chart.h"

#include "widgets/aroma_chart.h"
#include "widgets/aroma_container.h"
#include "aroma_incense_loader.h"
#include "aroma_font.h"
#include "aroma_style.h"
#include "aroma_ubuntu_font.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ------------------------------------------------------------------ stub */

static unsigned char *s_cap;
static int s_cap_w;
static int s_cap_h;
static int s_load_calls;
static int s_update_calls;
static int s_unload_calls;
static unsigned int s_handle_seq;

/* Text the widget asked to have drawn, so the test can check that tick labels
   reach the renderer verbatim. */
static char s_rendered[16][64];
static int s_rendered_count;

static float stub_measure_text(size_t window_id, AromaFont *font, const char *text, float scale)
{
    (void)window_id;
    (void)font;
    (void)scale;
    return text ? (float)strlen(text) * 7.0f : 0.0f;
}

static void stub_render_text(size_t window_id, AromaFont *font, const char *text,
                             int x, int y, uint32_t color, float scale)
{
    (void)window_id;
    (void)font;
    (void)x;
    (void)y;
    (void)color;
    (void)scale;
    if (!text || s_rendered_count >= (int)(sizeof(s_rendered) / sizeof(s_rendered[0])))
        return;
    snprintf(s_rendered[s_rendered_count], sizeof(s_rendered[0]), "%s", text);
    s_rendered_count++;
}

static unsigned int stub_load_image_from_rgba(unsigned char *data, int width, int height)
{
    s_load_calls++;
    free(s_cap);
    s_cap = NULL;
    if (!data || width <= 0 || height <= 0)
        return 0;
    size_t bytes = (size_t)width * (size_t)height * 4u;
    s_cap = (unsigned char *)malloc(bytes);
    if (!s_cap)
        return 0;
    memcpy(s_cap, data, bytes);
    s_cap_w = width;
    s_cap_h = height;
    return ++s_handle_seq;
}

static bool stub_update_image_rgba(unsigned int texture_id, unsigned char *data, int width, int height)
{
    (void)texture_id;
    s_update_calls++;
    if (!data || width != s_cap_w || height != s_cap_h || !s_cap)
        return false;
    memcpy(s_cap, data, (size_t)width * (size_t)height * 4u);
    return true;
}

static void stub_unload_image(unsigned int texture_id)
{
    (void)texture_id;
    s_unload_calls++;
}

static AromaGraphicsInterface s_stub_gfx;
static AromaGraphicsInterface *(*s_real_get_gfx)(void);

static AromaGraphicsInterface *stub_get_graphics_interface(void)
{
    return &s_stub_gfx;
}

/* Simulates an environment with no graphics backend at all: the getter exists
   but hands back nothing, which is what a headless host looks like. */
static AromaGraphicsInterface *stub_get_no_graphics_interface(void)
{
    return NULL;
}

static void install_stub(void)
{
    memset(&s_stub_gfx, 0, sizeof(s_stub_gfx));
    s_stub_gfx.load_image_from_rgba = stub_load_image_from_rgba;
    s_stub_gfx.update_image_rgba = stub_update_image_rgba;
    s_stub_gfx.unload_image = stub_unload_image;
    s_stub_gfx.measure_text = stub_measure_text;
    s_stub_gfx.render_text = stub_render_text;

    free(s_cap);
    s_cap = NULL;
    s_cap_w = s_cap_h = 0;
    s_load_calls = s_update_calls = s_unload_calls = 0;
    s_rendered_count = 0;

    s_real_get_gfx = aroma_backend_abi.get_graphics_interface;
    aroma_backend_abi.get_graphics_interface = stub_get_graphics_interface;
}

static void remove_stub(void)
{
    aroma_backend_abi.get_graphics_interface = s_real_get_gfx;
    free(s_cap);
    s_cap = NULL;
}

/* -------------------------------------------------------------- utilities */

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void draw_now(AromaNode *node)
{
    AromaNodeDrawFn cb = aroma_node_get_draw_cb(node);
    if (cb)
        cb(node, 0);
}

/* Counts pixels whose bytes are [r,g,b,a] — the order GL_RGBA wants. */
static int count_rgba(const unsigned char *rgba, size_t pixels,
                      unsigned r, unsigned g, unsigned b, unsigned a)
{
    int n = 0;
    for (size_t i = 0; i < pixels; i++)
    {
        const unsigned char *p = rgba + i * 4u;
        if (p[0] == (unsigned char)r && p[1] == (unsigned char)g &&
            p[2] == (unsigned char)b && p[3] == (unsigned char)a)
            n++;
    }
    return n;
}

static int count_alpha(const unsigned char *rgba, size_t pixels, unsigned a)
{
    int n = 0;
    for (size_t i = 0; i < pixels; i++)
        if (rgba[i * 4u + 3u] == (unsigned char)a)
            n++;
    return n;
}

/* ------------------------------------------------------------------ tests */

static void test_chart_create(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_ui_chart(NULL, 0, 0, 100, 60) == NULL, "chart rejects NULL parent");

    AromaNode *chart = aroma_ui_chart(root, 5, 6, 100, 60);
    CHECK(chart != NULL, "chart created");
    if (chart)
    {
        AromaRect *rect = (AromaRect *)chart->node_widget_ptr;
        CHECK(rect && rect->x == 5 && rect->y == 6 && rect->width == 100 && rect->height == 60,
              "chart keeps its geometry");

        /* Empty/short inputs must be no-ops rather than crashes. */
        aroma_chart_set_series_csv(chart, NULL, "");
        aroma_chart_set_series_csv(NULL, NULL, "1,2,3");
        aroma_chart_set_series_csv(chart, NULL, "1,2,3");
        aroma_chart_set_series_csv(chart, "1,2", "1,2,3"); /* mismatched x: indices win */
        aroma_chart_set_range(chart, false, 0.0, 10.0, false, -1.0, 1.0);
        aroma_chart_set_tick_labels(chart, "0,5,10", "-1,0,1");
        aroma_chart_set_padding(chart, 20, 4, 6, 18);
        aroma_chart_set_line_thickness(chart, 3.0f);
        aroma_chart_set_line_color(chart, 0xFF00FF00);
        aroma_chart_set_grid_color(chart, 0x22FFFFFF);
        aroma_chart_set_frame_color(chart, 0x66FFFFFF);
        aroma_chart_set_text_color(chart, 0x99FFFFFF);
        aroma_chart_set_furniture(chart, false, true);

        /* No graphics backend installed: drawing must bail out quietly. */
        AromaGraphicsInterface *(*saved)(void) = aroma_backend_abi.get_graphics_interface;
        aroma_backend_abi.get_graphics_interface = stub_get_no_graphics_interface;
        draw_now(chart);
        aroma_backend_abi.get_graphics_interface = saved;
        CHECK(1, "chart draw survives a missing graphics interface");

        __destroy_node(chart);
    }

    __destroy_node(root);
    __node_system_destroy();
}

static void test_chart_markup(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *parent = aroma_container_create(root, 0, 0, 800, 600);
    CHECK(parent != NULL, "markup parent created");
    if (!parent)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    uint64_t before = parent->child_count;
    IncenseRegistry *reg = NULL;

    static const char *src =
        "Window {\n"
        "    width: 800\n"
        "    height: 600\n"
        "    Chart { id: \"energy\" x: 12 y: 8 width: 300 height: 120\n"
        "        data: \"3,5,4,7,6,9\"\n"
        "        x_labels: \"0,1,2,3,4,5\"\n"
        "        y_labels: \"0,5,10\"\n"
        "        y_min: 0\n"
        "        y_max: 10\n"
        "        line_thickness: 2\n"
        "        line_color: #FF8AB4F8\n"
        "        grid_color: #33FFFFFF\n"
        "        frame_color: #40FFFFFF\n"
        "        text_color: #99FFFFFF\n"
        "        pad_left: 26\n"
        "        pad_top: 6\n"
        "        pad_right: 8\n"
        "        pad_bottom: 18\n"
        "        grid: 1\n"
        "        frame: 1\n"
        "        size: 12\n"
        "    }\n"
        "}\n";

    bool ok = IncenseLoadStringIntoParent(src, parent, NULL, NULL, &reg);
    CHECK(ok, "chart markup mounts");
    CHECK(parent->child_count > before, "chart markup adds nodes");
    if (reg)
        IncenseFreeRegistry(reg);

    int nerrs = 0;
    const IncenseError *errs = IncenseGetErrors(&nerrs);
    for (int i = 0; i < nerrs; i++)
        printf("[chart markup error %d] type=%d line=%d: %s %s\n", i, (int)errs[i].type,
               errs[i].line, errs[i].message, errs[i].suggestion);
    CHECK(nerrs == 0, "chart markup loads without errors");

    __destroy_node(root); /* parent is a child of root */
    __node_system_destroy();
}

/* Pixels in the captured raster that carry `color` at full opacity. */
static int count_theme_color(uint32_t color)
{
    return count_rgba(s_cap, (size_t)s_cap_w * (size_t)s_cap_h,
                      (color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu, 255);
}

/* Colours nobody set must track the theme — including after it changes, which
 * is what a chart built before the theme is pinned has to survive. */
static void test_chart_theme_colors(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    install_stub();

    AromaNode *chart = aroma_ui_chart(root, 0, 0, 200, 100);
    CHECK(chart != NULL, "theme chart created");
    if (chart)
    {
        aroma_chart_set_series_csv(chart, NULL, "0,1,2,3,4");
        aroma_chart_set_range(chart, true, 0.0, 1.0, false, 0.0, 4.0);

        AromaTheme saved = aroma_theme_get_global();
        AromaTheme blue = aroma_theme_create_material_blue_dark();
        aroma_theme_set_global(&blue);
        draw_now(chart);
        CHECK(s_cap != NULL, "theme chart rasterised");
        CHECK(count_theme_color(blue.colors.primary) > 0,
              "an unset line colour follows the theme");

        AromaTheme teal = aroma_theme_create_material_teal_dark();
        aroma_theme_set_global(&teal);
        draw_now(chart);
        CHECK(count_theme_color(teal.colors.primary) > 0,
              "a theme switch repaints the chart");
        CHECK(count_theme_color(blue.colors.primary) == 0,
              "the previous stroke colour is gone");

        aroma_chart_set_line_color(chart, 0xFFFF0000);
        draw_now(chart);
        CHECK(count_theme_color(0xFFFF0000) > 0, "an explicit line colour wins");
        CHECK(count_theme_color(teal.colors.primary) == 0,
              "the theme colour steps aside once a colour is set");

        aroma_theme_set_global(&saved);
        __destroy_node(chart);
    }

    remove_stub();
    __destroy_node(root);
    __node_system_destroy();
}

static void test_chart_raster(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    install_stub();

    AromaNode *chart = aroma_ui_chart(root, 10, 20, 300, 160);
    CHECK(chart != NULL, "raster chart created");
    if (chart)
    {
        aroma_chart_set_series_csv(chart, NULL, "0,2,4,6,8,10");
        aroma_chart_set_range(chart, true, 0.0, 1.0, false, 0.0, 10.0);
        aroma_chart_set_line_color(chart, 0xFF3366CC);
        aroma_chart_set_font(chart, NULL); /* no tick text in this test */

        float k = aroma_font_raster_scale();
        int expect_w = (int)lround((double)(300 - AROMA_CHART_PAD_LEFT - AROMA_CHART_PAD_RIGHT) * (double)k);
        int expect_h = (int)lround((double)(160 - AROMA_CHART_PAD_TOP - AROMA_CHART_PAD_BOTTOM) * (double)k);

        draw_now(chart);

        CHECK(s_load_calls == 1, "first draw uploads a raster once");
        CHECK(s_cap != NULL && s_cap_w == expect_w && s_cap_h == expect_h,
              "raster covers exactly the plot area at device density");

        if (s_cap && expect_w > 0 && expect_h > 0)
        {
            size_t pixels = (size_t)s_cap_w * (size_t)s_cap_h;
            int keyed = count_alpha(s_cap, pixels, 0); /* background + chrome */
            int keyed_black = count_rgba(s_cap, pixels, 0, 0, 0, 0);
            int white = count_rgba(s_cap, pixels, 255, 255, 255, 255);
            int black = count_rgba(s_cap, pixels, 0, 0, 0, 255);
            int stroke = count_rgba(s_cap, pixels, 0x33, 0x66, 0xCC, 0xFF);

            CHECK(keyed * 10 > (int)pixels * 8, "most of the raster is keyed out");
            CHECK(keyed_black == keyed,
                  "keyed pixels are transparent black, so filtering cannot bleed white");
            CHECK(white == 0, "pbPlots' white canvas never survives");
            CHECK(black == 0, "pbPlots' black chrome never survives");
            CHECK(stroke > 0, "the stroke lands in its own colour");
        }

        /* A data change re-renders in place (same size) instead of reloading. */
        int loads_before = s_load_calls;
        aroma_chart_set_series_csv(chart, NULL, "10,8,6,4,2,0");
        draw_now(chart);
        CHECK(s_update_calls >= 1, "data change updates the existing texture");
        CHECK(s_load_calls == loads_before, "data change does not reallocate the texture");

        /* Redrawing with nothing changed must not touch the GPU at all. */
        int updates_before = s_update_calls;
        draw_now(chart);
        CHECK(s_update_calls == updates_before, "unchanged redraw reuses the cached raster");

        /* Tick labels are free text: tokenising must keep "6a" whole instead of
           stopping at the first non-digit, and axis text must be asked for at
           the design-space positions the widget computed. */
        AromaFont *font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                        aroma_ubuntu_ttf_len, 12);
        CHECK(font != NULL, "axis font created");
        if (font)
        {
            s_rendered_count = 0;
            aroma_chart_set_tick_labels(chart, "6a,9a,12p", "0,10");
            aroma_chart_set_font(chart, font);
            draw_now(chart);

            int saw_6a = 0, saw_12p = 0, saw_10 = 0, saw_truncated = 0;
            for (int i = 0; i < s_rendered_count; i++)
            {
                if (strcmp(s_rendered[i], "6a") == 0) saw_6a = 1;
                if (strcmp(s_rendered[i], "12p") == 0) saw_12p = 1;
                if (strcmp(s_rendered[i], "10") == 0) saw_10 = 1;
                if (strcmp(s_rendered[i], "6") == 0 || strcmp(s_rendered[i], "12") == 0)
                    saw_truncated = 1;
            }
            CHECK(s_rendered_count == 5, "every tick label is drawn");
            CHECK(saw_6a && saw_12p, "text tick labels survive tokenising");
            CHECK(saw_10, "numeric tick labels still render");
            CHECK(!saw_truncated, "tick labels are not cut off at the first non-digit");
        }

        __destroy_node(chart);
        CHECK(s_unload_calls == 1, "destroy releases the texture");
        if (font)
            aroma_font_destroy(font);
    }

    remove_stub();
    __destroy_node(root);
    __node_system_destroy();
}

void run_chart_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Chart Tests ===\n");
    test_chart_create();
    test_chart_markup();
    test_chart_theme_colors();
    test_chart_raster();
    printf("Aroma Chart: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
