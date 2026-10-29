/* Incense live-preview sandbox (WebAssembly harness).
 *
 * Built with Emscripten into docs/incense_sandbox.{js,wasm} via
 * tools/sandbox/build_wasm.sh. docs/sandbox.html drives it:
 *   aroma_sandbox_reload(source)  mount Incense markup, report errors
 *   aroma_sandbox_has_error()     nonzero when the last reload failed
 *   aroma_sandbox_get_last_error() human-readable failure, "Line L: msg"
 *   aroma_sandbox_set_theme(dark) light -> Material Blue, dark -> Material Black
 *   aroma_sandbox_get_width/height() current canvas size in pixels
 *   aroma_sandbox_init()          re-run first-boot setup (idempotent)
 *
 * Curated docs demos reference callbacks (on_click: "back", ...) that have
 * no app behind them in a preview. They are registered here as no-ops so
 * every demo loads with status Ready; select_theme is wired for real so
 * the theming preview keeps working.
 */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aroma_ui.h"
#include "aroma_style.h"
#include "aroma_font.h"
#include "aroma_animation.h"
#include "aroma_ubuntu_font.h"
#include "aroma_material_font.h"
#include "aroma_incense_loader.h"
#include "widgets/aroma_chart.h"
#include "loud_dashboard.h"
#include "fitness_demo.h"
#include "travel_demo.h"


#define SANDBOX_WIN_W 320
#define SANDBOX_WIN_H 480
#define SANDBOX_FONT_PX 20

static AromaWindow *s_window;
static IncenseRegistry *s_registry;
static AromaFont *s_font;
static AromaFont *s_icon_font;
static bool s_dark;
static bool s_booted;
static int s_has_error;
static char s_last_error[1024];

/* Glyphs are rasterized per pixel size: scaling one face blurs it, so every
 * size the previews ask for with `size:` / `icon_size:` gets its own face.
 * Created once on boot and published to the Incense loader. */
static const int s_text_sizes[] = {7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 23, 24, 26, 28, 30, 32, 34, 38, 40, 44, 52};
static AromaFont *s_text_fonts[sizeof(s_text_sizes) / sizeof(s_text_sizes[0])];
static const int s_icon_sizes[] = {8, 11, 12, 13, 14, 16, 18, 20, 22, 24, 26, 32, 36, 40, 48, 64};
static AromaFont *s_icon_fonts[sizeof(s_icon_sizes) / sizeof(s_icon_sizes[0])];
static AromaFont *s_lucide_fonts[sizeof(s_icon_sizes) / sizeof(s_icon_sizes[0])];

static void create_sized_fonts(void)
{
    for (size_t i = 0; i < sizeof(s_text_sizes) / sizeof(s_text_sizes[0]); i++) {
        if (!s_text_fonts[i])
            s_text_fonts[i] = aroma_font_create_from_memory(
                aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, s_text_sizes[i]);
        IncenseRegisterFontSize(s_text_sizes[i], s_text_fonts[i]);
    }
    for (size_t i = 0; i < sizeof(s_icon_sizes) / sizeof(s_icon_sizes[0]); i++) {
        if (!s_icon_fonts[i])
            s_icon_fonts[i] = aroma_font_create_from_memory(
                icon_ttf, icon_ttf_len, s_icon_sizes[i]);
        IncenseRegisterIconFontSize(s_icon_sizes[i], s_icon_fonts[i]);

        /* Lucide is a second glyph pack for the line-style dashboard icons.
           The TTF is preloaded at /assets/lucide.ttf by build_wasm.sh. */
        if (!s_lucide_fonts[i])
            s_lucide_fonts[i] = aroma_font_create("assets/lucide.ttf", s_icon_sizes[i]);
        if (s_lucide_fonts[i])
            IncenseRegisterIconPackFontSize("lucide", s_icon_sizes[i], s_lucide_fonts[i]);
    }
    loud_dashboard_bind_fonts(s_text_fonts, s_text_sizes,
                              (int)(sizeof(s_text_sizes) / sizeof(s_text_sizes[0])));
}

static void set_error(const char *msg)
{
    s_has_error = 1;
    if (msg && msg[0]) {
        snprintf(s_last_error, sizeof(s_last_error), "%s", msg);
        s_last_error[sizeof(s_last_error) - 1] = '\0';
    } else {
        snprintf(s_last_error, sizeof(s_last_error), "Unknown error");
    }
}

static void apply_theme(void);

/* Applies a named theme (the same names the sandbox picker uses, e.g.
 * "Material Blue dark").  The embedded dashboard demo pins itself to its
 * own violet theme this way, and every later apply_theme() call
 * (including the docs page's light/dark sync) keeps re-applying the pin. */
static char s_pinned_theme[64];

/* Violet dark palette matching the demo's reference design.  Built from the
 * Material dark preset so every spacing/typography value stays consistent,
 * with only the colour roles replaced. */
static AromaTheme theme_aroma_violet_dark(void)
{
    AromaTheme t = aroma_theme_create_material_black();
    t.colors.primary        = 0xFF723CEAu;  /* dominant violet            */
    t.colors.primary_dark   = 0xFF54309Au;  /* dim violet (pressed)       */
    t.colors.primary_light  = 0xFF9261F1u;  /* light violet (hover)       */
    t.colors.secondary      = 0xFFD4FD63u;  /* chartreuse accent          */
    t.colors.background     = 0xFF000000u;  /* app background             */
    t.colors.surface        = 0xFF1A1A1Cu;  /* card surface               */
    t.colors.text_primary   = 0xFFFFFFFFu;
    t.colors.text_secondary = 0xFF9A93ADu;
    t.colors.border         = 0xFF332E45u;  /* violet-tinted separator    */
    t.colors.error          = 0xFFFF375Fu;
    return t;
}

/* Dark training shell with an orange primary for the Fitness dashboard. */
static AromaTheme theme_fitness(void)
{
    AromaTheme t = aroma_theme_create_material_black();
    t.colors.primary        = 0xFFF97316u;
    t.colors.primary_dark   = 0xFFEA580Cu;
    t.colors.primary_light  = 0xFFFDBA74u;
    t.colors.secondary      = 0xFF84CC16u;
    t.colors.background     = 0xFF0A0A0Au;
    t.colors.surface        = 0xFF1A1A1Au;
    t.colors.text_primary   = 0xFFFFFFFFu;
    t.colors.text_secondary = 0xFFA3A3A3u;
    t.colors.border         = 0xFF2A2A2Au;
    t.colors.error          = 0xFFEF4444u;
    t.typography.font_color = t.colors.text_primary;
    return t;
}

/* Car infotainment palette: black shell, graphite cards and iOS-style
 * green/blue accents. */
static AromaTheme theme_travel(void)
{
    AromaTheme t = aroma_theme_create_material_black();
    t.colors.primary        = 0xFF34C759u;
    t.colors.primary_dark   = 0xFF248A3Du;
    t.colors.primary_light  = 0xFF30D158u;
    t.colors.secondary      = 0xFF0A84FFu;
    t.colors.background     = 0xFF000000u;
    t.colors.surface        = 0xFF1C1C1Eu;
    t.colors.text_primary   = 0xFFFFFFFFu;
    t.colors.text_secondary = 0xFF8E8E93u;
    t.colors.border         = 0xFF2C2C2Eu;
    t.colors.error          = 0xFFFF3B30u;
    t.typography.font_color = t.colors.text_primary;
    return t;
}


static AromaTheme theme_named(const char *name)
{
    if (!strcmp(name, "Fitness"))
        return theme_fitness();
    if (!strcmp(name, "Travel"))
        return theme_travel();
    if (!strcmp(name, "Aroma Violet dark"))
        return theme_aroma_violet_dark();
    if (!strcmp(name, "Material Blue dark"))
        return aroma_theme_create_material_blue_dark();
    if (!strcmp(name, "Material Blue"))
        return aroma_theme_create_material_blue();
    return aroma_theme_create_dark();
}

static void apply_theme(void)
{
    AromaTheme theme = s_pinned_theme[0]
                           ? theme_named(s_pinned_theme)
                           : (s_dark ? aroma_theme_create_material_black()
                                     : aroma_theme_create_material_blue());
    aroma_theme_set_global(&theme);
    aroma_ui_request_redraw(NULL);
}

EMSCRIPTEN_KEEPALIVE void aroma_sandbox_set_theme_preset(const char *name)
{
    if (!name || !name[0])
        return;
    snprintf(s_pinned_theme, sizeof(s_pinned_theme), "%s", name);
    s_pinned_theme[sizeof(s_pinned_theme) - 1] = '\0';
    apply_theme();
}

static void noop_cb(void *userdata)
{
    (void)userdata;
}

/* The charts demo ships a Shuffle button so the docs page proves the plots
 * are live, not screenshots: each press regenerates the series behind the
 * same ids and the widget re-rasterises on the next frame. */
static unsigned s_plot_seed = 0x51ED270Bu;

static double plot_rand(int span)
{
    s_plot_seed = s_plot_seed * 1664525u + 1013904223u;
    return (double)((s_plot_seed >> 16) % (unsigned)span);
}

static void plots_shuffle_cb(void *userdata)
{
    (void)userdata;
    if (!s_registry)
        return;

    double a[7], b[7], ys[7];
    AromaNode *line = IncenseFindWidgetQuiet(s_registry, "plot_line");
    if (line)
    {
        for (int i = 0; i < 7; i++)
        {
            a[i] = 2.0 + plot_rand(9); /* 2..10, axis 0..12 */
            b[i] = 2.0 + plot_rand(9);
        }
        aroma_chart_clear_series(line);
        aroma_chart_add_series(line, NULL, a, 7);
        aroma_chart_add_series(line, NULL, b, 7);
        aroma_chart_set_series_color(line, 0, 0xFF8AB4F8u);
        aroma_chart_set_series_color(line, 1, 0xFFF28B82u);
        aroma_chart_set_series_thickness(line, 0, 2.0f);
        aroma_chart_set_series_thickness(line, 1, 2.0f);
    }

    AromaNode *scatter = IncenseFindWidgetQuiet(s_registry, "plot_scatter");
    if (scatter)
    {
        double sx[5], sy[5];
        for (int i = 0; i < 5; i++)
        {
            sx[i] = plot_rand(10); /* 0..9, axis 0..10 */
            sy[i] = plot_rand(10);
        }
        aroma_chart_clear_series(scatter);
        aroma_chart_add_series(scatter, NULL, sx, 5);
        aroma_chart_add_series(scatter, NULL, sy, 5);
        aroma_chart_set_series_color(scatter, 0, 0xFF81C995u);
        aroma_chart_set_series_color(scatter, 1, 0xFFFDD663u);
        aroma_chart_set_series_point_type(scatter, 0, AROMA_CHART_POINT_DOTS);
        aroma_chart_set_series_point_type(scatter, 1, AROMA_CHART_POINT_TRIANGLES);
    }

    AromaNode *points = IncenseFindWidgetQuiet(s_registry, "plot_line_points");
    if (points)
    {
        for (int i = 0; i < 7; i++)
            ys[i] = 2.0 + plot_rand(10); /* 2..11, axis 0..12 */
        aroma_chart_clear_series(points);
        aroma_chart_add_series(points, NULL, ys, 7);
        aroma_chart_set_series_color(points, 0, 0xFFC58AF9u);
        aroma_chart_set_series_point_type(points, 0, AROMA_CHART_POINT_CIRCLES);
        aroma_chart_set_series_line_style(points, 0, AROMA_CHART_DASHED);
        aroma_chart_set_series_thickness(points, 0, 2.0f);
    }

    AromaNode *bar = IncenseFindWidgetQuiet(s_registry, "plot_bar");
    if (bar)
    {
        double qa[4], qb[4];
        for (int i = 0; i < 4; i++)
        {
            qa[i] = 3.0 + plot_rand(15); /* 3..17, axis 0..18 */
            qb[i] = 3.0 + plot_rand(15);
        }
        aroma_chart_clear_series(bar);
        aroma_chart_add_series(bar, NULL, qa, 4);
        aroma_chart_add_series(bar, NULL, qb, 4);
        aroma_chart_set_series_color(bar, 0, 0xFF8AB4F8u);
        aroma_chart_set_series_color(bar, 1, 0xFFF28B82u);
    }

    aroma_ui_request_redraw(NULL);
}

static void select_theme_cb(int index, const char *option, void *userdata)
{
    (void)index;
    (void)userdata;
    if (!option)
        return;
    AromaTheme theme;
    if (!strcmp(option, "Dark"))
        theme = aroma_theme_create_dark();
    else if (!strcmp(option, "High contrast"))
        theme = aroma_theme_create_high_contrast();
    else if (!strcmp(option, "Material Blue"))
        theme = aroma_theme_create_material_blue();
    else if (!strcmp(option, "Material Teal"))
        theme = aroma_theme_create_material_teal();
    else if (!strcmp(option, "Material Green"))
        theme = aroma_theme_create_material_green();
    else if (!strcmp(option, "Material Orange"))
        theme = aroma_theme_create_material_orange();
    else if (!strcmp(option, "Material Pink"))
        theme = aroma_theme_create_material_pink();
    else if (!strcmp(option, "Material Black"))
        theme = aroma_theme_create_material_black();
    else if (!strcmp(option, "High contrast dark"))
        theme = aroma_theme_create_high_contrast_dark();
    else if (!strcmp(option, "Material Blue dark"))
        theme = aroma_theme_create_material_blue_dark();
    else if (!strcmp(option, "Material Teal dark"))
        theme = aroma_theme_create_material_teal_dark();
    else if (!strcmp(option, "Material Green dark"))
        theme = aroma_theme_create_material_green_dark();
    else if (!strcmp(option, "Material Orange dark"))
        theme = aroma_theme_create_material_orange_dark();
    else if (!strcmp(option, "Material Pink dark"))
        theme = aroma_theme_create_material_pink_dark();
    else
        theme = aroma_theme_create_default();
    aroma_theme_set_global(&theme);
    aroma_ui_request_redraw(NULL);
}

static void register_preview_callbacks(void)
{
    static const char *names[] = {
        "back", "select_theme", "studio_reset", "studio_tab", "studio_orbit",
        "studio_tilt", "studio_zoom", "studio_lightx", "studio_lighty",
        "studio_lightz", "on_search_change", "navigate",
        "navigate_wifi_connect", "wifi_connect",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        IncenseRegisterCallback(names[i], INCENSE_CALLBACK_VOID_PTR,
                                (void *)noop_cb, NULL);
    IncenseRegisterCallback("select_theme", INCENSE_CALLBACK_INT_STRING_PTR,
                            (void *)select_theme_cb, NULL);
    IncenseRegisterCallback("plots_shuffle", INCENSE_CALLBACK_VOID_PTR,
                            (void *)plots_shuffle_cb, NULL);
    loud_dashboard_register_callbacks();
    fitness_register_callbacks();
    travel_register_callbacks();

}

static void mount_blank(void)
{
    static const char blank[] =
        "Window {\n"
        "    width: 320\n"
        "    height: 480\n"
        "    title: \"Sandbox\"\n"
        "    Container { x: 0 y: 0 width: 320 height: 480 }\n"
        "}\n";
    if (s_window) {
        aroma_ui_destroy_window(s_window);
        s_window = NULL;
    }
    if (s_registry) {
        IncenseFreeRegistry(s_registry);
        s_registry = NULL;
    }
    s_window = IncenseLoadStringEx(blank, s_font, s_icon_font, &s_registry);
}

EMSCRIPTEN_KEEPALIVE void aroma_sandbox_reload(const char *source)
{
    s_has_error = 0;
    s_last_error[0] = '\0';
    loud_dashboard_reset();
    if (!source || !source[0]) {
        mount_blank();
        if (!s_window)
            set_error("Failed to create window");
        return;
    }
    if (s_window) {
        aroma_ui_destroy_window(s_window);
        s_window = NULL;
    }
    if (s_registry) {
        IncenseFreeRegistry(s_registry);
        s_registry = NULL;
    }
    s_window = IncenseLoadStringEx(source, s_font, s_icon_font, &s_registry);
    int nerrs = 0;
    const IncenseError *errs = IncenseGetErrors(&nerrs);
    const IncenseError *first = NULL;
    for (int i = 0; i < nerrs; i++) {
        if (errs[i].type == INCENSE_ERROR_SUGGESTION)
            continue;
        if (!first)
            first = &errs[i];
    }
    if (!s_window || IncenseHasFatalError()) {
        char buf[1024];
        if (first)
            snprintf(buf, sizeof(buf), "Line %d: %s", first->line, first->message);
        else
            snprintf(buf, sizeof(buf), "Failed to create window");
        set_error(buf);
        return;
    }
    if (first) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "Line %d: %s", first->line, first->message);
        set_error(buf);
        return;
    }
    loud_dashboard_after_mount(s_registry);
    fitness_after_mount(s_registry);
    travel_after_mount(s_registry);

    aroma_ui_request_redraw(NULL);
}

EMSCRIPTEN_KEEPALIVE int aroma_sandbox_has_error(void)
{
    return s_has_error;
}

EMSCRIPTEN_KEEPALIVE const char *aroma_sandbox_get_last_error(void)
{
    return s_last_error;
}

EMSCRIPTEN_KEEPALIVE void aroma_sandbox_set_theme(int dark)
{
    s_dark = dark ? true : false;
    apply_theme();
}

EMSCRIPTEN_KEEPALIVE int aroma_sandbox_get_width(void)
{
#ifdef __EMSCRIPTEN__
    double w = 0.0, h = 0.0;
    if (emscripten_get_element_css_size("#canvas", &w, &h) == EMSCRIPTEN_RESULT_SUCCESS && w > 0.0)
        return (int)w;
#endif
    return SANDBOX_WIN_W;
}

EMSCRIPTEN_KEEPALIVE int aroma_sandbox_get_height(void)
{
#ifdef __EMSCRIPTEN__
    double w = 0.0, h = 0.0;
    if (emscripten_get_element_css_size("#canvas", &w, &h) == EMSCRIPTEN_RESULT_SUCCESS && h > 0.0)
        return (int)h;
#endif
    return SANDBOX_WIN_H;
}

EMSCRIPTEN_KEEPALIVE void aroma_sandbox_init(void)
{
    if (s_booted)
        return;
    s_booted = true;
    if (!s_font)
        s_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                               aroma_ubuntu_ttf_len,
                                               SANDBOX_FONT_PX);
    if (!s_icon_font)
        s_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len,
                                                    SANDBOX_FONT_PX);
    create_sized_fonts();
    register_preview_callbacks();
    apply_theme();
    mount_blank();
}

int main(void)
{
    if (!aroma_ui_init())
        return 1;
    /* The sandbox is always an embedded preview: let the host page show
     * through wherever the mounted app draws nothing (the curated dashboard
     * demo floats a window on the docs page background). */
    aroma_ui_set_transparent_background(true);
    aroma_sandbox_init();
    return 0;
}
