#include "settings_ui.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "ui_animation_utils.h"
#include "theme_manager.h"
#include "tabs_manager.h"
#include "voice_handler.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#define SETTINGS_TAB_SHIFT_DENOM 3

/**
 * shift_ui_chrome - shift all HUD/overlay nodes by @dx pixels.
 *
 * Centralises the 9-node shift that was duplicated verbatim in both
 * open_settings_panel() and close_settings_panel().
 */
static void shift_ui_chrome(int dx)
{
    shift_node(state.overlay, dx);
    shift_node(state.status_card, dx);
    shift_node(state.battery_icon, dx);
    shift_node(state.signal_icon, dx);
    shift_node(state.wifi_icon, dx);
    shift_node(state.gps_icon, dx);
    shift_node(state.bluetooth_icon, dx);
    shift_node(state.voice_button, dx);
    shift_node(state.settings_icon, dx);
}

void open_settings_panel(void *user_data)
{
    UNUSED(user_data);

    if (!state.settings_panel_node || state.settings_panel_open)
        return;


    aroma_node_set_hidden(state.settings_panel_node, false);

    animate_node_x(state.settings_panel_node, WIN_W, WIN_W - SETTINGS_PANEL_W);
    animate_node_x(state.vehicle_view_root, 0, -SETTINGS_PANEL_W);

    shift_ui_chrome(-SETTINGS_PANEL_W);

    animate_node_x(state.tabs, 0, -(SETTINGS_PANEL_W / SETTINGS_TAB_SHIFT_DENOM));

    state.settings_panel_open = true;
}

void close_settings_panel(void *user_data)
{
    UNUSED(user_data);

    if (!state.settings_panel_node || !state.settings_panel_open)
        return;

    animate_node_x(state.settings_panel_node, WIN_W - SETTINGS_PANEL_W, WIN_W);
    animate_node_x(state.vehicle_view_root, -SETTINGS_PANEL_W, 0);

    shift_ui_chrome(SETTINGS_PANEL_W);

    animate_node_x(state.tabs, -(SETTINGS_PANEL_W / SETTINGS_TAB_SHIFT_DENOM), 0);

    state.settings_panel_open = false;
}

void settings_button_callback(void *user_data)
{
    UNUSED(user_data);

    if (state.settings_panel_open)
        close_settings_panel(NULL);
    else
        open_settings_panel(NULL);
}

static void toggle_tab_animation_cb(AromaNode *sender, void *user_data)
{
    UNUSED(sender);
    UNUSED(user_data);

    static bool is_slide = true;
    is_slide = !is_slide;
    aroma_tabs_set_transition(state.tabs,
                              is_slide ? AROMA_ANIM_SLIDE_X : AROMA_ANIM_FADE, 300);
}

static void toggle_voice_assistant_cb(AromaNode *sender, void *user_data)
{
    UNUSED(sender);
    UNUSED(user_data);

    state.g_voice_assistant_enabled = !state.g_voice_assistant_enabled;
}

static void *dev_beep_thread_func(void *arg)
{
    UNUSED(arg);

#ifdef AROMA_USE_VOICE_CONTROL
    pid_t pid = fork();
    if (pid == 0)
    {

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char *const argv[] = {
            "speaker-test", "-t", "sine", "-f", "1200", "-l", "1", NULL};
        execvp("speaker-test", argv);
        _exit(1);
    }
    else if (pid > 0)
    {

        struct timespec ts = {.tv_sec = 0, .tv_nsec = 150000000L};
        nanosleep(&ts, NULL);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
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

typedef void (*item_action_fn)(void);

typedef struct
{
    int section;
    int item;
    item_action_fn action;
} ListItemAction;

static void action_toggle_theme(void) { toggle_theme(); }
static void action_dev_easter_egg(void);

static const ListItemAction item_action_table[] = {
    {1, 1, action_toggle_theme},

};
static const int item_action_table_size =
    (int)(sizeof(item_action_table) / sizeof(item_action_table[0]));

void listview_callback(int index, void *user_data)
{
    UNUSED(user_data);

    int selected = aroma_sidebar_get_selected(state.sidebar);

    if (selected == 6)
    {
        static int build_clicks = 0;

        if (index == 4)
        {
            build_clicks++;

            if (build_clicks > 2 && build_clicks < 7)
            {
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "You are now %d steps away from being a developer.",
                         7 - build_clicks);
                queue_voice_partial(msg);
            }
            else if (build_clicks >= 7)
            {
                if (build_clicks == 7)
                {
                    queue_voice_action(-1, false, false, "You are now a developer!");
                    spawn_dev_beep();
                }
                aroma_node_set_hidden(state.easter_egg_overlay, false);
            }
        }
        else
        {
            build_clicks = 0;
        }
        return;
    }

    for (int i = 0; i < item_action_table_size; i++)
    {
        if (item_action_table[i].section == selected &&
            item_action_table[i].item == index)
        {
            item_action_table[i].action();
            return;
        }
    }
}

static void action_dev_easter_egg(void) {}

static void read_processor_name(char *buf, size_t bufsz)
{
    strncpy(buf, "Unknown", bufsz);
    buf[bufsz - 1] = '\0';

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return;

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "model name", 10) == 0)
        {
            char *colon = strchr(line, ':');
            if (colon)
            {
                strncpy(buf, colon + 2, bufsz - 1);
                buf[bufsz - 1] = '\0';
                char *nl = strchr(buf, '\n');
                if (nl)
                    *nl = '\0';
            }
            break;
        }
    }
    fclose(f);
}

static void read_ram_str(char *buf, size_t bufsz)
{
    long total_ram_mb = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "MemTotal", 8) == 0)
            {
                long kb = 0;
                sscanf(line, "MemTotal: %ld kB", &kb);
                total_ram_mb = kb / 1024;
                break;
            }
        }
        fclose(f);
    }

    if (total_ram_mb > 1024)
        snprintf(buf, bufsz, "%.1f GB", total_ram_mb / 1024.0);
    else if (total_ram_mb > 0)
        snprintf(buf, bufsz, "%ld MB", total_ram_mb);
    else
        strncpy(buf, "Unknown", bufsz);

    buf[bufsz - 1] = '\0';
}

static void read_uptime_str(char *buf, size_t bufsz)
{
    strncpy(buf, "Unknown", bufsz);
    buf[bufsz - 1] = '\0';

    FILE *f = fopen("/proc/uptime", "r");
    if (!f)
        return;

    double s = 0.0;
    if (fscanf(f, "%lf", &s) == 1)
    {
        int d = (int)(s / 86400);
        int h = (int)((s - d * 86400) / 3600);
        int m = (int)((s - d * 86400 - h * 3600) / 60);

        if (d > 0)
            snprintf(buf, bufsz, "%d days, %d hrs", d, h);
        else if (h > 0)
            snprintf(buf, bufsz, "%d hrs, %d min", h, m);
        else
            snprintf(buf, bufsz, "%d min", m);
    }
    fclose(f);
}

static void read_load_str(char *buf, size_t bufsz)
{
    strncpy(buf, "Unknown", bufsz);
    buf[bufsz - 1] = '\0';

    FILE *f = fopen("/proc/loadavg", "r");
    if (!f)
        return;

    double l1, l5, l15;
    if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) == 3)
        snprintf(buf, bufsz, "%.2f, %.2f, %.2f", l1, l5, l15);

    fclose(f);
}

static void read_time_str(char *buf, size_t bufsz)
{
    time_t rawtime;
    time(&rawtime);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", localtime(&rawtime));
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h)
{
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h,
                                      listview_callback, NULL,
                                      state.settings_font);
    if (lv)
        aroma_listview_set_icon_font(lv, state.icon_font);
    return lv;
}

void build_settings_ui(AromaNode *window)
{
    const int panel_h = WIN_H - 80;
    const int sidebar_w = 220;
    const int area_w = SETTINGS_PANEL_W - 20;
    const int area_h = panel_h - 120;
    const int panel_x = sidebar_w + 8;
    const int panel_w = area_w - sidebar_w - 8;

    state.settings_panel_node = aroma_ui_container(
        window, WIN_W, 0, SETTINGS_PANEL_W, panel_h,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.settings_panel_node, Z_LAYER_SETTINGS_PANEL);
    aroma_node_set_hidden(state.settings_panel_node, true);
    state.settings_panel_open = false;

    state.settings_root = aroma_container_create(
        state.settings_panel_node, 10, 10, area_w, area_h);
    aroma_node_set_z_index(state.settings_root, Z_LAYER_SETTINGS_PANEL + 1);

    const char *labels[] = {
        "Connectivity", "Display & Theme", "Sound & Media",
        "Navigation", "Vehicle & Climate", "Behaviors",
        "System & About"};
    const char *icons[] = {
        AROMA_ICON_WIFI, AROMA_ICON_BRIGHTNESS_HIGH, AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP, AROMA_ICON_DIRECTIONS_CAR, AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO};
    const int num_sections = 7;

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

        char processor_name[256], ram_str[64], uptime_str[64],
            load_str[64], time_str[64];

        read_processor_name(processor_name, sizeof(processor_name));
        read_ram_str(ram_str, sizeof(ram_str));
        read_uptime_str(uptime_str, sizeof(uptime_str));
        read_load_str(load_str, sizeof(load_str));
        read_time_str(time_str, sizeof(time_str));

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