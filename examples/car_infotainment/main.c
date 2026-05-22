#include "aroma.h"
#include "aroma_animation.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#define WIN_W 1280
#define WIN_H 800

// Layer definitions
#define Z_LAYER_BACKGROUND       1
#define Z_LAYER_VEHICLE_IMAGE    2
#define Z_LAYER_VEHICLE_OVERLAYS 3
#define Z_LAYER_CARDS_BOTTOM     10
#define Z_LAYER_MAP_PANEL        20
#define Z_LAYER_MAP_BUTTON       15
#define Z_LAYER_MAP_CONTROLS     21
#define Z_LAYER_MAP_CLOSE        22
#define Z_LAYER_STATUS_BAR       100
#define Z_LAYER_STATUS_ICONS     101
#define Z_LAYER_SETTINGS_PANEL   150

#define MAP_PANEL_WIDTH  WIN_W
#define MAP_PANEL_OFFSET 0
#define SETTINGS_PANEL_W 800
#define SETTINGS_ANIM_MS 350

// Application state
typedef struct {
    AromaFont *icon_font;
    AromaFont *ui_font;
    AromaFont *tab_font;
    AromaFont *settings_font;
    AromaFont *clock_font;
    AromaFont *clock_pm_am_font;

    AromaWindow *window;
    AromaTheme theme;
    bool dark_theme_enabled;

    // UI elements
    AromaNode *vehicle_view_root;
    AromaNode *tabs;
    AromaNode *sidebar;
    AromaNode *time_label;
    AromaNode *location_label;
    AromaNode *status_card;
    AromaNode *signal_icon;
    AromaNode *wifi_icon;
    AromaNode *battery_icon;
    AromaNode *gps_icon;
    AromaNode *bluetooth_icon;
    AromaNode *settings_icon;

    // Vehicle view
    AromaNode *overlay;
    AromaNode *speed_label;
    AromaNode *range_label;
    AromaNode *climate_label;
    AromaNode *gear_bg_card;
    AromaNode *gear_fg_card;
    AromaNode *battery_image;
    AromaNode *battery_health;
    AromaNode *battery_percentage;
    AromaNode *ac_card;
    AromaNode *music_card;
    AromaNode *nav_card;
    AromaNode *ac_temp_label;
    AromaNode *vehicle_view_large_clock;
    AromaNode *vehicle_view_large_clock_pm_am;
    AromaNode *vehicle_view_warning_message_card;
    AromaNode *vehicle_view_warning_message_label;

    // Map
    AromaNode *map_node;
    AromaNode *map_panel;
    AromaNode *map_overlay_background;
    AromaNode *recent_lv;
    bool map_panel_open;

    // Settings
    AromaNode *settings_panel_node;
    AromaNode *settings_root;
    AromaNode *listviews[8];
    AromaNode *listview_containers[8];
    bool settings_panel_open;

    // Simulated vehicle state
    double speed;
    int gear;
    double range;
    double soc;
    double cabin_temp;
    double target_temp;
    int fan_speed;
    int hvac_on;
    int current_ac_temp;
} AppState;

static AppState state = {0};

// Helper functions
static void animate_node_x(AromaNode *node, int from, int to)
{
    if (!node) return;
    AromaAnimation *anim = aroma_animation_start(
        node, AROMA_ANIM_SLIDE_X, from, to, SETTINGS_ANIM_MS);
    if (anim)
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static int get_node_x(AromaNode *node)
{
    if (!node || !node->node_widget_ptr) return 0;
    return ((AromaRect *)node->node_widget_ptr)->x;
}

static void shift_node(AromaNode *node, int delta)
{
    if (!node) return;
    int x = get_node_x(node);
    animate_node_x(node, x, x + delta);
}

// Map panel
static void map_zoom_in_cb(void *user_data)
{
    if (user_data) aroma_map_zoom_in((AromaNode *)user_data);
}

static void map_zoom_out_cb(void *user_data)
{
    if (user_data) aroma_map_zoom_out((AromaNode *)user_data);
}

static void navigate_map(int index, void *user_data)
{
    AromaNode *map = (AromaNode *)user_data;
    if (!map) return;
    aroma_map_clear_route(map);

    static const struct {
        double lat, lon;
        const char *start_label, *end_label;
        double end_lat, end_lon;
        int zoom;
    } routes[] = {
        { 48.8566,  2.3522, "Start: Paris",       "Home: Versailles",  48.8049,  2.1204, 12 },
        { 51.5074, -0.1278, "Start: London",      "Work: Heathrow",    51.4700, -0.4543, 11 },
        { 52.5200, 13.4050, "Start: Berlin",      "Gym: BER Airport",  52.3667, 13.5033, 11 },
        { 41.9028, 12.4964, "Start: Colosseum",   "Supermarket: FCO",  41.7999, 12.2462, 12 },
        { 48.1351, 11.5820, "Start: Marienplatz", "Cafe: MUC Airport", 48.3537, 11.7861, 11 },
    };
    
    if (index < 0 || index >= 5) return;

    aroma_map_pan_to(map, routes[index].lat, routes[index].lon);
    aroma_map_set_zoom(map, routes[index].zoom);
    aroma_map_set_route(map, routes[index].lat, routes[index].lon, 
                       routes[index].end_lat, routes[index].end_lon, 0xFF35A8FE);
    aroma_map_add_popup_marker(map, routes[index].lat, routes[index].lon, 
                              0xFF00C853, routes[index].start_label);
    aroma_map_add_popup_marker(map, routes[index].end_lat, routes[index].end_lon, 
                              0xFFD50000, routes[index].end_label);
}

void open_map_panel(void *user_data)
{
    (void)user_data;
    if (!state.map_panel || state.map_panel_open) return;

    if (state.settings_panel_open) {
        // Close settings first
        animate_node_x(state.settings_panel_node, WIN_W - SETTINGS_PANEL_W, WIN_W);
        animate_node_x(state.vehicle_view_root, -SETTINGS_PANEL_W, 0);
        animate_node_x(state.tabs, -(SETTINGS_PANEL_W / 3), 0);
        state.settings_panel_open = false;
    }

    aroma_node_set_hidden(state.map_overlay_background, false);
    aroma_node_set_hidden(state.map_panel, false);
    
    AromaAnimation *map_anim = aroma_animation_start(
        state.map_panel, AROMA_ANIM_SLIDE_X, WIN_W, MAP_PANEL_OFFSET, 450);
    aroma_animation_start(state.recent_lv, AROMA_ANIM_SLIDE_X, WIN_W, MAP_PANEL_OFFSET, 350);
    aroma_animation_set_easing(map_anim, AROMA_EASE_OUT_CUBIC);
    state.map_panel_open = true;
}

void close_map_panel(void *user_data)
{
    (void)user_data;
    if (!state.map_panel || !state.map_panel_open) return;

    aroma_node_set_hidden(state.map_overlay_background, true);
    AromaAnimation *map_anim = aroma_animation_start(
        state.map_panel, AROMA_ANIM_SLIDE_X, MAP_PANEL_OFFSET, WIN_W, 450);
    aroma_animation_set_easing(map_anim, AROMA_EASE_OUT_CUBIC);
    state.map_panel_open = false;
}

// Settings panel
static void open_settings_panel(void *user_data)
{
    (void)user_data;
    if (!state.settings_panel_node || state.settings_panel_open) return;

    if (state.map_panel_open) close_map_panel(NULL);

    aroma_node_set_hidden(state.settings_panel_node, false);
    animate_node_x(state.settings_panel_node, WIN_W, WIN_W - SETTINGS_PANEL_W);
    animate_node_x(state.vehicle_view_root, 0, -SETTINGS_PANEL_W);
    shift_node(state.status_card, -SETTINGS_PANEL_W);
    shift_node(state.battery_icon, -SETTINGS_PANEL_W);
    shift_node(state.signal_icon, -SETTINGS_PANEL_W);
    shift_node(state.wifi_icon, -SETTINGS_PANEL_W);
    shift_node(state.gps_icon, -SETTINGS_PANEL_W);
    shift_node(state.bluetooth_icon, -SETTINGS_PANEL_W);
    shift_node(state.settings_icon, -SETTINGS_PANEL_W);
    animate_node_x(state.tabs, 0, -(SETTINGS_PANEL_W / 3));
    state.settings_panel_open = true;
}

void close_settings_panel(void *user_data)
{
    (void)user_data;
    if (!state.settings_panel_node || !state.settings_panel_open) return;

    animate_node_x(state.settings_panel_node, WIN_W - SETTINGS_PANEL_W, WIN_W);
    animate_node_x(state.vehicle_view_root, -SETTINGS_PANEL_W, 0);
    shift_node(state.status_card, SETTINGS_PANEL_W);
    shift_node(state.battery_icon, SETTINGS_PANEL_W);
    shift_node(state.signal_icon, SETTINGS_PANEL_W);
    shift_node(state.wifi_icon, SETTINGS_PANEL_W);
    shift_node(state.gps_icon, SETTINGS_PANEL_W);
    shift_node(state.bluetooth_icon, SETTINGS_PANEL_W);
    shift_node(state.settings_icon, SETTINGS_PANEL_W);
    animate_node_x(state.tabs, -(SETTINGS_PANEL_W / 3), 0);
    state.settings_panel_open = false;
}

static void settings_button_callback(void *user_data)
{
    (void)user_data;
    if (state.map_panel_open) close_map_panel(NULL);
    if (state.settings_panel_open) close_settings_panel(NULL);
    else open_settings_panel(NULL);
}

// AC controls
static void ac_temp_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp < 30) state.current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16) state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

// Battery diagnostics
static void battery_diagnostics(void *user_data)
{
    (void)user_data;
    aroma_image_set_source(state.overlay,
        #ifdef __EMSCRIPTEN__
            "/assets/car_battery.png"
        #else
        "../assets/car_battery.png"
        #endif
    );
    aroma_animation_start(state.overlay, AROMA_ANIM_SLIDE_Y, 900, 250, 400);
    aroma_node_set_hidden(state.battery_image, false);
    aroma_node_set_hidden(state.battery_health, false);
    aroma_node_set_hidden(state.battery_percentage, false);
    aroma_animation_start(state.battery_image, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_health, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_percentage, AROMA_ANIM_FADE, 0, 1, 1000);
}

// Settings listview callback
static void listview_callback(int index, void *user_data)
{
    (void)user_data;
    int selected = aroma_sidebar_get_selected(state.sidebar);

    if (selected == 1 && index == 1) {
        state.dark_theme_enabled = !state.dark_theme_enabled;
        if (state.dark_theme_enabled) {
            state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
        } else {
            state.theme = aroma_theme_create_high_contrast();
            state.theme.colors.primary = 0xFF2196F3;
            state.theme.colors.primary_dark = 0xFF1976D2;
            state.theme.colors.primary_light = 0xFFBBDEFB;
        }
        state.theme.enable_shadows = false;
        aroma_ui_set_theme(&state.theme);
    }
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h)
{
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h, listview_callback, NULL, state.settings_font);
    if (lv) aroma_listview_set_icon_font(lv, state.icon_font);
    return lv;
}

void build_settings_ui(AromaNode *window)
{
    int panel_h = WIN_H - 80;
    int sidebar_w = 220;

    state.settings_panel_node = aroma_ui_container(
        window, WIN_W, 0, SETTINGS_PANEL_W, panel_h,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.settings_panel_node, Z_LAYER_SETTINGS_PANEL);
    aroma_node_set_hidden(state.settings_panel_node, true);

    int area_w = SETTINGS_PANEL_W - 20;
    int area_h = panel_h - 120;
    int panel_x = sidebar_w + 8;
    int panel_w = area_w - sidebar_w - 8;

    state.settings_root = aroma_container_create(
        state.settings_panel_node, 10, 10, area_w, area_h);

    const char *labels[] = {"Connectivity", "Display & Theme", "Sound & Media",
                            "Navigation", "Vehicle & Climate", "Behaviors", "System & About"};
    const char *icons[] = {AROMA_ICON_WIFI, AROMA_ICON_BRIGHTNESS_HIGH, AROMA_ICON_VOLUME_UP,
                           AROMA_ICON_MAP, AROMA_ICON_DIRECTIONS_CAR, AROMA_ICON_SETTINGS, AROMA_ICON_INFO};
    int num_sections = 7;

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections, NULL, NULL, state.settings_font, state.icon_font);

    state.listviews[0] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[0], "Wi-Fi", "Connected", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Bluetooth", "1 Device", AROMA_ICON_BLUETOOTH, NULL);
    state.listview_containers[0] = aroma_listview_get_scroll_container(state.listviews[0]);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[1], "Brightness", "Adaptive", AROMA_ICON_BRIGHTNESS_HIGH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Dark theme", "Toggle", AROMA_ICON_INVERT_COLORS, NULL);
    state.listview_containers[1] = aroma_listview_get_scroll_container(state.listviews[1]);

    state.listviews[2] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[2], "Volume", "70%", AROMA_ICON_VOLUME_UP, NULL);
    state.listview_containers[2] = aroma_listview_get_scroll_container(state.listviews[2]);

    state.listviews[3] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[3], "Location", "High accuracy", AROMA_ICON_GPS_FIXED, NULL);
    state.listview_containers[3] = aroma_listview_get_scroll_container(state.listviews[3]);

    state.listviews[4] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[4], "Climate", "Auto", AROMA_ICON_DIRECTIONS_CAR, NULL);
    state.listview_containers[4] = aroma_listview_get_scroll_container(state.listviews[4]);

    state.listviews[5] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[5], "Tab Animation", "Fade/Slide", AROMA_ICON_SETTINGS, NULL);
    state.listview_containers[5] = aroma_listview_get_scroll_container(state.listviews[5]);

    state.listviews[6] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[6], "Software", "AromaHMI v0.0.1", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Build", __DATE__ " " __TIME__, AROMA_ICON_BUILD, NULL);
    state.listview_containers[6] = aroma_listview_get_scroll_container(state.listviews[6]);

    for (int i = 0; i < num_sections; i++)
        aroma_sidebar_set_content(state.sidebar, i, &state.listview_containers[i], 1);

    aroma_sidebar_set_selected(state.sidebar, 0);
}

// Vehicle view
void build_vehicle_view(AromaNode *window)
{
    state.vehicle_view_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);

    // Background
    AromaNode *backroad = aroma_ui_image(
        state.vehicle_view_root,
        #ifdef __EMSCRIPTEN__
        "/assets/backroad_blur.png"
        #else
        "../assets/backroad_blur.png"
        #endif
        , 0, 0, WIN_W, WIN_H);
    aroma_node_set_z_index(backroad, Z_LAYER_BACKGROUND);

    // Car image
    AromaNode *car_img = aroma_ui_image(
        state.vehicle_view_root,
        #ifdef __EMSCRIPTEN__
        "/assets/car.png"
        #else
        "../assets/car.png"
        #endif
        , 250, 250, 700, 405);
    aroma_node_set_z_index(car_img, Z_LAYER_VEHICLE_IMAGE);

    state.overlay = aroma_ui_image(state.vehicle_view_root, NULL, 250, 250, 700, 405);
    aroma_node_set_z_index(state.overlay, Z_LAYER_VEHICLE_OVERLAYS);

    // Battery button
    aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_BATTERY_FULL,
        WIN_W - 395, 22, 40, ICON_BUTTON_OUTLINED,
        battery_diagnostics, NULL, state.icon_font);

    // Clock
    state.vehicle_view_large_clock = aroma_ui_label(
        state.vehicle_view_root, "12:45",
        WIN_W / 2 - 90, 35, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_large_clock_pm_am = aroma_ui_label(
        state.vehicle_view_root, "PM",
        WIN_W / 2 + 90, 60, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock_pm_am, Z_LAYER_VEHICLE_OVERLAYS);

    // Location
    aroma_ui_label(state.vehicle_view_root, "68°F, San Francisco",
        WIN_W / 2 - 100, 130, LABEL_STYLE_LABEL_LARGE, state.ui_font);

    // Gear selector
    state.gear_bg_card = aroma_ui_card(state.vehicle_view_root, 25, 18, 225, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_bg_card, Z_LAYER_VEHICLE_OVERLAYS);

    state.gear_fg_card = aroma_ui_card(state.gear_bg_card, 25, 5, 50, 40, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_fg_card, Z_LAYER_VEHICLE_OVERLAYS + 1);
    aroma_card_set_colors(state.gear_fg_card, state.theme.colors.primary, state.theme.colors.primary);

    aroma_ui_label(state.gear_bg_card, "P", 22, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_ui_label(state.gear_bg_card, "R", 77, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_ui_label(state.gear_bg_card, "N", 132, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_ui_label(state.gear_bg_card, "D", 187, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);

    // Speed
    state.speed_label = aroma_ui_label(
        state.vehicle_view_root, "0", 140, 215, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.speed_label, Z_LAYER_VEHICLE_OVERLAYS);

    aroma_ui_label(state.vehicle_view_root, "km/h", 155, 305, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    // Range
    state.range_label = aroma_ui_label(
        state.vehicle_view_root, "Range: 420 km", WIN_W - 250, 100, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.range_label, Z_LAYER_VEHICLE_OVERLAYS);

    // Climate
    state.climate_label = aroma_ui_label(
        state.vehicle_view_root, "Inside: 22.0°C | AC: 23.0°C (Auto)", WIN_W - 250, 140, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.climate_label, Z_LAYER_VEHICLE_OVERLAYS);

    // Car indicators
    aroma_ui_divider(state.vehicle_view_root, 400, 340, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_ui_divider(state.vehicle_view_root, 700, 260, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_ui_icon(state.vehicle_view_root, AROMA_ICON_LOCK, 712, 220, 24, state.theme.colors.primary, state.icon_font);
    aroma_ui_label(state.vehicle_view_root, "Frunk", 410, 320, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_ui_label(state.vehicle_view_root, "Open", 410, 345, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_ui_divider(state.vehicle_view_root, 880, 310, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_ui_label(state.vehicle_view_root, "Trunk", 890, 290, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_ui_label(state.vehicle_view_root, "Closed", 890, 315, LABEL_STYLE_LABEL_LARGE, state.ui_font);

    // Warning card
    state.vehicle_view_warning_message_card = aroma_ui_card(
        state.vehicle_view_root, 330, WIN_H + 100, 600, 70, CARD_TYPE_FILLED);
    state.vehicle_view_warning_message_label = aroma_ui_label(
        state.vehicle_view_warning_message_card, "", 110, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_warning_message_card, Z_LAYER_CARDS_BOTTOM + 50);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);

    // Battery diagnostics images
    state.battery_image = aroma_ui_image(
        state.vehicle_view_root,
        #ifdef __EMSCRIPTEN__
        "/assets/charging.png"
        #else
        "../assets/charging.png"
        #endif
        , WIN_W / 2 - 180, 200, 128, 128);
    state.battery_health = aroma_ui_label(
        state.vehicle_view_root, "Battery Health: Good", WIN_W / 2 - 20, 220, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    state.battery_percentage = aroma_ui_label(
        state.vehicle_view_root, "85%", WIN_W / 2 - 20, 260, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_health, Z_LAYER_VEHICLE_OVERLAYS);
    aroma_node_set_z_index(state.battery_percentage, Z_LAYER_VEHICLE_OVERLAYS);
    aroma_node_set_hidden(state.battery_image, true);
    aroma_node_set_hidden(state.battery_health, true);
    aroma_node_set_hidden(state.battery_percentage, true);

    // AC Card
    state.ac_card = aroma_ui_card((AromaNode *)state.window, 30, WIN_H - 200, 220, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.ac_card, Z_LAYER_MAP_PANEL + 1);
    aroma_ui_label(state.ac_card, "Climate", 15, 12, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    state.ac_temp_label = aroma_ui_label(state.ac_card, "23°C", 75, 45, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.ac_temp_label, Z_LAYER_MAP_PANEL + 2);
    aroma_ui_iconbutton(state.ac_card, AROMA_ICON_REMOVE, 15, 55, 40, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    aroma_ui_iconbutton(state.ac_card, AROMA_ICON_ADD, 165, 55, 40, ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);

    // Music Card
    state.music_card = aroma_ui_card((AromaNode *)state.window, WIN_W / 2 - 225, WIN_H - 200, 450, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.music_card, Z_LAYER_MAP_PANEL + 1);
    aroma_ui_divider(state.music_card, 0, 60, 450, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_ui_label(state.music_card, "Kendrick Lamar - HUMBLE.", 20, 18, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *music_row = aroma_ui_container(state.music_card, 110, 70, 410, 40, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap(music_row, 80);
    aroma_ui_icon(music_row, AROMA_ICON_SKIP_PREVIOUS, 0, 0, 40, aroma_color_blend(state.theme.colors.primary, state.theme.colors.surface, 0.5), state.icon_font);
    aroma_ui_icon(music_row, AROMA_ICON_PLAY_ARROW, 0, 0, 40, state.theme.colors.primary, state.icon_font);
    aroma_ui_icon(music_row, AROMA_ICON_SKIP_NEXT, 0, 0, 40, aroma_color_blend(state.theme.colors.primary, state.theme.colors.surface, 0.5), state.icon_font);

    // Nav Card
    state.nav_card = aroma_ui_card((AromaNode *)state.window, WIN_W / 2 + 250, WIN_H - 200, 300, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.nav_card, Z_LAYER_MAP_PANEL + 1);
    aroma_ui_divider(state.nav_card, 0, 60, 300, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_ui_divider(state.nav_card, 150, 60, 60, DIVIDER_ORIENTATION_VERTICAL);
    aroma_ui_label(state.nav_card, "Navigate", 20, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_ui_label(state.nav_card, "Home", 20, 75, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_ui_label(state.nav_card, "Work", 170, 75, LABEL_STYLE_LABEL_LARGE, state.ui_font);

    // Animate cards
    AromaAnimation *a1 = aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    AromaAnimation *a2 = aroma_animation_start(state.nav_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    AromaAnimation *a3 = aroma_animation_start(state.ac_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    aroma_animation_set_easing(a1, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(a2, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(a3, AROMA_EASE_OUT_ELASTIC);

    // Map button
    aroma_ui_iconbutton(state.vehicle_view_root, AROMA_ICON_MAP,
        WIN_W - 80, WIN_H / 2 - 25, 50, ICON_BUTTON_FILLED, open_map_panel, NULL, state.icon_font);
}

// Map panel
void build_map_panel(AromaNode *window)
{
    state.map_overlay_background = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H, AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.map_overlay_background, Z_LAYER_MAP_PANEL - 1);
    aroma_node_set_hidden(state.map_overlay_background, true);

    state.map_panel = aroma_ui_container(
        window, MAP_PANEL_OFFSET, 0, MAP_PANEL_WIDTH, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.map_panel, Z_LAYER_MAP_PANEL);
    aroma_node_set_hidden(state.map_panel, true);

    state.map_node = aroma_ui_map(state.map_panel, 0, 0, MAP_PANEL_WIDTH, WIN_H);
    aroma_node_set_z_index(state.map_node, Z_LAYER_MAP_PANEL);
    aroma_map_set_show_attribution(state.map_node, false);
    aroma_map_set_center(state.map_node, 48.8566, 2.3522);
    aroma_map_set_zoom(state.map_node, 12);

    // Zoom controls
    AromaNode *zoom_in = aroma_ui_iconbutton(state.map_panel, AROMA_ICON_ADD, MAP_PANEL_WIDTH - 70, 120, 50, ICON_BUTTON_FILLED, map_zoom_in_cb, (void *)state.map_node, state.icon_font);
    AromaNode *zoom_out = aroma_ui_iconbutton(state.map_panel, AROMA_ICON_REMOVE, MAP_PANEL_WIDTH - 70, 180, 50, ICON_BUTTON_FILLED, map_zoom_out_cb, (void *)state.map_node, state.icon_font);
    aroma_button_set_colors(zoom_in, state.theme.colors.primary, state.theme.colors.primary, state.theme.colors.secondary, state.theme.colors.text_primary);
    aroma_button_set_colors(zoom_out, state.theme.colors.primary, state.theme.colors.primary, state.theme.colors.secondary, state.theme.colors.text_primary);
    aroma_node_set_z_index(zoom_in, Z_LAYER_MAP_CONTROLS);
    aroma_node_set_z_index(zoom_out, Z_LAYER_MAP_CONTROLS);

    // Recent places
    AromaNode *recent_card = aroma_ui_card(state.map_panel, WIN_W - 390, WIN_H - 450, 300, 280, CARD_TYPE_FILLED);
    aroma_node_set_z_index(recent_card, Z_LAYER_MAP_CONTROLS);
    aroma_ui_label(recent_card, "Recently Visited", 20, 20, LABEL_STYLE_LABEL_LARGE, state.ui_font);

    state.recent_lv = aroma_ui_listview(recent_card, 0, 60, 300, 200, navigate_map, state.map_node, state.ui_font);
    aroma_listview_add_item(state.recent_lv, "Home", "123 Main St", NULL);
    aroma_listview_add_item(state.recent_lv, "Work", "456 Business Rd", NULL);
    aroma_listview_add_item(state.recent_lv, "Gym", "789 Fitness Ave", NULL);

    // Close button
    aroma_ui_iconbutton(state.map_panel, AROMA_ICON_CLOSE, 20, 20, 50, ICON_BUTTON_FILLED, close_map_panel, NULL, state.icon_font);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    aroma_animation_manager_init();

    char build_info[256];
    snprintf(build_info, sizeof(build_info), "AromaOS v0.0.1 - Build: %s %s", __DATE__, __TIME__);
    aroma_splash(false, "AromaOS", build_info);
    
    aroma_ui_init();

    // Theme
    state.theme = aroma_theme_create_material_blue();
    state.theme.enable_shadows = false;
    state.theme.colors.background = aroma_color_blend(state.theme.colors.primary, state.theme.colors.background, 0.96f);
    aroma_ui_set_theme(&state.theme);

    // Fonts
    state.ui_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    state.icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    state.tab_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 128);
    state.clock_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 68);
    state.clock_pm_am_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    state.settings_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);

    // Window
    state.window = aroma_ui_create_window("Automotive HMI", WIN_W, WIN_H);
    aroma_event_set_root((AromaNode *)state.window);
    aroma_ui_prepare_font_for_window(0, state.ui_font);

    // Status bar
    state.time_label = aroma_ui_label((AromaNode *)state.window, "12:45 PM", 50, 30, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.time_label, Z_LAYER_STATUS_BAR);
    state.location_label = aroma_ui_label((AromaNode *)state.window, "San Francisco, 68°F", 150, 30, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.location_label, Z_LAYER_STATUS_BAR);

    state.status_card = aroma_ui_card((AromaNode *)state.window, WIN_W - 235, 18, 200, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.status_card, Z_LAYER_STATUS_BAR);

    state.signal_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.wifi_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_WIFI, WIN_W - 80, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.battery_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BATTERY_FULL, WIN_W - 40, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.gps_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_GPS_FIXED, WIN_W - 160, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.bluetooth_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BLUETOOTH_AUDIO, WIN_W - 200, 30, 24, state.theme.colors.text_primary, state.icon_font);

    AromaNode *status_icons[] = {state.signal_icon, state.wifi_icon, state.battery_icon, state.gps_icon, state.bluetooth_icon};
    for (int i = 0; i < 5; i++) aroma_node_set_z_index(status_icons[i], Z_LAYER_STATUS_ICONS);

    state.settings_icon = aroma_ui_iconbutton((AromaNode *)state.window, AROMA_ICON_SETTINGS, WIN_W - 345, 22, 40, ICON_BUTTON_OUTLINED, settings_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.settings_icon, Z_LAYER_STATUS_ICONS);

    // Build UI
    build_vehicle_view((AromaNode *)state.window);
    build_map_panel((AromaNode *)state.window);
    build_settings_ui((AromaNode *)state.window);

    // Tabs
    state.tabs = aroma_ui_tabs_with_icons(
        (AromaNode *)state.window, 0, WIN_H - 80, WIN_W, 80,
        (const char *[]){"Vehicle View", "Settings"},
        (const char *[]){AROMA_ICON_VISIBILITY, AROMA_ICON_SETTINGS},
        2, NULL, NULL, state.ui_font, state.tab_font);
    aroma_node_set_z_index(state.tabs, Z_LAYER_MAP_BUTTON);
    aroma_tabs_set_content(state.tabs, 0, (AromaNode **)&state.vehicle_view_root, 1);
    aroma_tabs_set_content(state.tabs, 1, &state.settings_panel_node, 1);

    // Init state
    state.current_ac_temp = 23;
    state.speed = 0;
    state.gear = 0;
    state.range = 420;
    state.soc = 85;
    state.cabin_temp = 22.0;
    state.target_temp = 23.0;
    state.fan_speed = 3;
    state.hvac_on = 1;

    // Main loop
    uint64_t last_time_update = 0;
    
    while (aroma_ui_is_running())
    {
        uint64_t now = aroma_time_now_ms();

        // Update clock
        if (now - last_time_update > 30000) {
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            if (timeinfo) {
                char clock_str[16];
                strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
                aroma_label_set_text(state.vehicle_view_large_clock, clock_str);
            }
            last_time_update = now;
        }

        // Update displays
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", state.speed);
        aroma_label_set_text(state.speed_label, buf);
        
        snprintf(buf, sizeof(buf), "Range: %.0f km", state.range);
        aroma_label_set_text(state.range_label, buf);
        
        snprintf(buf, sizeof(buf), "%.0f%%", state.soc);
        aroma_label_set_text(state.battery_percentage, buf);

        if (state.hvac_on) {
            snprintf(buf, sizeof(buf), "Inside: %.1f°C | AC: %.1f°C (Auto)", state.cabin_temp, state.target_temp);
        } else {
            snprintf(buf, sizeof(buf), "Inside: %.1f°C | AC Off", state.cabin_temp);
        }
        aroma_label_set_text(state.climate_label, buf);

        snprintf(buf, sizeof(buf), "%.1f°C, San Francisco", state.cabin_temp);
        aroma_label_set_text(state.location_label, buf);

        // Simulate vehicle data
        state.speed += 0.1;
        if (state.speed > 120) state.speed = 0;
        state.gear = (state.speed > 5) ? 3 : 0;
        state.range -= 0.001;
        if (state.range < 100) state.range = 420;

        // Process events and render
        aroma_ui_process_events();
        aroma_ui_render(state.window);
        
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    // Cleanup
    aroma_ui_unload_font(state.ui_font);
    aroma_ui_unload_font(state.icon_font);
    aroma_ui_unload_font(state.tab_font);
    aroma_ui_unload_font(state.clock_font);
    aroma_ui_unload_font(state.clock_pm_am_font);
    aroma_ui_unload_font(state.settings_font);
    aroma_ui_shutdown();
    
    return 0;
}