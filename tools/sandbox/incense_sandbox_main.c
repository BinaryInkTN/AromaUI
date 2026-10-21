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
#include "loud_dashboard.h"

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
static const int s_text_sizes[] = {12, 14, 16, 18, 20, 23, 24, 26, 40, 52};
static AromaFont *s_text_fonts[sizeof(s_text_sizes) / sizeof(s_text_sizes[0])];
static const int s_icon_sizes[] = {12, 14, 16, 18, 20, 24, 26, 32, 36, 40, 48, 64};
static AromaFont *s_icon_fonts[sizeof(s_icon_sizes) / sizeof(s_icon_sizes[0])];

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

static AromaTheme theme_named(const char *name)
{
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
    loud_dashboard_register_callbacks();
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
