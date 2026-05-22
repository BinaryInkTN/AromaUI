#include "settings_ui.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "ui_animation_utils.h"
#include "theme_manager.h"
#include "tabs_manager.h"
#include "voice_handler.h"
#include "map_panel.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

void open_settings_panel(void *user_data)
{
    (void)user_data;
    if (!state.settings_panel_node || state.settings_panel_open)
        return;

    if (state.map_panel_open)
        close_map_panel(NULL);

    aroma_node_set_hidden(state.settings_panel_node, false);

    animate_node_x(state.settings_panel_node, WIN_W, WIN_W - SETTINGS_PANEL_W);
    animate_node_x(state.vehicle_view_root, 0, -SETTINGS_PANEL_W);

    shift_node(state.overlay, -SETTINGS_PANEL_W);
    shift_node(state.status_card, -SETTINGS_PANEL_W);
    shift_node(state.battery_icon, -SETTINGS_PANEL_W);
    shift_node(state.signal_icon, -SETTINGS_PANEL_W);
    shift_node(state.wifi_icon, -SETTINGS_PANEL_W);
    shift_node(state.gps_icon, -SETTINGS_PANEL_W);
    shift_node(state.bluetooth_icon, -SETTINGS_PANEL_W);
    shift_node(state.voice_button, -SETTINGS_PANEL_W);
    shift_node(state.settings_icon, -SETTINGS_PANEL_W);

    animate_node_x(state.tabs, 0, -(SETTINGS_PANEL_W / 3));

    state.settings_panel_open = true;
}

void close_settings_panel(void *user_data)
{
    (void)user_data;
    if (!state.settings_panel_node || !state.settings_panel_open)
        return;

    animate_node_x(state.settings_panel_node, WIN_W - SETTINGS_PANEL_W, WIN_W);
    animate_node_x(state.vehicle_view_root, -SETTINGS_PANEL_W, 0);

    shift_node(state.overlay, SETTINGS_PANEL_W);
    shift_node(state.status_card, SETTINGS_PANEL_W);
    shift_node(state.battery_icon, SETTINGS_PANEL_W);
    shift_node(state.signal_icon, SETTINGS_PANEL_W);
    shift_node(state.wifi_icon, SETTINGS_PANEL_W);
    shift_node(state.gps_icon, SETTINGS_PANEL_W);
    shift_node(state.bluetooth_icon, SETTINGS_PANEL_W);
    shift_node(state.voice_button, SETTINGS_PANEL_W);
    shift_node(state.settings_icon, SETTINGS_PANEL_W);

    animate_node_x(state.tabs, -(SETTINGS_PANEL_W / 3), 0);

    state.settings_panel_open = false;
}

void settings_button_callback(void *user_data)
{
    (void)user_data;
    if (state.map_panel_open)
        close_map_panel(NULL);

    if (state.settings_panel_open)
        close_settings_panel(NULL);
    else
        open_settings_panel(NULL);
}

static void toggle_tab_animation_cb(AromaNode *sender, void *user_data)
{
    (void)sender;
    (void)user_data;
    static bool is_slide = true;
    is_slide = !is_slide;
    aroma_tabs_set_transition(state.tabs,
        is_slide ? AROMA_ANIM_SLIDE_X : AROMA_ANIM_FADE, 300);
}

static void toggle_voice_assistant_cb(AromaNode *sender, void *user_data)
{
    (void)sender;
    (void)user_data;
    state.g_voice_assistant_enabled = !state.g_voice_assistant_enabled;
}

static void *dev_beep_thread_func(void *arg)
{
    (void)arg;
#ifdef AROMA_USE_VOICE_CONTROL
    system("(speaker-test -t sine -f 1200 -l 1 >/dev/null 2>&1 "
           "& pid=$!; sleep 0.15; kill -9 $pid >/dev/null 2>&1) &");
#endif
    return NULL;
}

static void spawn_dev_beep(void)
{
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, dev_beep_thread_func, NULL);
    pthread_attr_destroy(&attr);
}

void listview_callback(int index, void *user_data)
{
    (void)user_data;
    int selected = aroma_sidebar_get_selected(state.sidebar);

    if (selected == 1 && index == 1) {
        toggle_theme();
    } else if (selected == 6) {
        static int build_clicks = 0;
        if (index == 4) {
            build_clicks++;
            if (build_clicks > 2 && build_clicks < 7) {
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "You are now %d steps away from being a developer.",
                         7 - build_clicks);
                queue_voice_partial(msg);
            } else if (build_clicks >= 7) {
                if (build_clicks == 7) {
                    queue_voice_action(-1, false, false, "You are now a developer!");
                    spawn_dev_beep();
                }
                aroma_node_set_hidden(state.easter_egg_overlay, false);
            }
        } else {
            build_clicks = 0;
        }
    }
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h)
{
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h,
                                      listview_callback, NULL, state.settings_font);
    if (lv)
        aroma_listview_set_icon_font(lv, state.icon_font);
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
    state.settings_panel_open = false;

    int area_w = SETTINGS_PANEL_W - 20;
    int area_h = panel_h - 120;
    int panel_x = sidebar_w + 8;
    int panel_w = area_w - sidebar_w - 8;

    state.settings_root = aroma_container_create(
        state.settings_panel_node, 10, 10, area_w, area_h);
    aroma_node_set_z_index(state.settings_root, Z_LAYER_SETTINGS_PANEL + 1);

    const char *labels[] = {
        "Connectivity", "Display & Theme", "Sound & Media",
        "Navigation", "Vehicle & Climate", "Behaviors",
        "System & About"
    };
    const char *icons[] = {
        AROMA_ICON_WIFI, AROMA_ICON_BRIGHTNESS_HIGH, AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP, AROMA_ICON_DIRECTIONS_CAR, AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO
    };
    int num_sections = 7;

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections,
        NULL, NULL, state.settings_font, state.icon_font);
    aroma_sidebar_set_transition(state.sidebar, AROMA_ANIM_FADE, 200);

    state.listviews[0] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[0], "Wi-Fi", "Connected - AutoNet", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Bluetooth", "1 Device Paired", AROMA_ICON_BLUETOOTH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Mobile data", "5G connection active", AROMA_ICON_NETWORK_CELL, NULL);
    state.listview_containers[0] = aroma_listview_get_scroll_container(state.listviews[0]);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[1], "Brightness level", "Adaptive", AROMA_ICON_BRIGHTNESS_HIGH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Dark theme", "Toggle dark/light", AROMA_ICON_INVERT_COLORS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Auto-rotate screen", "On", AROMA_ICON_SCREEN_ROTATION, NULL);
    state.listview_containers[1] = aroma_listview_get_scroll_container(state.listviews[1]);

    state.listviews[2] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[2], "Media volume", "70%", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Navigation volume", "80%", AROMA_ICON_NAVIGATION, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "System sounds", "On", AROMA_ICON_NOTIFICATIONS, NULL);
    state.listview_containers[2] = aroma_listview_get_scroll_container(state.listviews[2]);

    state.listviews[3] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[3], "Location services", "High accuracy", AROMA_ICON_GPS_FIXED, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Live traffic", "On", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Voice guidance", "On", AROMA_ICON_VOLUME_UP, NULL);
    state.listview_containers[3] = aroma_listview_get_scroll_container(state.listviews[3]);

    state.listviews[4] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[4], "Climate settings", "Auto mode", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Vehicle diagnostics", "All systems normal", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Drive mode", "Comfort", AROMA_ICON_DIRECTIONS_CAR, NULL);
    state.listview_containers[4] = aroma_listview_get_scroll_container(state.listviews[4]);

    state.listviews[5] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[5], "Tab Transition", "Toggle Fade/Slide", AROMA_ICON_SETTINGS, toggle_tab_animation_cb);
    aroma_listview_add_item_with_icon(state.listviews[5], "Voice Assistant", "Enable/Disable", AROMA_ICON_VOLUME_UP, toggle_voice_assistant_cb);
    state.listview_containers[5] = aroma_listview_get_scroll_container(state.listviews[5]);

    state.listviews[6] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    {
        char processor_name[256] = "Unknown";
        FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
        if (cpuinfo) {
            char line[256];
            while (fgets(line, sizeof(line), cpuinfo)) {
                if (strncmp(line, "model name", 10) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        strncpy(processor_name, colon + 2, sizeof(processor_name) - 1);
                        processor_name[sizeof(processor_name) - 1] = '\0';
                        char *nl = strchr(processor_name, '\n');
                        if (nl) *nl = '\0';
                    }
                    break;
                }
            }
            fclose(cpuinfo);
        }

        long total_ram_mb = 0;
        FILE *meminfo = fopen("/proc/meminfo", "r");
        if (meminfo) {
            char line[256];
            while (fgets(line, sizeof(line), meminfo)) {
                if (strncmp(line, "MemTotal", 8) == 0) {
                    long kb;
                    sscanf(line, "MemTotal: %ld kB", &kb);
                    total_ram_mb = kb / 1024;
                    break;
                }
            }
            fclose(meminfo);
        }
        char ram_str[64];
        if (total_ram_mb > 1024) snprintf(ram_str, sizeof(ram_str), "%.1f GB", total_ram_mb / 1024.0);
        else if (total_ram_mb > 0) snprintf(ram_str, sizeof(ram_str), "%ld MB", total_ram_mb);
        else strncpy(ram_str, "NaN", sizeof(ram_str));

        char uptime_str[64] = "Unknown";
        FILE *uptime_file = fopen("/proc/uptime", "r");
        if (uptime_file) {
            double s;
            if (fscanf(uptime_file, "%lf", &s) == 1) {
                int d = (int)(s / 86400),
                    h = (int)((s - d * 86400) / 3600),
                    m = (int)((s - d * 86400 - h * 3600) / 60);
                if (d > 0) snprintf(uptime_str, sizeof(uptime_str), "%d days, %d hrs", d, h);
                else if (h > 0) snprintf(uptime_str, sizeof(uptime_str), "%d hrs, %d min", h, m);
                else snprintf(uptime_str, sizeof(uptime_str), "%d min", m);
            }
            fclose(uptime_file);
        }

        char load_str[64] = "Unknown";
        FILE *loadavg = fopen("/proc/loadavg", "r");
        if (loadavg) {
            double l1, l5, l15;
            if (fscanf(loadavg, "%lf %lf %lf", &l1, &l5, &l15) == 3)
                snprintf(load_str, sizeof(load_str), "%.2f, %.2f, %.2f", l1, l5, l15);
            fclose(loadavg);
        }

        char time_str[64];
        time_t rawtime; time(&rawtime);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&rawtime));

        aroma_listview_add_item_with_icon(state.listviews[6], "Processor", processor_name, AROMA_ICON_MEMORY, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "RAM", ram_str, AROMA_ICON_STORAGE, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Vehicle name", "Aroma Automotive", AROMA_ICON_DIRECTIONS_CAR, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Software", "AromaHMI v0.0.1 / AromaSDK", AROMA_ICON_INFO, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Build date", __DATE__ " " __TIME__, AROMA_ICON_BUILD, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Security patch", "March 1, 2026", AROMA_ICON_SECURITY, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Uptime", uptime_str, AROMA_ICON_ACCESS_TIME, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Load average", load_str, AROMA_ICON_COMPUTER, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Current time", time_str, AROMA_ICON_ACCESS_TIME, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Platform Backend", "GLPS (X11)", AROMA_ICON_VERIFIED_USER, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "CAN interface", CAN_INTERFACE, AROMA_ICON_SETTINGS_INPUT_COMPONENT, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Voice control",
#ifdef AROMA_USE_VOICE_CONTROL
                                          "Enabled (compiled)",
#else
                                          "Disabled (stub)",
#endif
                                          AROMA_ICON_MIC, NULL);
    }
    state.listview_containers[6] = aroma_listview_get_scroll_container(state.listviews[6]);

    for (int i = 0; i < num_sections; i++)
        aroma_sidebar_set_content(state.sidebar, i, &state.listview_containers[i], 1);

    aroma_sidebar_set_selected(state.sidebar, 0);
}