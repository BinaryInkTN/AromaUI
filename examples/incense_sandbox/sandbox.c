#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static void em_main_loop(void);
#endif

static AromaWindow *g_window = NULL;
static AromaFont *g_text_font = NULL;
static AromaFont *g_icon_font = NULL;
static IncenseRegistry *g_registry = NULL;
static int g_hot_reload_watcher = -1;

static AromaNode *g_main_page = NULL;
static AromaNode *g_detail_pages[8];
static const char *g_detail_page_ids[8] = {
    "page_airplane",
    "page_wifi",
    "page_bluetooth",
    NULL, NULL, NULL, NULL, NULL
};
static int g_detail_page_count = 3;
static AromaNode *g_current_page = NULL;
static AromaNode *g_from_page = NULL;
static bool g_is_animating = false;

static inline int get_win_w(void) {
    return g_window ? g_window->rect.width : 320;
}
static inline int get_win_h(void) {
    return g_window ? g_window->rect.height : 480;
}
#define WIN_W get_win_w()
#define WIN_H get_win_h()
#define NAV_ANIM_MS 250

static void set_page_x(AromaNode *page, int x)
{
    AromaRect *r = aroma_node_get_rect(page);

    if (!r || r->x == x)
        return;
    r->x = x;
    /* Do not shift descendants here. The per-frame layout pass in
       window_update_callback propagates the page offset to children
       exactly once. Shifting the subtree here as well would move
       grandchildren twice per frame, so pages would smear over each
       other mid-transition. */
    aroma_node_invalidate_tree(page);
}

static void on_nav_complete(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;

    if (g_from_page)
    {
        aroma_node_set_hidden(g_from_page, true);
        aroma_node_invalidate_tree(g_from_page);
        g_from_page = NULL;
    }

    if (g_current_page)
    {
        set_page_x(g_current_page, 0);
        aroma_node_update_layout(g_current_page, 0, 0, WIN_W, WIN_H);
        aroma_node_invalidate_tree(g_current_page);
    }

    g_is_animating = false;
    aroma_ui_request_redraw(NULL);
}

static void slide_cb(AromaNode *target, float val, void *user_data)
{
    (void)user_data;
    set_page_x(target, (int)val);
}

static void paint_page_opaque(AromaNode *page)
{
    if (!page)
        return;
    /* Fullscreen pages must be opaque: during a slide transition two
       pages share the screen, and transparent pages would let both
       pages' text show through each other as ghosting. */
    aroma_container_set_debug_bg(page, aroma_ui_get_theme().colors.background);
    aroma_node_invalidate(page);
}

static void navigate_to(AromaNode *target, bool is_back)
{
    if (!g_window || !target || g_is_animating)
        return;

    AromaNode *current = g_current_page;
    if (!current)
        current = g_main_page;

    if (current == target)
        return;

    AromaRect *cur_rect = aroma_node_get_rect(current);
    AromaRect *tgt_rect = aroma_node_get_rect(target);

    if (!cur_rect || !tgt_rect)
        return;

    int cur_start_x = 0;
    int cur_end_x = is_back ? WIN_W : -WIN_W;
    int tgt_start_x = is_back ? -WIN_W : WIN_W;
    int tgt_end_x = 0;

    g_is_animating = true;
    g_from_page = current;
    g_current_page = target;

    aroma_node_set_hidden(target, false);
    paint_page_opaque(current);
    paint_page_opaque(target);
    set_page_x(target, tgt_start_x);

    AromaAnimation *cur_anim = aroma_animation_start_custom(
        current, (float)cur_start_x, (float)cur_end_x,
        NAV_ANIM_MS, slide_cb, NULL);
    if (cur_anim)
        aroma_animation_set_easing(cur_anim, AROMA_EASE_OUT_CUBIC);

    AromaAnimation *tgt_anim = aroma_animation_start_custom(
        target, (float)tgt_start_x, (float)tgt_end_x,
        NAV_ANIM_MS, slide_cb, NULL);
    if (tgt_anim)
    {
        aroma_animation_set_easing(tgt_anim, AROMA_EASE_OUT_CUBIC);
        aroma_animation_set_on_complete(tgt_anim, on_nav_complete);
    }
}

static void navigate_to_detail(int index, void *user_data)
{
    (void)user_data;
    if (g_is_animating) return;

    int page_index = -1;
    if (index == 0) page_index = 0;
    else if (index == 1) page_index = 1;
    else if (index == 2) page_index = 2;

    if (page_index < 0 || page_index >= g_detail_page_count) return;
    if (!g_detail_pages[page_index]) return;

    navigate_to(g_detail_pages[page_index], false);
}

static void navigate_to_main(void *user_data)
{
    (void)user_data;
    if (g_is_animating) return;
    if (!g_main_page) return;

    navigate_to(g_main_page, true);
}

static void navigate_to_wifi_connect(void *user_data)
{
    (void)user_data;
    if (g_is_animating) return;
    if (!g_window || !g_registry) return;

    AromaNode *target = IncenseFindWidget(g_registry, "page_wifi_connect");
    if (!target) return;

    navigate_to(target, false);
}

static void on_wifi_connect(void *user_data)
{
    (void)user_data;
}

static bool g_search_active = false;
static char g_search_text[128] = {0};

static int strcase_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return 0;
    while (*haystack)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*n && tolower((unsigned char)*h) == tolower((unsigned char)*n))
        {
            h++;
            n++;
        }
        if (!*n) return 1;
        haystack++;
    }
    return 0;
}

static bool on_search_change(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!g_registry) return false;

    if (text && strlen(text) > 0) {
        strncpy(g_search_text, text, sizeof(g_search_text) - 1);
        g_search_text[sizeof(g_search_text) - 1] = '\0';
        g_search_active = true;
    } else {
        g_search_text[0] = '\0';
        g_search_active = false;
    }

    AromaNode *list = IncenseFindWidget(g_registry, "settings_list");
    if (!list) return false;

    size_t count = aroma_listview_get_count(list);
    for (size_t i = 0; i < count; i++) {
        bool should_show = !g_search_active;
        if (g_search_active) {
            const char *item_text = aroma_listview_get_item_text(list, (int)i);
            if (item_text && strcase_contains(item_text, g_search_text)) {
                should_show = true;
            } else {
                should_show = false;
            }
        }
        aroma_listview_set_item_hidden(list, (int)i, !should_show);
    }
    aroma_node_update_layout(list, 0, 120, WIN_W, 360);
    aroma_node_invalidate(list);
    aroma_ui_request_redraw(NULL);
    return true;
}

static void register_navigation(void)
{
    g_main_page = NULL;
    g_current_page = NULL;
    g_from_page = NULL;
    g_is_animating = false;

    if (!g_window || !g_registry) return;

    for (int i = 0; i < 8; i++)
    {
        g_detail_pages[i] = NULL;
    }

    g_main_page = IncenseFindWidget(g_registry, "page_main");

    for (int i = 0; i < g_detail_page_count && i < 8; i++)
    {
        if (g_detail_page_ids[i])
        {
            g_detail_pages[i] = IncenseFindWidget(g_registry, g_detail_page_ids[i]);
        }
    }

    if (g_main_page)
    {
        aroma_node_set_hidden(g_main_page, false);
        AromaRect *r = aroma_node_get_rect(g_main_page);
        if (r) r->x = 0;
        aroma_node_update_layout(g_main_page, 0, 0, WIN_W, WIN_H);
    }
    for (int i = 0; i < g_detail_page_count; i++)
    {
        if (g_detail_pages[i])
        {
            aroma_node_set_hidden(g_detail_pages[i], true);
            AromaRect *r = aroma_node_get_rect(g_detail_pages[i]);
            if (r) r->x = 0;
            aroma_node_update_layout(g_detail_pages[i], 0, 0, WIN_W, WIN_H);
        }
    }

    g_current_page = g_main_page;
}

#ifdef __EMSCRIPTEN__
static void em_main_loop(void)
{
    if (!aroma_ui_is_running()) return;
    aroma_ui_process_events();
    if (g_window)
    {
        aroma_ui_render(g_window);
    }
}
#endif

static void on_theme_select(int index, const char *option, void *user_data)
{
    (void)option;
    (void)user_data;
    /* Order must match the theming demo's Dropdown options. */
    static AromaTheme (*makers[])(void) = {
        aroma_theme_create_default,
        aroma_theme_create_dark,
        aroma_theme_create_high_contrast,
        aroma_theme_create_material_blue,
        aroma_theme_create_material_teal,
        aroma_theme_create_material_green,
        aroma_theme_create_material_orange,
        aroma_theme_create_material_pink,
        aroma_theme_create_material_black,
        aroma_theme_create_high_contrast_dark,
        aroma_theme_create_material_blue_dark,
        aroma_theme_create_material_teal_dark,
        aroma_theme_create_material_green_dark,
        aroma_theme_create_material_orange_dark,
        aroma_theme_create_material_pink_dark,
    };
    size_t count = sizeof(makers) / sizeof(makers[0]);
    if (index < 0 || (size_t)index >= count)
        return;
    AromaTheme theme = makers[index]();
    aroma_ui_set_theme(&theme);
}

/* ----- 3D Studio bench (viewer3d example) -----
 * Plain-words camera + light panel: Orbit/Tilt/Zoom sliders scrub both
 * studio viewers, Front/Side/Top jump to preset views, light sliders
 * move the (global) scene light. Touching any camera control stops the
 * turntable so it never fights the user; Reset restores it. Switching
 * tabs pushes the visible viewer's camera back into the sliders so the
 * panel always reflects the viewport. Sliders are int-based, so radian
 * values ride scaled (x100). */
static float s_studio_light[3] = {150.0f, 200.0f, 150.0f};
static int s_studio_tab = 0;
/* While pushing viewer state into sliders, suppress the take-over so a
   tab switch or reset does not stop the turntable as a side effect. */
static bool s_studio_quiet = false;

static AromaNode *studio_widget(const char *id)
{
    if (!g_registry || !id)
        return NULL;
    /* IncenseFindWidget warns on miss; the bench ids only exist in the
       viewer3d example, so stay quiet for every other demo. */
    IncenseClearErrors();
    AromaNode *node = IncenseFindWidget(g_registry, id);
    IncenseClearErrors();
    return node;
}

static void studio_show(const char *id, float value, const char *fmt)
{
    AromaNode *label = studio_widget(id);
    if (!label)
        return;
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, (double)value);
    aroma_label_set_text(label, buf);
}

static void studio_apply_camera(int field, float value)
{
    /* Camera settings are per model: only the visible viewer moves, so
       Fox and Cube never disturb each other. (The scene light stays
       global.) */
    AromaNode *viewer = studio_widget(s_studio_tab == 1 ? "model_cube" : "model_fox");
    Aroma3DCamera cam;
    if (!viewer || !aroma_3d_viewer_get_camera(viewer, &cam))
        return;
    if (field == 0)
        cam.theta = value;
    else if (field == 1)
    {
        cam.phi = value;
        if (cam.phi < 0.1f)
            cam.phi = 0.1f;
        if (cam.phi > 3.1f)
            cam.phi = 3.1f;
    }
    else
    {
        cam.radius = value;
        if (cam.radius < 0.5f)
            cam.radius = 0.5f;
    }
    aroma_3d_viewer_set_camera(viewer, &cam);
}

static void studio_stop_spin(void)
{
    AromaNode *viewers[2] = {studio_widget("model_fox"), studio_widget("model_cube")};
    for (int i = 0; i < 2; i++)
        if (viewers[i])
            aroma_3d_viewer_set_auto_rotate(viewers[i], false);
}

static bool studio_slider_orbit(AromaNode *node, void *user_data)
{
    (void)user_data;
    if (!node)
        return false;
    float v = aroma_slider_get_value(node) / 100.0f;
    studio_apply_camera(0, v);
    studio_show("v_orbit", v, "%.2f");
    if (!s_studio_quiet)
        studio_stop_spin();
    return true;
}

static bool studio_slider_tilt(AromaNode *node, void *user_data)
{
    (void)user_data;
    if (!node)
        return false;
    float v = aroma_slider_get_value(node) / 100.0f;
    studio_apply_camera(1, v);
    studio_show("v_tilt", v, "%.2f");
    if (!s_studio_quiet)
        studio_stop_spin();
    return true;
}

static bool studio_slider_zoom(AromaNode *node, void *user_data)
{
    (void)user_data;
    if (!node)
        return false;
    float v = (float)aroma_slider_get_value(node);
    studio_apply_camera(2, v);
    studio_show("v_zoom", v, "%.0f");
    if (!s_studio_quiet)
        studio_stop_spin();
    return true;
}

static bool studio_slider_light(AromaNode *node, void *user_data)
{
    if (!node)
        return false;
    int axis = (int)(intptr_t)user_data;
    if (axis < 0 || axis > 2)
        return false;
    static const char *const labels[3] = {"v_lightx", "v_lighty", "v_lightz"};
    s_studio_light[axis] = (float)aroma_slider_get_value(node);
    aroma_3d_set_light_position(s_studio_light[0], s_studio_light[1], s_studio_light[2]);
    studio_show(labels[axis], s_studio_light[axis], "%.0f");
    AromaNode *viewers[2] = {studio_widget("model_fox"), studio_widget("model_cube")};
    for (int i = 0; i < 2; i++)
        if (viewers[i])
            aroma_3d_viewer_update(viewers[i]);
    return true;
}

static void studio_set_slider(const char *id, int value)
{
    AromaNode *slider = studio_widget(id);
    if (slider)
        aroma_slider_set_value(slider, value);
}

/* Push a viewer's camera into the panel sliders + readouts. Runs
   quiet so syncing never stops the turntable as a side effect. */
static void studio_push_camera(AromaNode *viewer)
{
    Aroma3DCamera cam;
    if (!viewer || !aroma_3d_viewer_get_camera(viewer, &cam))
        return;
    int orbit = (int)(cam.theta * 100.0f) % 628;
    if (orbit < 0)
        orbit += 628;
    s_studio_quiet = true;
    studio_set_slider("s_orbit", orbit);
    studio_set_slider("s_tilt", (int)(cam.phi * 100.0f));
    studio_set_slider("s_zoom", (int)(cam.radius + 0.5f));
    s_studio_quiet = false;
    studio_show("v_orbit", orbit / 100.0f, "%.2f");
    studio_show("v_tilt", cam.phi, "%.2f");
    studio_show("v_zoom", cam.radius, "%.0f");
}

static bool studio_reset(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    AromaNode *fox = studio_widget("model_fox");
    AromaNode *cube = studio_widget("model_cube");
    Aroma3DCamera cam;
    if (fox && aroma_3d_viewer_get_camera(fox, &cam))
    {
        cam.theta = 0.0f;
        cam.phi = 1.0f;
        cam.radius = 232.0f;
        cam.fov = 45.0f;
        aroma_3d_viewer_set_camera(fox, &cam);
        aroma_3d_viewer_set_auto_rotate(fox, true);
    }
    if (cube && aroma_3d_viewer_get_camera(cube, &cam))
    {
        cam.theta = 0.6f;
        cam.phi = 1.1f;
        cam.radius = 5.0f;
        cam.fov = 45.0f;
        aroma_3d_viewer_set_camera(cube, &cam);
        aroma_3d_viewer_set_auto_rotate(cube, false);
    }
    s_studio_light[0] = 150.0f;
    s_studio_light[1] = 200.0f;
    s_studio_light[2] = 150.0f;
    aroma_3d_set_light_position(150.0f, 200.0f, 150.0f);
    studio_set_slider("s_lightx", 150);
    studio_set_slider("s_lighty", 200);
    studio_set_slider("s_lightz", 150);
    studio_show("v_lightx", 150.0f, "%.0f");
    studio_show("v_lighty", 200.0f, "%.0f");
    studio_show("v_lightz", 150.0f, "%.0f");
    studio_push_camera(s_studio_tab == 1 ? cube : fox);
    return true;
}

static void studio_tab_sync(AromaNode *node, int index, void *user_data)
{
    (void)node;
    (void)user_data;
    s_studio_tab = (index == 1) ? 1 : 0;
    AromaNode *viewer = studio_widget(s_studio_tab == 1 ? "model_cube" : "model_fox");
    studio_push_camera(viewer);
    AromaNode *name = studio_widget("scene_name");
    if (name)
        aroma_label_set_text(name, s_studio_tab == 1 ? "Cube" : "Fox");
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void aroma_sandbox_init(void)
{
    g_text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    /* Sandbox previews default to the light Material Blue app theme.
       The embedding docs page may switch this to Material Blue dark at
       runtime via aroma_sandbox_set_theme() when dark mode is on. */
    AromaTheme theme = aroma_theme_create_material_blue();
    aroma_ui_set_theme(&theme);
    IncenseRegisterCallback("navigate", INCENSE_CALLBACK_INT_PTR, (void *)navigate_to_detail, NULL);
    IncenseRegisterCallback("back", INCENSE_CALLBACK_VOID_PTR, (void *)navigate_to_main, NULL);
    IncenseRegisterCallback("navigate_wifi_connect", INCENSE_CALLBACK_VOID_PTR, (void *)navigate_to_wifi_connect, NULL);
    IncenseRegisterCallback("wifi_connect", INCENSE_CALLBACK_VOID_PTR, (void *)on_wifi_connect, NULL);
    IncenseRegisterCallback("on_search_change", INCENSE_CALLBACK_NODE_STRING_PTR, (void *)on_search_change, NULL);
    IncenseRegisterCallback("select_theme", INCENSE_CALLBACK_INT_STRING_PTR, (void *)on_theme_select, NULL);
    IncenseRegisterCallback("studio_orbit", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_slider_orbit, NULL);
    IncenseRegisterCallback("studio_tilt", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_slider_tilt, NULL);
    IncenseRegisterCallback("studio_zoom", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_slider_zoom, NULL);
    IncenseRegisterCallback("studio_reset", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_reset, NULL);
    IncenseRegisterCallback("studio_lightx", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_slider_light, (void *)(intptr_t)0);
    IncenseRegisterCallback("studio_lighty", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_slider_light, (void *)(intptr_t)1);
    IncenseRegisterCallback("studio_lightz", INCENSE_CALLBACK_BOOL_PTR, (void *)studio_slider_light, (void *)(intptr_t)2);
    IncenseRegisterCallback("studio_tab", INCENSE_CALLBACK_NODE_INT_PTR, (void *)studio_tab_sync, NULL);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void aroma_sandbox_set_theme(int dark)
{
    AromaTheme theme = dark ? aroma_theme_create_material_blue_dark()
                            : aroma_theme_create_material_blue();
    /* aroma_ui_set_theme() applies globally and invalidates every
       window, so the switch paints on the next frame. */
    aroma_ui_set_theme(&theme);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void aroma_sandbox_reload(const char *source)
{
    if (!source) return;

    AromaWindow *new_window = IncenseLoadStringEx(source, g_text_font, g_icon_font, &g_registry);
    if (!new_window)
    {
        return;
    }

    if (g_window)
    {
        aroma_ui_destroy_window(g_window);
        g_window = NULL;
    }

    g_window = new_window;
    AromaNode *root = (AromaNode *)g_window;
    aroma_event_set_root(root);
    register_navigation();
    /* Fresh document, fresh bench: the example carries explicit studio
       light props, so reset the cache to match. */
    s_studio_light[0] = 150.0f;
    s_studio_light[1] = 200.0f;
    s_studio_light[2] = 150.0f;
#ifdef __EMSCRIPTEN__
    aroma_textbox_enable_virtual_keyboard(root, true);
#endif
    aroma_node_invalidate_tree(root);
    aroma_ui_request_redraw(NULL);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char *aroma_sandbox_get_last_error(void)
{
    static char buf[512];
    int count = IncenseGetErrorCount();
    if (count <= 0) return "";
    const IncenseError *errs = IncenseGetErrors(&count);
    if (!errs || count <= 0) return "";
    snprintf(buf, sizeof(buf), "Line %d: %s", errs[0].line, errs[0].message);
    return buf;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int aroma_sandbox_has_error(void)
{
    return IncenseHasFatalError() ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int aroma_sandbox_get_width(void)
{
    if (!g_window) return 400;
    return g_window->rect.width;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int aroma_sandbox_get_height(void)
{
    if (!g_window) return 600;
    return g_window->rect.height;
}

static const char *default_source =
    "Window {\n"
    "    width: 320\n"
    "    height: 480\n"
    "    title: \"Settings\"\n"
    "\n"
    "    Container {\n"
    "        id: \"page_main\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 1\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 120\n"
    "        \n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "                visible: 0\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Settings\"\n"
    "                style: large\n"
    "                y:20\n"
    "                x: 10\n"
    "            }\n"
    "             Textbox {\n"
    "            text: \"\"\n"
    "            placeholder: \"Search settings\"\n"
    "            x: 10\n"
    "            y: 56\n"
    "            width: 300\n"
    "            height: 44\n"
    "            on_change: \"on_search_change\"\n"
    "        }\n"
    "        }\n"
    "\n"
    "       \n"
    "        ListView {\n"
    "            id: \"settings_list\"\n"
    "            x: 0\n"
    "            y: 120\n"
    "            width: 320\n"
    "            height: 360\n"
    "            on_select: \"navigate\"\n"
    "\n"
    "            Header { text: \"General\" }\n"
    "            ListItem { text: \"Airplane Mode\" secondary: \"Off\" icon: \"AROMA_ICON_AIRPLANEMODE_INACTIVE\" }\n"
    "            ListItem { text: \"Wi-Fi\" secondary: \"Not Connected\" icon: \"AROMA_ICON_NETWORK_WIFI\" }\n"
    "            ListItem { text: \"Bluetooth\" secondary: \"On\" icon: \"AROMA_ICON_BLUETOOTH\" }\n"
    "            ListItem { text: \"Cellular\" secondary: \"5G On\" icon: \"AROMA_ICON_SIGNAL_CELLULAR_4_BAR\" }\n"
    "            ListItem { text: \"VPN\" secondary: \"Disconnected\" icon: \"AROMA_ICON_LOCK\" }\n"
    "\n"
    "            Header { text: \"Display & Brightness\" }\n"
    "            ListItem { text: \"Brightness\" secondary: \"50%\" icon: \"AROMA_ICON_BRIGHTNESS_MEDIUM\" }\n"
    "            ListItem { text: \"Auto-Lock\" secondary: \"2 minutes\" icon: \"AROMA_ICON_LOCK\" }\n"
    "            ListItem { text: \"Night Shift\" secondary: \"On until 7 AM\" icon: \"AROMA_ICON_WB_SUNNY\" }\n"
    "            ListItem { text: \"True Tone\" secondary: \"On\" icon: \"AROMA_ICON_BRIGHTNESS_AUTO\" }\n"
    "\n"
    "            Header { text: \"Privacy\" }\n"
    "            ListItem { text: \"Location Services\" secondary: \"While Using the App\" icon: \"AROMA_ICON_LOCATION_CITY\" }\n"
    "            ListItem { text: \"Photos\" secondary: \"All Photos\" icon: \"AROMA_ICON_PHOTO\" }\n"
    "            ListItem { text: \"Camera\" secondary: \"On\" icon: \"AROMA_ICON_PHOTO_CAMERA\" }\n"
    "            ListItem { text: \"Microphone\" secondary: \"On\" icon: \"AROMA_ICON_MIC\" }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_airplane\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "    \n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Airplane Mode\"\n"
    "                style: large\n"
    "                y: 20\n"
    "                x: 60\n"
    "            }\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 56\n"
    "            width: 320\n"
    "            height: 424\n"
    "\n"
    "            ListItem { text: \"Airplane Mode\" secondary: \"Off\" }\n"
    "            ListItem { text: \"My Number\" secondary: \"\" }\n"
    "            ListItem { text: \"Cellular Data Options\" secondary: \"\" }\n"
    "            ListItem { text: \"Cellular Data\" secondary: \"On\" }\n"
    "            ListItem { text: \"Data Roaming\" secondary: \"Off\" }\n"
    "            ListItem { text: \"Voice & Data\" secondary: \"5G Auto\" }\n"
    "            ListItem { text: \"Data Mode\" secondary: \"Allow More Data on 5G\" }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_wifi\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "  \n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Wi-Fi\"\n"
    "                style: large\n"
    "                y: 20\n"
    "                x: 60\n"
    "            }\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 56\n"
    "            width: 320\n"
    "            height: 370\n"
    "\n"
    "            ListItem { text: \"Wi-Fi\" secondary: \"On\" }\n"
    "            ListItem { text: \"Network Name\" secondary: \"Not Connected\" }\n"
    "            ListItem { text: \"Auto-Join\" secondary: \"On\" }\n"
    "            ListItem { text: \"Auto-Lock\" secondary: \"Never\" }\n"
    "            ListItem { text: \"Ask to Join\" secondary: \"On\" }\n"
    "        }\n"
    "\n"
    "        IconButton {\n"
    "            x: 246\n"
    "            y: 394\n"
    "            width: 60\n"
    "            height: 60\n"
    "            icon: \"AROMA_ICON_ADD\"\n"
    "            variant: standard\n"
    "            on_click: \"navigate_wifi_connect\"\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_wifi_connect\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Wi-Fi\"\n"
    "                style: large\n"
    "                y: 20\n"
    "                x: 60\n"
    "            }\n"
    "        }\n"
    "\n"
    "        Container {\n"
    "            x: 20\n"
    "            y: 70\n"
    "            width: 280\n"
    "            height: 340\n"
    "\n"
    "            Textbox {\n"
    "                text: \"\"\n"
    "                placeholder: \"Network Name (SSID)\"\n"
    "                x: 20\n"
    "                y: 30\n"
    "                width: 240\n"
    "                height: 44\n"
    "            }\n"
    "\n"
    "            Textbox {\n"
    "                text: \"\"\n"
    "                placeholder: \"Password\"\n"
    "                x: 20\n"
    "                y: 90\n"
    "                width: 240\n"
    "                height: 44\n"
    "            }\n"
    "\n"
    "            Button {\n"
    "                text: \"Connect to Network\"\n"
    "                x: 40\n"
    "                y: 170\n"
    "                width: 200\n"
    "                height: 44\n"
    "                on_click: \"wifi_connect\"\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_bluetooth\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Bluetooth\"\n"
    "                style: large\n"
    "                y: 20\n"
    "                x: 60 \n"
    "            }\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 56\n"
    "            width: 320\n"
    "            height: 424\n"
    "\n"
    "            ListItem { text: \"Bluetooth\" secondary: \"On\" }\n"
    "            ListItem { text: \"My Devices\" secondary: \"\" }\n"
    "            ListItem { text: \"Other Devices\" secondary: \"\" }\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    ;

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    bool init_ok = aroma_ui_init();
    printf("aroma_ui_init: %s\n", init_ok ? "OK" : "FAILED");
    aroma_animation_manager_init();
    aroma_sandbox_init();

    printf("fonts: text=%p icon=%p\n", (void*)g_text_font, (void*)g_icon_font);

    aroma_sandbox_reload(default_source);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(g_window);
        usleep(16000);
    }

    if (g_window)
    {
        aroma_ui_destroy_window(g_window);
    }
    if (g_registry)
    {
        IncenseFreeRegistry(g_registry);
    }
    aroma_font_destroy(g_text_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_shutdown();
#endif

    return 0;
}