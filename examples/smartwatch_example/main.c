#include <aroma.h>
#include <aroma_animation.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#define WATCH_W 380
#define WATCH_H 380

#define WATCH_PADDING 30
#define WATCH_INNER_W (WATCH_W - 2 * WATCH_PADDING)
#define WATCH_INNER_H (WATCH_H - 2 * WATCH_PADDING)

#define Z_WATCH_FACE 1
#define Z_STATS 2
#define Z_WEATHER 2
#define Z_BOTTOM_NAV 5
#define Z_NOTIFICATION 10
#define Z_APPS_PANEL 15
#define Z_APPS_CLOSE 16

typedef struct
{
    AromaFont *ui_font;
    AromaFont *icon_font;
    AromaFont *clock_font;
    AromaFont *small_font;
    AromaFont *tiny_font;

    AromaWindow *window;
    AromaNode *watch_face;
    AromaNode *time_label;
    AromaNode *date_label;
    AromaNode *battery_icon;
    AromaNode *battery_text;
    AromaNode *steps_icon;
    AromaNode *steps_label;
    AromaNode *heart_icon;
    AromaNode *heart_label;
    AromaNode *weather_icon;
    AromaNode *weather_label;

    AromaNode *notif_card;
    AromaNode *notif_icon;
    AromaNode *notif_title;
    AromaNode *notif_body;

    AromaNode *apps_panel;
    AromaNode *apps_close_btn;
    AromaNode *settings_btn;

    AromaTheme theme;
    bool dark_mode;
    bool apps_open;
    bool notif_visible;

    int steps;
    int heart_rate;
    int battery_pct;
} WatchState;

static WatchState watch = {0};

static void toggle_apps(void *user_data)
{
    if (watch.apps_open)
    {
        aroma_animation_start(watch.apps_panel, AROMA_ANIM_SLIDE_Y, 0, WATCH_H, 250);
        watch.apps_open = false;
    }
    else
    {
        aroma_node_set_hidden(watch.apps_panel, false);
        aroma_animation_start(watch.apps_panel, AROMA_ANIM_SLIDE_Y, WATCH_H, 0, 250);
        watch.apps_open = true;
    }
}

static void close_apps(void *user_data)
{
    if (watch.apps_open)
    {
        aroma_animation_start(watch.apps_panel, AROMA_ANIM_SLIDE_Y, 0, WATCH_H, 250);
        watch.apps_open = false;
    }
}

static void toggle_notification(void *user_data)
{
    if (watch.notif_visible)
    {
        aroma_animation_start(watch.notif_card, AROMA_ANIM_SLIDE_Y, WATCH_PADDING + 10, -120, 300);
        watch.notif_visible = false;
    }
    else
    {
        aroma_node_set_hidden(watch.notif_card, false);
        aroma_animation_start(watch.notif_card, AROMA_ANIM_SLIDE_Y, -120, WATCH_PADDING + 10, 300);
        watch.notif_visible = true;
    }
}

static void toggle_dark_mode(void *user_data)
{
    watch.dark_mode = !watch.dark_mode;
    if (watch.dark_mode)
    {
        watch.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
    }
    else
    {
        watch.theme = aroma_theme_create_material_blue();
    }

    aroma_ui_set_theme(&watch.theme);

}

static void open_quick_reply(void *user_data)
{
    aroma_label_set_text(watch.notif_body, "Replying...");
}

void build_watch_face(AromaNode *window)
{
    watch.watch_face = aroma_ui_container(
        window, WATCH_PADDING, WATCH_PADDING, WATCH_INNER_W, WATCH_INNER_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(watch.watch_face, Z_WATCH_FACE);

    watch.time_label = aroma_ui_label(
        watch.watch_face, "10:09",
        WATCH_INNER_W / 2 - 85, 10, LABEL_STYLE_LABEL_LARGE, watch.clock_font);
    aroma_node_set_z_index(watch.time_label, Z_WATCH_FACE + 1);
    aroma_label_set_color(watch.time_label, watch.theme.colors.text_primary);

    watch.date_label = aroma_ui_label(
        watch.watch_face, "Mon, Jan 15",
        WATCH_INNER_W / 2 - 55, 80, LABEL_STYLE_LABEL_MEDIUM, watch.small_font);
    aroma_node_set_z_index(watch.date_label, Z_WATCH_FACE + 1);
    aroma_label_set_color(watch.date_label, watch.theme.colors.text_secondary);

    AromaNode *stats_card = aroma_ui_card(
        watch.watch_face, 0, 125, WATCH_INNER_W, 55, CARD_TYPE_FILLED);
    aroma_node_set_z_index(stats_card, Z_STATS);

    AromaAnimation* stats_anim = aroma_animation_start(stats_card, AROMA_ANIM_SLIDE_Y, -60, 157, 1200);

    watch.steps_icon = aroma_ui_icon(stats_card, AROMA_ICON_DIRECTIONS_WALK, 40, 14, 22,
                                     watch.theme.colors.primary, watch.icon_font);
    aroma_node_set_z_index(watch.steps_icon, Z_STATS + 2);

    watch.steps_label = aroma_ui_label(stats_card, "8,432", 42, 14, LABEL_STYLE_LABEL_MEDIUM, watch.tiny_font);
    aroma_node_set_z_index(watch.steps_label, Z_STATS + 2);
    aroma_label_set_color(watch.steps_label, watch.theme.colors.text_primary);

    watch.heart_icon = aroma_ui_icon(stats_card, AROMA_ICON_FAVORITE, 150, 16, 22,
                                     0xFFE91E63, watch.icon_font);
    aroma_node_set_z_index(watch.heart_icon, Z_STATS + 2);

    watch.heart_label = aroma_ui_label(stats_card, "72 bpm", 152, 16, LABEL_STYLE_LABEL_MEDIUM, watch.tiny_font);
    aroma_node_set_z_index(watch.heart_label, Z_STATS + 2);
    aroma_label_set_color(watch.heart_label, watch.theme.colors.text_primary);

    watch.battery_icon = aroma_ui_icon(stats_card, AROMA_ICON_BATTERY_FULL, 265, 16, 22,
                                       watch.theme.colors.primary, watch.icon_font);
    aroma_node_set_z_index(watch.battery_icon, Z_STATS + 2);

    watch.battery_text = aroma_ui_label(stats_card, "85%", 267, 16, LABEL_STYLE_LABEL_MEDIUM, watch.tiny_font);
    aroma_node_set_z_index(watch.battery_text, Z_STATS + 2);
    aroma_label_set_color(watch.battery_text, watch.theme.colors.text_primary);

    AromaNode *weather_card = aroma_ui_card(
        watch.watch_face, WATCH_INNER_W / 2 - 80, 190, 160, 45, CARD_TYPE_FILLED);
    aroma_node_set_z_index(weather_card, Z_WEATHER);

    watch.weather_icon = aroma_ui_icon(weather_card, AROMA_ICON_WB_SUNNY, 40, 10, 24,
                                       0xFFFFC107, watch.icon_font);
    aroma_node_set_z_index(watch.weather_icon, Z_WEATHER + 2);
    AromaAnimation* weather_anim = aroma_animation_start(weather_card, AROMA_ANIM_SLIDE_Y, -45, 220, 1200);
    aroma_animation_set_easing(weather_anim, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(stats_anim, AROMA_EASE_OUT_ELASTIC);
    watch.weather_label = aroma_ui_label(weather_card, "22°C Sunny", 55, 12, LABEL_STYLE_LABEL_MEDIUM, watch.small_font);
    aroma_node_set_z_index(watch.weather_label, Z_WEATHER + 2);
    aroma_label_set_color(watch.weather_label, watch.theme.colors.text_secondary);

    watch.notif_card = aroma_ui_card(window, WATCH_PADDING, -120, WATCH_INNER_W, 100, CARD_TYPE_FILLED);
    aroma_node_set_z_index(watch.notif_card, Z_NOTIFICATION);
    aroma_node_set_hidden(watch.notif_card, true);
    
    watch.notif_icon = aroma_ui_icon(watch.notif_card, AROMA_ICON_EMAIL, 40, 15, 22,
                                     watch.theme.colors.primary, watch.icon_font);
    aroma_node_set_z_index(watch.notif_icon, Z_NOTIFICATION + 1);

    watch.notif_title = aroma_ui_label(watch.notif_card, "New Message",
                                       55, 12, LABEL_STYLE_LABEL_MEDIUM, watch.small_font);
    aroma_node_set_z_index(watch.notif_title, Z_NOTIFICATION + 1);

    watch.notif_body = aroma_ui_label(watch.notif_card, "Hey! How are you?",
                                      55, 38, LABEL_STYLE_LABEL_MEDIUM, watch.tiny_font);
    aroma_node_set_z_index(watch.notif_body, Z_NOTIFICATION + 1);
    aroma_label_set_color(watch.notif_body, watch.theme.colors.text_secondary);

    AromaNode *notif_dismiss = aroma_ui_iconbutton(watch.notif_card, AROMA_ICON_CLOSE,
                                                   WATCH_INNER_W - 60, 10, 30, ICON_BUTTON_OUTLINED,
                                                   toggle_notification, NULL, watch.icon_font);
    aroma_node_set_z_index(notif_dismiss, Z_NOTIFICATION + 1);

    AromaNode *notif_reply = aroma_ui_iconbutton(watch.notif_card, AROMA_ICON_REPLY,
                                                 WATCH_INNER_W - 60, 50, 30, ICON_BUTTON_FILLED,
                                                 open_quick_reply, NULL, watch.icon_font);
    aroma_node_set_z_index(notif_reply, Z_NOTIFICATION + 1);

    AromaNode *bottom_nav = aroma_ui_container(
        window, WATCH_PADDING, WATCH_H - WATCH_PADDING - 50, WATCH_INNER_W, 50,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_SPACE_AROUND, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(bottom_nav, Z_BOTTOM_NAV);

    AromaNode *apps_btn = aroma_ui_iconbutton(bottom_nav, AROMA_ICON_APPS,
                                              0, 0, 42, ICON_BUTTON_FILLED,
                                              toggle_apps, NULL, watch.icon_font);
    aroma_node_set_z_index(apps_btn, Z_BOTTOM_NAV + 1);

    AromaNode *notif_btn = aroma_ui_iconbutton(bottom_nav, AROMA_ICON_NOTIFICATIONS,
                                               0, 0, 42, ICON_BUTTON_FILLED,
                                               toggle_notification, NULL, watch.icon_font);
    aroma_node_set_z_index(notif_btn, Z_BOTTOM_NAV + 1);

    watch.apps_panel = aroma_ui_card(window, 0, WATCH_H, WATCH_W, WATCH_H - WATCH_PADDING - 55, CARD_TYPE_FILLED);
    aroma_node_set_z_index(watch.apps_panel, Z_APPS_PANEL);
    aroma_node_set_hidden(watch.apps_panel, true);

    watch.apps_close_btn = aroma_ui_iconbutton(watch.apps_panel, AROMA_ICON_CLOSE,
                                               WATCH_W - WATCH_PADDING - 40, 10, 35, ICON_BUTTON_FILLED,
                                               close_apps, NULL, watch.icon_font);
    aroma_node_set_z_index(watch.apps_close_btn, Z_APPS_CLOSE);

    AromaNode *apps_label = aroma_ui_label(watch.apps_panel, "Apps",
                                           20, 15, LABEL_STYLE_LABEL_LARGE, watch.ui_font);
    aroma_node_set_z_index(apps_label, Z_APPS_PANEL + 1);
    aroma_label_set_color(apps_label, watch.theme.colors.text_primary);
;
    AromaNode *apps_grid = aroma_container_create(
        watch.apps_panel, WATCH_PADDING, 55, WATCH_INNER_W, WATCH_H - WATCH_PADDING - 120);
    aroma_node_set_z_index(apps_grid, Z_APPS_PANEL + 1);
    aroma_node_set_layout_mode(apps_grid, AROMA_LAYOUT_MODE_GRID);
    aroma_node_set_grid_cols(apps_grid, 3);
    aroma_node_set_grid_rows(apps_grid, 2);
    aroma_node_set_gap(apps_grid, 8);

    const char *app_icons[] = {AROMA_ICON_PHONE, AROMA_ICON_EMAIL, AROMA_ICON_MAP,
                               AROMA_ICON_MUSIC_NOTE, AROMA_ICON_CAMERA, AROMA_ICON_SETTINGS};
    const char *app_labels[] = {"Phone", "E-Mail", "Maps", "Music", "Camera", "Settings"};
    uint32_t app_colors[] = {0xFF4CAF50, 0xFF2196F3, 0xFFFF9800, 0xFFE91E63, 0xFF9C27B0, 0xFF607D8B};

    for (int i = 0; i < 6; i++)
    {
        AromaNode *app_card = aroma_ui_card(apps_grid, 0, 0, 85, 85, CARD_TYPE_FILLED);
        aroma_node_set_z_index(app_card, Z_APPS_PANEL + 2);

        AromaNode *icon_bg = aroma_ui_card(app_card, 22, 10, 40, 40, CARD_TYPE_FILLED);
        aroma_card_set_colors(icon_bg, app_colors[i], app_colors[i]);
        aroma_node_set_z_index(icon_bg, Z_APPS_PANEL + 3);

        AromaNode *app_icon = aroma_ui_icon(icon_bg, app_icons[i], 32, 8, 24,
                                            0xFFFFFFFF, watch.icon_font);
        aroma_node_set_z_index(app_icon, Z_APPS_PANEL + 4);

        AromaNode *app_label = aroma_ui_label(app_card, app_labels[i], 23, 58,
                                              LABEL_STYLE_LABEL_MEDIUM, watch.tiny_font);
        aroma_node_set_z_index(app_label, Z_APPS_PANEL + 3);
        aroma_label_set_color(app_label, watch.theme.colors.text_secondary);
    }
}

int main(int argc, char **argv)
{
    aroma_animation_manager_init();
    aroma_ui_init();

    watch.dark_mode = false;
    watch.apps_open = false;
    watch.notif_visible = false;
    watch.steps = 8432;
    watch.heart_rate = 72;
    watch.battery_pct = 85;

    watch.theme = aroma_theme_create_material_black();
    watch.theme.enable_shadows = true;
    //material blue
    watch.theme.colors.primary = 0xFF2196F3; 
    watch.theme.colors.primary_dark = aroma_color_blend(watch.theme.colors.primary, 0xFF000000, 0.2f);

    aroma_ui_set_theme(&watch.theme);

    watch.ui_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
    watch.icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    watch.clock_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 56);
    watch.small_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    watch.tiny_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 13);

    watch.window = aroma_ui_create_window("Aroma Watch", WATCH_W, WATCH_H);
    aroma_event_set_root((AromaNode *)watch.window);
    aroma_ui_prepare_font_for_window(0, watch.ui_font);

    build_watch_face((AromaNode *)watch.window);

    uint64_t last_update = aroma_time_now_ms();

    while (aroma_ui_is_running())
    {
        uint64_t now = aroma_time_now_ms();

        if (now - last_update > 1000)
        {
            time_t rawtime = time(NULL);
            struct tm *ti = localtime(&rawtime);
            char time_str[16], date_str[32];
            strftime(time_str, sizeof(time_str), "%H:%M", ti);
            strftime(date_str, sizeof(date_str), "%a, %b %d", ti);
            aroma_label_set_text(watch.time_label, time_str);
            aroma_label_set_text(watch.date_label, date_str);
            last_update = now;
        }

        aroma_ui_process_events();
        aroma_ui_render(watch.window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    aroma_ui_destroy_window(watch.window);
    aroma_ui_unload_font(watch.ui_font);
    aroma_ui_unload_font(watch.icon_font);
    aroma_ui_unload_font(watch.clock_font);
    aroma_ui_unload_font(watch.small_font);
    aroma_ui_unload_font(watch.tiny_font);
    aroma_ui_shutdown();
    return 0;
}