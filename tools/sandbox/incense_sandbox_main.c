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
#include "aroma_ubuntu_font.h"
#include "aroma_material_font.h"
#include "aroma_incense_loader.h"

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
 * "Material Blue dark").  The embedded smart-home demo pins itself to
 * Material Blue dark this way, and every later apply_theme() call
 * (including the docs page's light/dark sync) keeps re-applying the pin. */
static char s_pinned_theme[64];

static AromaTheme theme_named(const char *name)
{
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

/* ------------------------------------------------------------------ *
 * Smart home demo (docs landing page).
 *
 * Unlike the other previews this one is wired for real: room tabs,
 * lights, dimmers, scenes, climate and security all mutate the live
 * widget tree so the embedded demo responds to the user.
 * ------------------------------------------------------------------ */

/* Accents for the Material Blue *dark* palette the smart-home demo pins
 * itself to (see aroma_sandbox_set_theme_preset below): muted foreground
 * plus the Google dark-mode accent ramps. */
#define SH_GRAY     0x9AA0A6FFu
#define SH_AMBER    0xFDD663FFu
#define SH_GREEN    0x81C995FFu
#define SH_RED      0xF28B82FFu
#define SH_CYAN     0x78D9ECFFu
#define SH_LIGHT_ON 0xFFB74DFFu
#define SH_ROOMS 4
#define SH_LIGHTS 3

typedef struct {
    const char *name;
    const char *note;
    int dim;
    bool on;
} ShLight;

typedef struct {
    const char *subtitle;
    ShLight lights[SH_LIGHTS];
} ShRoom;

static const ShRoom s_sh_rooms[SH_ROOMS] = {
    {"Living Room · 6 devices online", {
        {"Ceiling light", "Warm white", 80, true},
        {"Floor lamp", "Reading corner", 60, true},
        {"Desk lamp", "Cool white", 35, false}}},
    {"Bedroom · 4 devices online", {
        {"Bedside lamp", "Warm white", 45, true},
        {"Ceiling light", "Soft white", 0, false},
        {"Wardrobe strip", "Ambient", 25, false}}},
    {"Kitchen · 5 devices online", {
        {"Pendants", "Island bar", 70, true},
        {"Under cabinet", "Cool white", 85, true},
        {"Pantry", "Ambient", 50, true}}},
    {"Garage · 2 devices online", {
        {"Main light", "Fluorescent", 100, true},
        {"Workbench", "Spot", 0, false},
        {"Outside", "Security lamp", 0, false}}},
};

/* Widget ids owned by the demo markup, in fixed per-light order. */
static const char *const s_sh_ids[SH_LIGHTS][5] = {
    {"light1_name", "light1_status", "light1_slider", "light1_sw", "light1_icon"},
    {"light2_name", "light2_status", "light2_slider", "light2_sw", "light2_icon"},
    {"light3_name", "light3_status", "light3_slider", "light3_sw", "light3_icon"},
};

/* Full-brightness draw per fixture; scaled by the dimmer for the live
 * "Total draw" readout so the number reacts to every light change. */
static const int s_sh_watts[SH_LIGHTS] = {180, 120, 80};
#define SH_WATTS_MAX 400

/* Scene transport: the selected scene button is highlighted, the rest
 * fall back to outlined, so the card always shows the active scene. */
static const char *const s_sh_scene_ids[4] = {
    "scene_btn_movie", "scene_btn_dinner", "scene_btn_away", "scene_btn_sleep"};

static int s_sh_room;
static int s_sh_mode;
static bool s_sh_cam_paused;

/* Lets the (headless) interaction tests observe which callback fired. */
static void sh_trace(const char *name)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ window.__aromaLastCb = UTF8ToString($0); }, name);
#else
    (void)name;
#endif
}

static AromaNode *sh_node(const char *id)
{
    return s_registry ? IncenseFindWidgetQuiet(s_registry, id) : NULL;
}

static void sh_set_text(const char *id, const char *text)
{
    AromaNode *n = sh_node(id);
    if (n)
        aroma_label_set_text(n, text);
}

static void sh_set_state_label(const char *id, const char *text, uint32_t color)
{
    AromaNode *n = sh_node(id);
    if (!n)
        return;
    aroma_label_set_text(n, text);
    aroma_label_set_color(n, color);
}

static void sh_update_lights(void)
{
    int on_count = 0;
    int draw = 0;
    for (int i = 0; i < SH_LIGHTS; i++) {
        AromaNode *slider = sh_node(s_sh_ids[i][2]);
        AromaNode *sw = sh_node(s_sh_ids[i][3]);
        AromaNode *icon = sh_node(s_sh_ids[i][4]);
        bool on = sw ? aroma_switch_get_state(sw) : false;
        int dim = slider ? aroma_slider_get_value(slider) : 0;
        char buf[64];
        if (on) {
            snprintf(buf, sizeof(buf), "%s · %d%%",
                     s_sh_rooms[s_sh_room].lights[i].note, dim);
            on_count++;
            draw += (s_sh_watts[i] * dim + 50) / 100;
        } else {
            snprintf(buf, sizeof(buf), "Off");
        }
        sh_set_state_label(s_sh_ids[i][1], buf, SH_GRAY);
        if (icon)
            aroma_icon_set_color(icon, on ? SH_LIGHT_ON : SH_GRAY);
    }
    char count[16];
    snprintf(count, sizeof(count), "%d on", on_count);
    sh_set_text("lights_count", count);

    char watts[16];
    snprintf(watts, sizeof(watts), "%d W", draw);
    sh_set_text("total_draw_value", watts);
    AromaNode *bar = sh_node("total_draw");
    if (bar) {
        float frac = (float)draw / (float)SH_WATTS_MAX;
        if (frac > 1.0f)
            frac = 1.0f;
        aroma_progressbar_set_progress(bar, frac);
    }
    aroma_ui_request_redraw(NULL);
}

static void sh_update_climate(void)
{
    AromaNode *slider = sh_node("target_slider");
    AromaNode *gauge = sh_node("climate_gauge");
    int target = slider ? aroma_slider_get_value(slider) : 22;
    char buf[64];

    snprintf(buf, sizeof(buf), "%d°", target);
    sh_set_text("target_value", buf);
    if (gauge)
        aroma_gauge_set_value(gauge, (float)target);

    uint32_t color = SH_AMBER;
    if (s_sh_mode == 0) {
        snprintf(buf, sizeof(buf), "Heating to %d°", target);
        sh_set_state_label("climate_state", "Comfort", SH_GREEN);
    } else if (s_sh_mode == 1) {
        snprintf(buf, sizeof(buf), "Cooling to %d°", target);
        color = SH_CYAN;
        sh_set_state_label("climate_state", "Fast cool", SH_CYAN);
    } else {
        snprintf(buf, sizeof(buf), "Auto · holding %d°", target);
        color = SH_GREEN;
        sh_set_state_label("climate_state", "Eco", SH_GREEN);
    }
    sh_set_state_label("climate_status", buf, color);
    aroma_ui_request_redraw(NULL);
}

/* Keeps the scene card in sync: one tonal (selected) button, rest
 * outlined, plus the caption under the grid. */
static void sh_set_scene_active(int active)
{
    for (int i = 0; i < 4; i++) {
        AromaNode *btn = sh_node(s_sh_scene_ids[i]);
        if (btn)
            aroma_button_set_type(btn, i == active ? BUTTON_TYPE_TONAL
                                                   : BUTTON_TYPE_OUTLINED);
    }
}

static void sh_apply_room(int index)
{
    if (index < 0 || index >= SH_ROOMS)
        return;
    s_sh_room = index;
    const ShRoom *room = &s_sh_rooms[index];
    for (int i = 0; i < SH_LIGHTS; i++) {
        sh_set_text(s_sh_ids[i][0], room->lights[i].name);
        AromaNode *slider = sh_node(s_sh_ids[i][2]);
        AromaNode *sw = sh_node(s_sh_ids[i][3]);
        if (slider)
            aroma_slider_set_value(slider, room->lights[i].dim);
        if (sw)
            aroma_switch_set_state(sw, room->lights[i].on);
    }
    sh_set_text("room_subtitle", room->subtitle);
    sh_set_scene_active(-1);
    sh_set_state_label("scene_status",
                       "Tap a scene to set the room", SH_GRAY);
    sh_update_lights();
}

static void sh_room_cb(AromaNode *node, int index, void *userdata)
{
    (void)node;
    (void)userdata;
    sh_trace("smarthome_room");
    sh_apply_room(index);
}

static void sh_mode_cb(AromaNode *node, int index, void *userdata)
{
    (void)node;
    (void)userdata;
    sh_trace("smarthome_mode");
    s_sh_mode = index;
    sh_update_climate();
}

static bool sh_target_cb(AromaNode *node, void *userdata)
{
    (void)node;
    (void)userdata;
    sh_trace("smarthome_target");
    sh_update_climate();
    return true;
}

static bool sh_light_cb(AromaNode *node, void *userdata)
{
    (void)node;
    int index = (int)(intptr_t)userdata;
    static const char *const names[SH_LIGHTS] = {
        "smarthome_light1", "smarthome_light2", "smarthome_light3"};
    if (index >= 0 && index < SH_LIGHTS)
        sh_trace(names[index]);
    sh_update_lights();
    return true;
}

static bool sh_dimmer_cb(AromaNode *node, void *userdata)
{
    (void)node;
    int index = (int)(intptr_t)userdata;
    static const char *const names[SH_LIGHTS] = {
        "smarthome_dimmer1", "smarthome_dimmer2", "smarthome_dimmer3"};
    if (index >= 0 && index < SH_LIGHTS)
        sh_trace(names[index]);
    sh_update_lights();
    return true;
}

static bool sh_scene_cb(AromaNode *node, void *userdata)
{
    (void)node;
    int index = (int)(intptr_t)userdata;
    static const char *const names[4] = {
        "smarthome_scene_movie", "smarthome_scene_dinner",
        "smarthome_scene_away", "smarthome_scene_sleep"};
    if (index < 0 || index > 3)
        return false;
    sh_trace(names[index]);

    static const int dims[4][SH_LIGHTS] = {
        {20, 35, 20}, {70, 70, 60}, {0, 0, 0}, {0, 0, 0}};
    static const bool onoff[4][SH_LIGHTS] = {
        {true, true, false}, {true, true, true},
        {false, false, false}, {false, false, false}};
    for (int i = 0; i < SH_LIGHTS; i++) {
        AromaNode *slider = sh_node(s_sh_ids[i][2]);
        AromaNode *sw = sh_node(s_sh_ids[i][3]);
        if (slider && onoff[index][i])
            aroma_slider_set_value(slider, dims[index][i]);
        if (sw)
            aroma_switch_set_state(sw, onoff[index][i]);
    }

    static const char *const labels[4] = {
        "Movie · lights dimmed", "Dinner · warm and bright",
        "Away · all lights off", "Sleep · everything off"};
    static const uint32_t colors[4] = {
        SH_AMBER, 0xD7AEFBFFu, SH_RED, 0xAECBFAFFu};
    sh_set_state_label("scene_status", labels[index], colors[index]);
    sh_set_scene_active(index);
    sh_update_lights();
    return true;
}

/* Front door camera: pause/resume the live MJPEG-style feed and swap the
 * badge + button glyph so the state change is visible at a glance. */
static void sh_cam_cb(void *userdata)
{
    (void)userdata;
    sh_trace("smarthome_cam");
    s_sh_cam_paused = !s_sh_cam_paused;
    AromaNode *video = sh_node("cam_video");
    if (video) {
        if (s_sh_cam_paused)
            aroma_video_pause(video);
        else
            aroma_video_play(video);
    }
    AromaNode *btn = sh_node("cam_btn");
    if (btn)
        aroma_iconbutton_set_icon(btn, s_sh_cam_paused ? AROMA_ICON_VIDEOCAM_OFF
                                                       : AROMA_ICON_VIDEOCAM);
    sh_set_state_label("cam_state", s_sh_cam_paused ? "Paused" : "Live",
                       s_sh_cam_paused ? SH_GRAY : SH_GREEN);
    sh_set_text("cam_meta", s_sh_cam_paused ? "Front Door · Feed paused"
                                            : "Front Door · Motion 2 min ago");
    aroma_ui_request_redraw(NULL);
}

static bool sh_lock_cb(AromaNode *node, void *userdata)
{
    (void)userdata;
    sh_trace("smarthome_lock");
    bool locked = node ? aroma_switch_get_state(node) : true;
    sh_set_state_label("lock_state", locked ? "Locked" : "Unlocked",
                       locked ? SH_GREEN : SH_RED);
    AromaNode *icon = sh_node("lock_icon");
    if (icon) {
        aroma_icon_set_text(icon, locked ? AROMA_ICON_LOCK
                                         : AROMA_ICON_LOCK_OPEN, s_icon_font);
        aroma_icon_set_color(icon, locked ? SH_GREEN : SH_RED);
    }
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool sh_garage_cb(AromaNode *node, void *userdata)
{
    (void)userdata;
    sh_trace("smarthome_garage");
    bool closed = node ? aroma_switch_get_state(node) : true;
    sh_set_state_label("garage_state", closed ? "Closed" : "Open",
                       closed ? SH_GREEN : SH_RED);
    aroma_ui_request_redraw(NULL);
    return true;
}

static void register_smarthome_callbacks(void)
{
    IncenseRegisterCallback("smarthome_room", INCENSE_CALLBACK_NODE_INT_PTR,
                            (void *)sh_room_cb, NULL);
    IncenseRegisterCallback("smarthome_mode", INCENSE_CALLBACK_NODE_INT_PTR,
                            (void *)sh_mode_cb, NULL);
    IncenseRegisterCallback("smarthome_target", INCENSE_CALLBACK_BOOL_PTR,
                            (void *)sh_target_cb, NULL);

    static const char *const scene_names[4] = {
        "smarthome_scene_movie", "smarthome_scene_dinner",
        "smarthome_scene_away", "smarthome_scene_sleep"};
    for (int i = 0; i < 4; i++)
        IncenseRegisterCallback(scene_names[i], INCENSE_CALLBACK_BOOL_PTR,
                                (void *)sh_scene_cb, (void *)(intptr_t)i);

    static const char *const light_names[SH_LIGHTS] = {
        "smarthome_light1", "smarthome_light2", "smarthome_light3"};
    for (int i = 0; i < SH_LIGHTS; i++)
        IncenseRegisterCallback(light_names[i], INCENSE_CALLBACK_BOOL_PTR,
                                (void *)sh_light_cb, (void *)(intptr_t)i);

    static const char *const dimmer_names[SH_LIGHTS] = {
        "smarthome_dimmer1", "smarthome_dimmer2", "smarthome_dimmer3"};
    for (int i = 0; i < SH_LIGHTS; i++)
        IncenseRegisterCallback(dimmer_names[i], INCENSE_CALLBACK_BOOL_PTR,
                                (void *)sh_dimmer_cb, (void *)(intptr_t)i);

    IncenseRegisterCallback("smarthome_lock", INCENSE_CALLBACK_BOOL_PTR,
                            (void *)sh_lock_cb, NULL);
    IncenseRegisterCallback("smarthome_garage", INCENSE_CALLBACK_BOOL_PTR,
                            (void *)sh_garage_cb, NULL);
    IncenseRegisterCallback("smarthome_cam", INCENSE_CALLBACK_VOID_PTR,
                            (void *)sh_cam_cb, NULL);
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
    register_smarthome_callbacks();
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
    s_sh_room = 0;
    s_sh_mode = 0;
    s_sh_cam_paused = false;
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
    register_preview_callbacks();
    apply_theme();
    mount_blank();
}

int main(void)
{
    if (!aroma_ui_init())
        return 1;
    aroma_sandbox_init();
    return 0;
}
