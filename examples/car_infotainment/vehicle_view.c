#include "vehicle_view.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "map_panel.h"
#include <stdio.h>

static void ac_temp_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp < 30)
        state.current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16)
        state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

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
    AromaAnimation *anim = aroma_animation_start(
        state.overlay, AROMA_ANIM_SLIDE_Y, 900, 250, 400);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);

    aroma_node_set_hidden(state.vehicle_view_lock_divider, true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_divider, true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_icon, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_header, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_desc, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_divider, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_divider, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_header, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_desc, true);
    aroma_node_set_hidden(state.vehicle_view_lock_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_label, true);
    aroma_node_set_hidden(state.vehicle_view_warning_warning_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_action, true);
    aroma_node_set_hidden(state.battery_image, false);
    aroma_node_set_hidden(state.battery_health, false);
    aroma_node_set_hidden(state.battery_percentage, false);
    aroma_animation_start(state.battery_image, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_health, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_percentage, AROMA_ANIM_FADE, 0, 1, 1000);
}
void build_vehicle_view(AromaNode *window)
{
    state.vehicle_view_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.vehicle_view_root, Z_LAYER_BACKGROUND);

    AromaNode *backroad = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/backroad_blur.png"
#else
        "../assets/backroad_blur.png"
#endif
        ,
        0, 0, WIN_W, WIN_H);
    aroma_node_set_z_index(backroad, Z_LAYER_BACKGROUND);

    AromaNode *car_img = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/car.png"
#else
        "../assets/car.png"
#endif
        ,
        250, 250, 700, 405);
    aroma_node_set_z_index(car_img, Z_LAYER_VEHICLE_IMAGE);

    state.overlay = aroma_ui_image(state.vehicle_view_root, NULL, 250, 250, 700, 405);
    aroma_node_set_z_index(state.overlay, Z_LAYER_VEHICLE_OVERLAYS);

    state.battery_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_BATTERY_FULL,
        WIN_W - 395, 22, 40, ICON_BUTTON_OUTLINED,
        battery_diagnostics, NULL, state.icon_font);
    aroma_node_set_z_index(state.battery_button, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_large_clock = aroma_ui_label(
        state.vehicle_view_root, "12:45",
        WIN_W / 2 - 90, 35, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_large_clock_pm_am = aroma_ui_label(
        state.vehicle_view_root, "PM",
        WIN_W / 2 + 90, 60, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock_pm_am, Z_LAYER_VEHICLE_OVERLAYS + 2);

    AromaNode *location_temp_label = aroma_ui_label(
        state.vehicle_view_root, "68°F, San Francisco",
        WIN_W / 2 - 100, 130, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(location_temp_label, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.gear_bg_card = aroma_ui_card(
        state.vehicle_view_root, 25, 18, 225, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_bg_card, Z_LAYER_VEHICLE_OVERLAYS + 3);

    state.gear_fg_card = aroma_ui_card(state.gear_bg_card, 25, 5, 50, 40, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_fg_card, Z_LAYER_VEHICLE_OVERLAYS + 4);
    aroma_card_set_colors(state.gear_fg_card,
                          state.theme.colors.primary, state.theme.colors.primary);

    AromaNode *lbl_p = aroma_ui_label(state.gear_bg_card, "P", 22, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_r = aroma_ui_label(state.gear_bg_card, "R", 77, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_n = aroma_ui_label(state.gear_bg_card, "N", 132, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_d = aroma_ui_label(state.gear_bg_card, "D", 187, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(lbl_p, Z_LAYER_VEHICLE_OVERLAYS + 5);
    aroma_node_set_z_index(lbl_r, Z_LAYER_VEHICLE_OVERLAYS + 5);
    aroma_node_set_z_index(lbl_n, Z_LAYER_VEHICLE_OVERLAYS + 5);
    aroma_node_set_z_index(lbl_d, Z_LAYER_VEHICLE_OVERLAYS + 5);

    state.speed_label = aroma_ui_label(
        state.vehicle_view_root, "0",
        140, 215, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.speed_label, Z_LAYER_VEHICLE_OVERLAYS + 2);

    AromaNode *kmh_lbl = aroma_ui_label(
        state.vehicle_view_root, "km/h", 155, 305,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(kmh_lbl, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.range_label = aroma_ui_label(
        state.vehicle_view_root, "Range: 0 km",
        WIN_W - 250, 100, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.range_label, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.climate_label = aroma_ui_label(
        state.vehicle_view_root, "Climate: Off",
        WIN_W - 250, 140, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.climate_label, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 400, 340, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_frunk_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_lock_divider = aroma_ui_divider(
        state.vehicle_view_root, 700, 260, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_lock_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_lock_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_LOCK, 712, 220, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_lock_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_header = aroma_ui_label(
        state.vehicle_view_root, "Frunk", 410, 320, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_header, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Open", 410, 345, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_desc, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_trunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 880, 310, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_trunk_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_trunk_header = aroma_ui_label(
        state.vehicle_view_root, "Trunk", 890, 290, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_header, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_trunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Closed", 890, 315, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_desc, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_charge_port_divider = aroma_ui_divider(
        state.vehicle_view_root, 900, 430, 40, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(state.vehicle_view_charge_port_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_charge_port_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_POWER, 970, 415, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_charge_port_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_warning_message_card = aroma_ui_card(
        state.vehicle_view_root, 330, WIN_H + 100, 600, 70, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.vehicle_view_warning_message_card, Z_LAYER_CARDS_BOTTOM + 50);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);

    state.vehicle_view_warning_warning_icon = aroma_ui_icon(
        state.vehicle_view_warning_message_card, AROMA_ICON_WARNING,
        65, 22, 24, 0xFFFFD600, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_warning_warning_icon, Z_LAYER_CARDS_BOTTOM + 51);

    state.vehicle_view_warning_message_label = aroma_ui_label(
        state.vehicle_view_warning_message_card,
        "Warning: The Frunk is Open. Close it before driving.",
        110, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_warning_message_label, Z_LAYER_CARDS_BOTTOM + 51);

    state.battery_image = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/charging.png"
#else
        "../assets/charging.png"
#endif
        ,
        WIN_W / 2 - 180, 200, 128, 128);
    aroma_node_set_z_index(state.battery_image, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_node_set_hidden(state.battery_image, true);

    state.battery_health = aroma_ui_label(
        state.vehicle_view_root, "Battery Health: Good",
        WIN_W / 2 - 20, 220, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_health, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_label_set_color(state.battery_health, 0xFF00C853);
    aroma_node_set_hidden(state.battery_health, true);

    state.battery_percentage = aroma_ui_label(
        state.vehicle_view_root, "85%",
        WIN_W / 2 - 20, 260, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_percentage, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_node_set_hidden(state.battery_percentage, true);

    AromaNode *icons_col = aroma_ui_container(
        state.vehicle_view_root, 50, 100, 28, 300,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(icons_col, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_gap(icons_col, 20);

    AromaNode *high_beams = aroma_ui_image(icons_col,
#ifdef __EMSCRIPTEN__
                                           "/assets/high_beams.png"
#else
                                           "../assets/high_beams.png"
#endif
                                           ,
                                           0, 0, 28, 28);
    aroma_node_set_z_index(high_beams, Z_LAYER_VEHICLE_OVERLAYS + 3);

    AromaNode *low_beams = aroma_ui_image(icons_col,
#ifdef __EMSCRIPTEN__
                                          "/assets/low_beams.png"
#else
                                          "../assets/low_beams.png"
#endif
                                          ,
                                          0, 0, 28, 28);
    aroma_node_set_z_index(low_beams, Z_LAYER_VEHICLE_OVERLAYS + 3);

    AromaNode *abs_icon = aroma_ui_image(icons_col,
#ifdef __EMSCRIPTEN__
                                         "/assets/abs_indicator.png"
#else
                                         "../assets/abs_indicator.png"
#endif
                                         ,
                                         0, 0, 28, 28);
    aroma_node_set_z_index(abs_icon, Z_LAYER_VEHICLE_OVERLAYS + 3);

    AromaNode *brake_icon = aroma_ui_image(icons_col,
#ifdef __EMSCRIPTEN__
                                           "/assets/brake_indicator.png"
#else
                                           "../assets/brake_indicator.png"
#endif
                                           ,
                                           0, 0, 28, 28);
    aroma_node_set_z_index(brake_icon, Z_LAYER_VEHICLE_OVERLAYS + 3);

    state.ac_card = aroma_ui_card((AromaNode *)state.window, 30, WIN_H - 200, 220, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.ac_card, Z_LAYER_MAP_PANEL + 10);

    AromaNode *ac_label = aroma_ui_label(state.ac_card, "Climate", 15, 12, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(ac_label, Z_LAYER_MAP_PANEL + 11);

    state.ac_temp_label = aroma_ui_label(state.ac_card, "23°C", 75, 45, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.ac_temp_label, Z_LAYER_MAP_PANEL + 11);

    AromaNode *ac_temp_down_button = aroma_ui_iconbutton(
        state.ac_card, AROMA_ICON_REMOVE, 15, 55, 40,
        ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_temp_down_button, Z_LAYER_MAP_PANEL + 11);

    AromaNode *ac_temp_up_button = aroma_ui_iconbutton(
        state.ac_card, AROMA_ICON_ADD, 165, 55, 40,
        ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_temp_up_button, Z_LAYER_MAP_PANEL + 11);

    state.music_card = aroma_ui_card((AromaNode *)state.window, WIN_W / 2 - 225, WIN_H - 200, 450, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.music_card, Z_LAYER_MAP_PANEL + 10);

    AromaNode *music_divider = aroma_ui_divider(state.music_card, 0, 60, 450, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(music_divider, Z_LAYER_MAP_PANEL + 11);

    AromaNode *music_label = aroma_ui_label(state.music_card, "Kendrick Lamar - HUMBLE.", 20, 18, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(music_label, Z_LAYER_MAP_PANEL + 11);

    AromaNode *music_row = aroma_ui_container(
        state.music_card, 110, 70, 410, 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(music_row, Z_LAYER_MAP_PANEL + 11);
    aroma_node_set_gap(music_row, 80);

    AromaNode *m_prev = aroma_ui_icon(music_row, AROMA_ICON_SKIP_PREVIOUS, 0, 0, 40,
                                      aroma_color_blend(state.theme.colors.primary, state.theme.colors.surface, 0.5), state.icon_font);
    aroma_node_set_z_index(m_prev, Z_LAYER_MAP_PANEL + 12);

    AromaNode *m_play = aroma_ui_icon(music_row, AROMA_ICON_PLAY_ARROW, 0, 0, 40, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(m_play, Z_LAYER_MAP_PANEL + 12);

    AromaNode *m_next = aroma_ui_icon(music_row, AROMA_ICON_SKIP_NEXT, 0, 0, 40,
                                      aroma_color_blend(state.theme.colors.primary, state.theme.colors.surface, 0.5), state.icon_font);
    aroma_node_set_z_index(m_next, Z_LAYER_MAP_PANEL + 12);

    state.nav_card = aroma_ui_card((AromaNode *)state.window, WIN_W / 2 + 250, WIN_H - 200, 300, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.nav_card, Z_LAYER_MAP_PANEL + 10);

    AromaNode *nav_divider_h = aroma_ui_divider(state.nav_card, 0, 60, 300, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(nav_divider_h, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_divider_v = aroma_ui_divider(state.nav_card, 150, 60, 60, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(nav_divider_v, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_label = aroma_ui_label(state.nav_card, "Navigate", 20, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(nav_label, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_map_icon = aroma_ui_icon(state.nav_card, AROMA_ICON_MAP, 260, 20, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(nav_map_icon, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_home_label = aroma_ui_label(state.nav_card, "Home", 20, 75, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(nav_home_label, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_work_label = aroma_ui_label(state.nav_card, "Work", 170, 75, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(nav_work_label, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_home_icon = aroma_ui_icon(state.nav_card, AROMA_ICON_HOME, 120, 80, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(nav_home_icon, Z_LAYER_MAP_PANEL + 11);

    AromaNode *nav_work_icon = aroma_ui_icon(state.nav_card, AROMA_ICON_WORK, 270, 80, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(nav_work_icon, Z_LAYER_MAP_PANEL + 11);

    AromaAnimation *a1 = aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    AromaAnimation *a2 = aroma_animation_start(state.nav_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    AromaAnimation *a3 = aroma_animation_start(state.ac_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    aroma_animation_set_easing(a1, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(a2, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(a3, AROMA_EASE_OUT_ELASTIC);

    state.vehicle_view_side_arrow_icon_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_MAP,
        WIN_W - 80, WIN_H / 2 - 25, 50,
        ICON_BUTTON_FILLED, open_map_panel, NULL, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_side_arrow_icon_button, Z_LAYER_MAP_BUTTON);

    aroma_animation_start(state.vehicle_view_frunk_divider, AROMA_ANIM_SCALE_Y, 0, 90, 1200);
    aroma_animation_start(state.vehicle_view_trunk_divider, AROMA_ANIM_SCALE_Y, 0, 90, 1200);
    aroma_animation_start(state.vehicle_view_lock_divider, AROMA_ANIM_SCALE_Y, 0, 90, 1200);
    aroma_animation_start(state.vehicle_view_charge_port_divider, AROMA_ANIM_SCALE_X, 0, 40, 1200);
}