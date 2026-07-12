#include "shm_reader.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <aroma.h>
#include <aroma_animation.h>

#define ACCEL_GRAPH_CENTER_X 160.0f
#define ACCEL_GRAPH_CENTER_Y 416.0f
#define ACCEL_GRAPH_RADIUS_PX 100.0f
#define ACCEL_RANGE_MS2 8.0f
#define ACCEL_NODE_W 26.0f
#define ACCEL_NODE_H 26.0f

#define LATERAL_SIGN 1.0f
#define LONGITUDINAL_SIGN -1.0f

#define ACCEL_ANIM_DURATION_MS 120
#define ACCEL_MOVE_EPS_PX 0.75f

#define ERROR_FLASH_INTERVAL_TICKS 5
#define INDICATOR_FLASH_INTERVAL_TICKS 5
#define WARNING_FLASH_INTERVAL_TICKS 8

#define ICON_COLOR_LEFT 0xFF4CAF50
#define ICON_COLOR_RIGHT 0xFF4CAF50
#define ICON_COLOR_DIMMED 0xFF4A4A4A
#define ICON_COLOR_WARNING 0xFFFF9800
#define ICON_COLOR_DANGER 0xFFFF0000
#define ICON_COLOR_INFO 0xFF2196F3
#define ICON_COLOR_WHITE 0xFFFFFFFF

static bool get_flag(uint16_t flags, int bit)
{
    return (flags >> bit) & 1;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float accel_to_target_x(float lateral_ms2)
{
    float norm = clampf(lateral_ms2 / ACCEL_RANGE_MS2, -1.0f, 1.0f);
    float offset_px = LATERAL_SIGN * norm * ACCEL_GRAPH_RADIUS_PX;
    return (ACCEL_GRAPH_CENTER_X + offset_px) - (ACCEL_NODE_W / 2.0f);
}

static float accel_to_target_y(float longitudinal_ms2)
{
    float norm = clampf(longitudinal_ms2 / ACCEL_RANGE_MS2, -1.0f, 1.0f);
    float offset_px = LONGITUDINAL_SIGN * norm * ACCEL_GRAPH_RADIUS_PX;
    return (ACCEL_GRAPH_CENTER_Y + offset_px) - (ACCEL_NODE_H / 2.0f);
}

int main()
{
    aroma_ui_init();
    aroma_animation_manager_init();

    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 120);
    AromaFont *speed_unit_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
    AromaFont *big_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 28);
    AromaFont *small_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    AromaFont *tiny_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 11);
    AromaFont *time_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 22);
    AromaFont *icon_font_big = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 64);
    AromaFont *icon_font_medium = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 30);
    AromaFont *icon_font_small = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 18);
    AromaFont *icon_font_strip = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 56);

    AromaWindow *window = aroma_ui_create_window("Telemetry Dashboard", 1024, 600);
    AromaTheme theme = aroma_theme_create_material_black();
    // material blue
    theme.colors.primary = 0xFF2196F3;
    aroma_ui_set_theme(&theme);
    shm_reader_t *reader = shm_reader_init("/sdv_telemetry_shm");

    AromaNode *temp_value = aroma_ui_label((AromaNode *)window, "-- C", 20, 12, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_node_set_z_index(temp_value, 10);

    AromaNode *time_value = aroma_ui_label((AromaNode *)window, "--:--", 130, 12, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_node_set_z_index(time_value, 10);

    AromaNode *indicator_l = aroma_ui_icon((AromaNode *)window, AROMA_ICON_KEYBOARD_ARROW_LEFT, 400, 4, 48, ICON_COLOR_DIMMED, icon_font_strip);
    aroma_node_set_z_index(indicator_l, 10);
    AromaNode *indicator_r = aroma_ui_icon((AromaNode *)window, AROMA_ICON_KEYBOARD_ARROW_RIGHT, 700, 4, 48, ICON_COLOR_DIMMED, icon_font_strip);
    aroma_node_set_z_index(indicator_r, 10);

    AromaNode *warning_banner = aroma_ui_card((AromaNode *)window, 710, 4, 300, 48, CARD_TYPE_FILLED);
    AromaNode *warning_banner_icon = aroma_ui_icon((AromaNode *)warning_banner, AROMA_ICON_WARNING, 35, 10, 28, ICON_COLOR_WARNING, icon_font_small);
    AromaNode *warning_banner_label = aroma_ui_label((AromaNode *)warning_banner, "", 48, 14, LABEL_STYLE_LABEL_MEDIUM, small_font);
    aroma_node_set_z_index(warning_banner, 10);
    aroma_node_set_z_index(warning_banner_icon, 11);
    aroma_node_set_z_index(warning_banner_label, 11);
    aroma_node_set_hidden(warning_banner, true);

    AromaNode *speed_bg = aroma_ui_card((AromaNode *)window, 20, 72, 280, 220, CARD_TYPE_FILLED);
    AromaNode *label_speed = aroma_ui_label((AromaNode *)window, "60", 60, 100, LABEL_STYLE_LABEL_LARGE, text_font);
    AromaNode *label_speed_unit = aroma_ui_label((AromaNode *)window, "km/h", 220, 200, LABEL_STYLE_LABEL_MEDIUM, speed_unit_font);

    AromaNode *accel_card = aroma_ui_card((AromaNode *)window, 20, 306, 280, 220, CARD_TYPE_OUTLINED);

    AromaNode *accel_h_line = aroma_ui_divider((AromaNode *)window, 30, 416, 260, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(accel_h_line, 1);
    AromaNode *accel_v_line = aroma_ui_divider((AromaNode *)window, 160, 326, 180, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(accel_v_line, 1);

    AromaNode *accel_value_label = aroma_ui_label((AromaNode *)window, "0.0 m/s", 110, 516, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    aroma_node_set_hidden(accel_value_label, true);
    AromaNode *accel_node = aroma_ui_card(
        (AromaNode *)window,
        (int)(ACCEL_GRAPH_CENTER_X - ACCEL_NODE_W / 2.0f),
        (int)(ACCEL_GRAPH_CENTER_Y - ACCEL_NODE_H / 2.0f),
        (int)ACCEL_NODE_W, (int)ACCEL_NODE_H,
        CARD_TYPE_FILLED);
    aroma_node_set_z_index(accel_node, 5);

    float accel_node_x = ACCEL_GRAPH_CENTER_X - ACCEL_NODE_W / 2.0f;
    float accel_node_y = ACCEL_GRAPH_CENTER_Y - ACCEL_NODE_H / 2.0f;

    AromaNode *critical_overlay = aroma_ui_card((AromaNode *)window, 20, 72, 280, 454, CARD_TYPE_FILLED);
    AromaNode *critical_icon = aroma_ui_icon((AromaNode *)critical_overlay, AROMA_ICON_ERROR, 170, 140, 64, ICON_COLOR_DANGER, icon_font_big);
    AromaNode *critical_label = aroma_ui_label((AromaNode *)critical_overlay, "", 40, 230, LABEL_STYLE_LABEL_LARGE, big_font);
    AromaNode *critical_desc = aroma_ui_label((AromaNode *)critical_overlay, "", 50, 270, LABEL_STYLE_LABEL_MEDIUM, small_font);
    aroma_node_set_hidden(critical_overlay, true);

    int gx = 320;
    int gy = 75;
    int cw = 100;
    int ch = 92;
    int gap = 14;
    int isz = 40;
    int icx = (cw - isz) / 2;
    int icy = 10;

    AromaNode *engine_card = aroma_ui_card((AromaNode *)window, gx, gy, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *engine_icon = aroma_ui_icon((AromaNode *)engine_card, AROMA_ICON_SETTINGS, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *engine_label = aroma_ui_label((AromaNode *)window, "Engine", gx+10, gy+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *engine_value = aroma_ui_label((AromaNode *)window, "OK", gx+10, gy+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int x1 = gx + cw + gap;
    AromaNode *abs_card = aroma_ui_card((AromaNode *)window, x1, gy, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *abs_icon = aroma_ui_icon((AromaNode *)abs_card, AROMA_ICON_WARNING, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *abs_label = aroma_ui_label((AromaNode *)window, "ABS", x1+10, gy+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *abs_value = aroma_ui_label((AromaNode *)window, "OK", x1+10, gy+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int x2 = x1 + cw + gap;
    AromaNode *battery_card = aroma_ui_card((AromaNode *)window, x2, gy, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *battery_icon = aroma_ui_icon((AromaNode *)battery_card, AROMA_ICON_BATTERY_FULL, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *battery_label = aroma_ui_label((AromaNode *)window, "Battery", x2+10, gy+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *battery_value = aroma_ui_label((AromaNode *)window, "85%", x2+10, gy+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int x3 = x2 + cw + gap;
    AromaNode *doors_card = aroma_ui_card((AromaNode *)window, x3, gy, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *doors_icon = aroma_ui_icon((AromaNode *)doors_card, AROMA_ICON_LOCK_OPEN, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *doors_label = aroma_ui_label((AromaNode *)window, "Doors", x3+10, gy+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *doors_value = aroma_ui_label((AromaNode *)window, "Closed", x3+10, gy+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int x4 = x3 + cw + gap;
    AromaNode *seatbelt_card = aroma_ui_card((AromaNode *)window, x4, gy, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *seatbelt_icon = aroma_ui_icon((AromaNode *)seatbelt_card, AROMA_ICON_PERSON, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *seatbelt_label = aroma_ui_label((AromaNode *)window, "Seatbelt", x4+10, gy+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *seatbelt_value = aroma_ui_label((AromaNode *)window, "OK", x4+10, gy+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int x5 = x4 + cw + gap;
    AromaNode *wiper_card = aroma_ui_card((AromaNode *)window, x5, gy, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *wiper_icon = aroma_ui_icon((AromaNode *)wiper_card, AROMA_ICON_INVERT_COLORS, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *wiper_label = aroma_ui_label((AromaNode *)window, "Wipers", x5+10, gy+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *wiper_value = aroma_ui_label((AromaNode *)window, "Off", x5+10, gy+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int gy2 = gy + ch + gap;
    AromaNode *gps_card = aroma_ui_card((AromaNode *)window, gx, gy2, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *gps_icon = aroma_ui_icon((AromaNode *)gps_card, AROMA_ICON_GPS_FIXED, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *gps_label = aroma_ui_label((AromaNode *)window, "GPS", gx+10, gy2+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *gps_value = aroma_ui_label((AromaNode *)window, "8 sat", gx+10, gy2+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *speed_icon_card = aroma_ui_card((AromaNode *)window, x1, gy2, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *speed_icon = aroma_ui_icon((AromaNode *)speed_icon_card, AROMA_ICON_TRENDING_UP, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *speed_icon_label = aroma_ui_label((AromaNode *)window, "Speed", x1+10, gy2+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *speed_icon_value = aroma_ui_label((AromaNode *)window, "50 km/h", x1+10, gy2+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *accel_icon_card = aroma_ui_card((AromaNode *)window, x2, gy2, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *accel_icon = aroma_ui_icon((AromaNode *)accel_icon_card, AROMA_ICON_TIMELINE, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *accel_icon_label = aroma_ui_label((AromaNode *)window, "Accel", x2+10, gy2+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *accel_icon_value = aroma_ui_label((AromaNode *)window, "0.0", x2+10, gy2+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *cpu_card = aroma_ui_card((AromaNode *)window, x3, gy2, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *cpu_icon = aroma_ui_icon((AromaNode *)cpu_card, AROMA_ICON_MEMORY, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *cpu_label = aroma_ui_label((AromaNode *)window, "CPU", x3+10, gy2+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *cpu_value = aroma_ui_label((AromaNode *)window, "25%", x3+10, gy2+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *fault_card = aroma_ui_card((AromaNode *)window, x4, gy2, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *fault_icon = aroma_ui_icon((AromaNode *)fault_card, AROMA_ICON_ERROR, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *fault_label = aroma_ui_label((AromaNode *)window, "Faults", x4+10, gy2+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *fault_value = aroma_ui_label((AromaNode *)window, "None", x4+10, gy2+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *temp_icon_card = aroma_ui_card((AromaNode *)window, x5, gy2, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *temp_icon = aroma_ui_icon((AromaNode *)temp_icon_card, AROMA_ICON_WB_SUNNY, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *temp_icon_label = aroma_ui_label((AromaNode *)window, "Temp", x5+10, gy2+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *temp_icon_value = aroma_ui_label((AromaNode *)window, "22C", x5+10, gy2+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    int gy3 = gy2 + ch + gap;
    AromaNode *sched_card = aroma_ui_card((AromaNode *)window, gx, gy3, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *sched_icon = aroma_ui_icon((AromaNode *)sched_card, AROMA_ICON_SCHEDULE, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *sched_label = aroma_ui_label((AromaNode *)window, "Sched", gx+10, gy3+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *sched_value = aroma_ui_label((AromaNode *)window, "0", gx+10, gy3+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *miss_card = aroma_ui_card((AromaNode *)window, x1, gy3, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *miss_icon = aroma_ui_icon((AromaNode *)miss_card, AROMA_ICON_CANCEL, icx, icy, isz, ICON_COLOR_DIMMED, icon_font_medium);
    AromaNode *miss_label = aroma_ui_label((AromaNode *)window, "Misses", x1+10, gy3+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *miss_value = aroma_ui_label((AromaNode *)window, "0", x1+10, gy3+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *uptime_card = aroma_ui_card((AromaNode *)window, x2, gy3, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *uptime_icon = aroma_ui_icon((AromaNode *)uptime_card, AROMA_ICON_TIMER, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *uptime_label = aroma_ui_label((AromaNode *)window, "Uptime", x2+10, gy3+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *uptime_value = aroma_ui_label((AromaNode *)window, "0s", x2+10, gy3+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    AromaNode *humid_card = aroma_ui_card((AromaNode *)window, x3, gy3, cw, ch, CARD_TYPE_OUTLINED);
    AromaNode *humid_icon = aroma_ui_icon((AromaNode *)humid_card, AROMA_ICON_OPACITY, icx, icy, isz, ICON_COLOR_INFO, icon_font_medium);
    AromaNode *humid_label = aroma_ui_label((AromaNode *)window, "Humid", x3+10, gy3+56, LABEL_STYLE_LABEL_MEDIUM, tiny_font);
    AromaNode *humid_value = aroma_ui_label((AromaNode *)window, "45%", x3+10, gy3+72, LABEL_STYLE_LABEL_MEDIUM, tiny_font);

    sdv_telemetry_t state;
    memset(&state, 0, sizeof(state));
    bool state_valid = false;
    bool error_icon_visible = true;
    int error_flash_tick = 0;
    bool indicator_lit = true;
    int indicator_flash_tick = 0;
    bool warning_flash = true;
    int warning_flash_tick = 0;

    aroma_node_invalidate((AromaNode *)window);
    while (aroma_ui_is_running())
    {
        sdv_telemetry_t new_state;
        if (shm_reader_get_state(reader, &new_state))
        {
            state = new_state;
            state_valid = true;

            char speed_text[16];
            snprintf(speed_text, sizeof(speed_text), "%d", (int)(state.veh_speed_x10 / 10));
            aroma_label_set_text(label_speed, speed_text);

            char temp_text[16];
            snprintf(temp_text, sizeof(temp_text), "%.1f C", state.env_temp_x10 / 10.0f);
            aroma_label_set_text(temp_value, temp_text);

            float accel_ms2 = state.veh_accel_x100 / 100.0f;
            char accel_text[16];
            snprintf(accel_text, sizeof(accel_text), "%.1f m/s", accel_ms2);
            aroma_label_set_text(accel_value_label, accel_text);

            char cpu_text[16];
            snprintf(cpu_text, sizeof(cpu_text), "%.1f%%", state.cpu_load_x100 / 100.0f);
            aroma_label_set_text(cpu_value, cpu_text);

            char uptime_text[16];
            snprintf(uptime_text, sizeof(uptime_text), "%ds", (int)(state.uptime_ms / 1000));
            aroma_label_set_text(uptime_value, uptime_text);

            char humid_text[16];
            snprintf(humid_text, sizeof(humid_text), "%.1f%%", state.env_hum_x100 / 100.0f);
            aroma_label_set_text(humid_value, humid_text);

            char miss_text[16];
            snprintf(miss_text, sizeof(miss_text), "%d", state.total_misses);
            aroma_label_set_text(miss_value, miss_text);

            char sched_text[16];
            snprintf(sched_text, sizeof(sched_text), "%d", state.sched_mode);
            aroma_label_set_text(sched_value, sched_text);

            char temp_icon_text[16];
            snprintf(temp_icon_text, sizeof(temp_icon_text), "%.1fC", state.env_temp_x10 / 10.0f);
            aroma_label_set_text(temp_icon_value, temp_icon_text);

            char speed_icon_text[16];
            snprintf(speed_icon_text, sizeof(speed_icon_text), "%d km/h", (int)(state.veh_speed_x10 / 10));
            aroma_label_set_text(speed_icon_value, speed_icon_text);

            char accel_icon_text[16];
            snprintf(accel_icon_text, sizeof(accel_icon_text), "%.1f", accel_ms2);
            aroma_label_set_text(accel_icon_value, accel_icon_text);

            const char *acm_status_text = "OK";
            uint32_t acm_color = ICON_COLOR_LEFT;
            if (state.acm_status == 3 || state.acm_status == 4) {
                acm_status_text = "FAULT";
                acm_color = ICON_COLOR_DANGER;
            } else if (state.acm_status == 2) {
                acm_status_text = "DEGR";
                acm_color = ICON_COLOR_WARNING;
            }
          
            float target_x = accel_to_target_x(0.0f);
            float target_y = accel_to_target_y(accel_ms2);

            if (fabsf(target_x - accel_node_x) > ACCEL_MOVE_EPS_PX) {
                aroma_animation_stop(accel_node);
                aroma_animation_start(accel_node, AROMA_ANIM_SLIDE_X, accel_node_x, target_x, ACCEL_ANIM_DURATION_MS);
                accel_node_x = target_x;
            }
            if (fabsf(target_y - accel_node_y) > ACCEL_MOVE_EPS_PX) {
                aroma_animation_stop(accel_node);
                aroma_animation_start(accel_node, AROMA_ANIM_SLIDE_Y, accel_node_y, target_y, ACCEL_ANIM_DURATION_MS);
                accel_node_y = target_y;
            }

            float accel_magnitude = fabsf(accel_ms2);
            if (accel_magnitude > 6.0f) {
                aroma_card_set_colors(accel_node, ICON_COLOR_DANGER, ICON_COLOR_DANGER);
            } else if (accel_magnitude > 3.0f) {
                aroma_card_set_colors(accel_node, ICON_COLOR_WARNING, ICON_COLOR_WARNING);
            } else {
                aroma_card_set_colors(accel_node, ICON_COLOR_INFO, ICON_COLOR_INFO);
            }
        }

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_text[16];
        strftime(time_text, sizeof(time_text), "%H:%M", tm_info);
        aroma_label_set_text(time_value, time_text);

        if (state_valid)
        {
            bool door_open = get_flag(state.veh_flags, 1);
            bool engine_fault = get_flag(state.veh_flags, 2);
            bool abs_fault = get_flag(state.veh_flags, 3);
            bool battery_low = get_flag(state.veh_flags, 4);
            bool indicator_l_flag = get_flag(state.veh_flags, 5);
            bool indicator_r_flag = get_flag(state.veh_flags, 6);
            bool crash = get_flag(state.veh_flags, 7);
            bool airbag = get_flag(state.veh_flags, 8);
            bool seatbelt_warn = get_flag(state.veh_flags, 9);
            bool battery_warn = get_flag(state.veh_flags, 10);
            bool harsh_braking = get_flag(state.veh_flags, 12);
            bool hard_accel_flag = get_flag(state.veh_flags, 13);

            indicator_flash_tick++;
            if (indicator_flash_tick >= INDICATOR_FLASH_INTERVAL_TICKS)
            {
                indicator_flash_tick = 0;
                indicator_lit = !indicator_lit;
            }

            warning_flash_tick++;
            if (warning_flash_tick >= WARNING_FLASH_INTERVAL_TICKS)
            {
                warning_flash_tick = 0;
                warning_flash = !warning_flash;
            }

            aroma_icon_set_color(indicator_l, indicator_l_flag && indicator_lit ? ICON_COLOR_LEFT : ICON_COLOR_DIMMED);
            aroma_icon_set_color(indicator_r, indicator_r_flag && indicator_lit ? ICON_COLOR_RIGHT : ICON_COLOR_DIMMED);

            if (engine_fault) {
                aroma_icon_set_color(engine_icon, ICON_COLOR_DANGER);
                aroma_label_set_text(engine_value, "FAULT");
            } else {
                aroma_icon_set_color(engine_icon, ICON_COLOR_LEFT);
                aroma_label_set_text(engine_value, "OK");
            }

            if (abs_fault) {
                aroma_icon_set_color(abs_icon, ICON_COLOR_WARNING);
                aroma_label_set_text(abs_value, "FAULT");
            } else {
                aroma_icon_set_color(abs_icon, ICON_COLOR_LEFT);
                aroma_label_set_text(abs_value, "OK");
            }

            if (battery_low) {
                aroma_icon_set_color(battery_icon, warning_flash ? ICON_COLOR_WARNING : ICON_COLOR_DIMMED);
            } else if (battery_warn) {
                aroma_icon_set_color(battery_icon, ICON_COLOR_WARNING);
            } else {
                aroma_icon_set_color(battery_icon, ICON_COLOR_LEFT);
            }

            if (door_open) {
                aroma_icon_set_color(doors_icon, ICON_COLOR_DANGER);
                aroma_label_set_text(doors_value, "OPEN");
            } else {
                aroma_icon_set_color(doors_icon, ICON_COLOR_LEFT);
                aroma_label_set_text(doors_value, "Closed");
            }

            if (seatbelt_warn) {
                aroma_icon_set_color(seatbelt_icon, ICON_COLOR_DANGER);
                aroma_label_set_text(seatbelt_value, "OFF");
            } else {
                aroma_icon_set_color(seatbelt_icon, ICON_COLOR_LEFT);
                aroma_label_set_text(seatbelt_value, "OK");
            }

            const char *wiper_text;
            uint32_t wiper_color;
            switch (state.bcm_wiper_speed) {
                case 0: wiper_text = "Off"; wiper_color = ICON_COLOR_DIMMED; break;
                case 1: wiper_text = "Low"; wiper_color = ICON_COLOR_INFO; break;
                case 2: wiper_text = "High"; wiper_color = ICON_COLOR_INFO; break;
                default: wiper_text = "--"; wiper_color = ICON_COLOR_DIMMED; break;
            }
            aroma_label_set_text(wiper_value, wiper_text);
            aroma_icon_set_color(wiper_icon, wiper_color);

            char gps_text[16];
            snprintf(gps_text, sizeof(gps_text), "%d sat", state.gps_satellites);
            aroma_label_set_text(gps_value, gps_text);
            if (state.gps_satellites >= 4) {
                aroma_icon_set_color(gps_icon, ICON_COLOR_INFO);
            } else if (state.gps_satellites > 0) {
                aroma_icon_set_color(gps_icon, ICON_COLOR_WARNING);
            } else {
                aroma_icon_set_color(gps_icon, ICON_COLOR_DANGER);
            }

            if (state.fault_flags != 0) {
                char fault_text[16];
                snprintf(fault_text, sizeof(fault_text), "0x%02X", state.fault_flags);
                aroma_label_set_text(fault_value, fault_text);
                aroma_icon_set_color(fault_icon, ICON_COLOR_WARNING);
            } else {
                aroma_label_set_text(fault_value, "None");
                aroma_icon_set_color(fault_icon, ICON_COLOR_DIMMED);
            }

            if (state.total_misses > 0) {
                aroma_icon_set_color(miss_icon, ICON_COLOR_DANGER);
            } else {
                aroma_icon_set_color(miss_icon, ICON_COLOR_DIMMED);
            }

            if (state.cpu_load_x100 > 8000) {
                aroma_icon_set_color(cpu_icon, ICON_COLOR_DANGER);
            } else if (state.cpu_load_x100 > 5000) {
                aroma_icon_set_color(cpu_icon, ICON_COLOR_WARNING);
            } else {
                aroma_icon_set_color(cpu_icon, ICON_COLOR_INFO);
            }

            bool any_critical = crash || airbag || seatbelt_warn || door_open;

            if (any_critical)
            {
                const char *title;
                const char *desc;
                if (crash) {
                    title = "Crash Detected!";
                    desc = "Emergency services notified.";
                } else if (airbag) {
                    title = "Airbag Deployed!";
                    desc = "Remain still, wait for help.";
                } else if (seatbelt_warn) {
                    title = "Fasten Seatbelt!";
                    desc = "Seatbelt is not fastened.";
                } else {
                    title = "Doors Are Open!";
                    desc = "Close all doors to continue.";
                }
                aroma_label_set_text(critical_label, title);
                aroma_label_set_text(critical_desc, desc);
                aroma_node_set_hidden(critical_overlay, false);
                aroma_node_set_hidden(speed_bg, true);
                aroma_node_set_hidden(accel_card, true);
                aroma_node_set_hidden(accel_node, true);
                aroma_node_set_hidden(accel_h_line, true);
                aroma_node_set_hidden(accel_v_line, true);
                aroma_node_set_hidden(accel_value_label, true);

                error_flash_tick++;
                if (error_flash_tick >= ERROR_FLASH_INTERVAL_TICKS) {
                    error_flash_tick = 0;
                    error_icon_visible = !error_icon_visible;
                    aroma_node_set_hidden(critical_icon, !error_icon_visible);
                }
            }
            else
            {
                aroma_node_set_hidden(critical_overlay, true);
                aroma_node_set_hidden(speed_bg, false);
                aroma_node_set_hidden(accel_card, false);
                aroma_node_set_hidden(accel_node, false);
                aroma_node_set_hidden(accel_h_line, false);
                aroma_node_set_hidden(accel_v_line, false);
                aroma_node_set_hidden(accel_value_label, true);
                error_flash_tick = 0;
                error_icon_visible = true;
                aroma_node_set_hidden(critical_icon, false);
            }

            if (any_critical)
            {
                aroma_node_invalidate((AromaNode *)window);
                aroma_ui_process_events();
                aroma_ui_render(window);
                usleep(100000);
                continue;
            }

            float accel_ms2 = state.veh_accel_x100 / 100.0f;
            bool hard_accel = accel_ms2 >= 5.0f || hard_accel_flag;

            if (hard_accel) {
                aroma_node_set_hidden(warning_banner, false);
                aroma_label_set_text(warning_banner_label, "Hard Acceleration!");
                aroma_icon_set_color(warning_banner_icon, ICON_COLOR_WARNING);
            } else if (harsh_braking) {
                aroma_node_set_hidden(warning_banner, false);
                aroma_label_set_text(warning_banner_label, "Harsh Braking!");
                aroma_icon_set_color(warning_banner_icon, ICON_COLOR_DANGER);
            } else if (engine_fault) {
                aroma_node_set_hidden(warning_banner, false);
                aroma_label_set_text(warning_banner_label, "Engine Fault!");
                aroma_icon_set_color(warning_banner_icon, ICON_COLOR_DANGER);
            } else if (abs_fault) {
                aroma_node_set_hidden(warning_banner, false);
                aroma_label_set_text(warning_banner_label, "ABS Fault!");
                aroma_icon_set_color(warning_banner_icon, ICON_COLOR_WARNING);
            } else if (battery_warn || battery_low) {
                aroma_node_set_hidden(warning_banner, false);
                aroma_label_set_text(warning_banner_label, "Battery Warning!");
                aroma_icon_set_color(warning_banner_icon, ICON_COLOR_WARNING);
            } else {
                aroma_node_set_hidden(warning_banner, true);
            }
        }

        aroma_node_invalidate((AromaNode *)window);
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(100000);
    }

    aroma_animation_cleanup_all();
    aroma_ui_destroy_window(window);
    aroma_ui_unload_font(text_font);
    aroma_ui_unload_font(speed_unit_font);
    aroma_ui_unload_font(big_font);
    aroma_ui_unload_font(time_font);
    aroma_ui_unload_font(small_font);
    aroma_ui_unload_font(tiny_font);
    aroma_ui_unload_font(icon_font_big);
    aroma_ui_unload_font(icon_font_medium);
    aroma_ui_unload_font(icon_font_small);
    aroma_ui_unload_font(icon_font_strip);
    aroma_ui_shutdown();

    return 0;
}