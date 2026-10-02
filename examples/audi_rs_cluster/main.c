#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include "aroma.h"
#include "aroma_ubuntu_font.h"
#include "aroma_animation.h"

static AromaFont *small_font = NULL;
static AromaFont *title_font = NULL;
static AromaFont *speed_font = NULL;
static AromaFont *rpm_font = NULL;
static AromaFont *gear_font = NULL;

static AromaNode *rpm_gauge = NULL;
static AromaNode *speed_gauge = NULL;
static AromaNode *fuel_bar = NULL;
static AromaNode *coolant_bar = NULL;
static AromaNode *gear_label = NULL;
static AromaNode *odo_label = NULL;
static AromaNode *trip_label = NULL;
static AromaNode *range_label = NULL;

static AromaNode *splash_bg = NULL;
static AromaNode *splash_logo = NULL;
#define SPLASH_DURATION_FRAMES 120

static const float GEAR_SPEED_THRESHOLDS[] = {0.0f, 20.0f, 45.0f, 75.0f, 110.0f, 150.0f};
static const char *GEAR_LABELS[] = {"N", "1", "2", "3", "4", "5"};
#define GEAR_COUNT (sizeof(GEAR_LABELS) / sizeof(GEAR_LABELS[0]))

static const char *gear_for_speed(float speed_kmh)
{
    const char *gear = GEAR_LABELS[0];
    for (size_t i = 0; i < GEAR_COUNT; i++)
    {
        if (speed_kmh >= GEAR_SPEED_THRESHOLDS[i])
        {
            gear = GEAR_LABELS[i];
        }
    }
    return gear;
}

int main(int argc, char **argv)
{
    if (!aroma_ui_init())
    {
        printf("Failed to initialize Aroma UI\n");
        return -1;
    }

    AromaTheme dark_theme = aroma_theme_create_material_black();

    aroma_ui_set_theme(&dark_theme);
    AromaWindow *window = aroma_ui_create_window("VW Cluster", 1024, 600);
    if (!window)
        return -1;
    small_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 14);
    title_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 20);
    rpm_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    speed_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 40);
    gear_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 32);
    aroma_animation_manager_init();
    AromaNode *root = (AromaNode *)window;
    aroma_ui_image(root, "../assets/bg.jpg", 0, 0, 1024, 600);

    aroma_ui_image(root, "../assets/gauge_bg.png", 52, 141, 320, 320);
    rpm_gauge = aroma_ui_gauge(root, 12, 100, 400, 400);
    aroma_gauge_set_range(rpm_gauge, 0.0f, 8000.0f);
    aroma_gauge_set_angles(rpm_gauge, 2.35619f, 7.06858f);
    aroma_gauge_set_colors(rpm_gauge, 0xFFFFFFFF, 0xFFFFFFFF);
    aroma_gauge_set_thickness(rpm_gauge, 2, 4);
    aroma_gauge_set_ticks(rpm_gauge, true, 9, 4, 20, 8, 0xFFFFFFFF, 2);
    aroma_gauge_set_labels_numeric(rpm_gauge, true, 0, 1, 9, rpm_font, 0xFFFFFFFF, 10.0f);
    aroma_gauge_set_needle(rpm_gauge, true, 0xFFFF2222, 4);
    aroma_gauge_set_red_zone(rpm_gauge, 7000.0f);
    aroma_ui_image(root, "../assets/needle_hub.png", 182, 271, 60, 60);

    AromaNode *coolant_caption = aroma_ui_label(root, "COOLANT", 452, 72, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(coolant_caption, 0xFF888888);

    AromaNode *coolant_c_label = aroma_ui_label(root, "C", 452, 95, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(coolant_c_label, 0xFFAAAAAA);

    coolant_bar = aroma_ui_progressbar(root, 464, 95, 90, 10, PROGRESS_TYPE_DETERMINATE, 0.0f);

    AromaNode *coolant_h_label = aroma_ui_label(root, "H", 558, 95, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(coolant_h_label, 0xFFAAAAAA);

    AromaNode *center_screen = aroma_ui_card(root, 402, 160, 220, 280, CARD_TYPE_GLASS);
    aroma_node_set_hidden(center_screen, true);

    AromaNode *rs_logo = aroma_ui_image(root, "../assets/v5_logo.png", 395, 190, 600 / 2.6, 488 / 2.6);

    aroma_ui_divider(root, 402, 160, 2, DIVIDER_ORIENTATION_HORIZONTAL);

    AromaNode *gear_caption = aroma_ui_label(root, "GEAR", 412, 175, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(gear_caption, 0xFF888888);
    aroma_node_set_hidden(gear_caption, true);

    gear_label = aroma_ui_label(root, "N", 412, 195, LABEL_STYLE_LABEL_LARGE, gear_font);
    aroma_label_set_color(gear_label, 0xFFFFFFFF);
    aroma_node_set_hidden(gear_label, true);

    AromaNode *center_divider = aroma_ui_divider(root, 412, 250, 200, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_hidden(center_divider, true);

    AromaNode *odo_caption = aroma_ui_label(root, "ODO", 412, 260, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(odo_caption, 0xFF888888);
    aroma_node_set_hidden(odo_caption, true);

    odo_label = aroma_ui_label(root, "--- km", 412, 278, LABEL_STYLE_LABEL_LARGE, title_font);
    aroma_label_set_color(odo_label, 0xFFFFFFFF);
    aroma_node_set_hidden(odo_label, true);

    AromaNode *trip_caption = aroma_ui_label(root, "TRIP", 412, 310, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(trip_caption, 0xFF888888);
    aroma_node_set_hidden(trip_caption, true);

    trip_label = aroma_ui_label(root, "--- km", 412, 328, LABEL_STYLE_LABEL_LARGE, title_font);
    aroma_label_set_color(trip_label, 0xFFFFFFFF);
    aroma_node_set_hidden(trip_label, true);

    AromaNode *bottom_divider = aroma_ui_divider(root, 412, 365, 200, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_hidden(bottom_divider, true);

    AromaNode *range_caption = aroma_ui_label(root, "RANGE", 412, 378, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(range_caption, 0xFF888888);
    aroma_node_set_hidden(range_caption, true);

    range_label = aroma_ui_label(root, "--- km", 412, 396, LABEL_STYLE_LABEL_LARGE, title_font);
    aroma_label_set_color(range_label, 0xFFFFFFFF);
    aroma_node_set_hidden(range_label, true);

    AromaNode *rpm_unit1 = aroma_ui_label(root, "1/min", 195, 370, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(rpm_unit1, 0xFFAAAAAA);
    AromaNode *rpm_unit2 = aroma_ui_label(root, "x1000", 195, 390, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(rpm_unit2, 0xFFAAAAAA);

    aroma_ui_image(root, "../assets/gauge_bg.png", 652, 141, 320, 320);

    speed_gauge = aroma_ui_gauge(root, 612, 100, 400, 400);
    aroma_gauge_set_range(speed_gauge, 0.0f, 320.0f);
    aroma_gauge_set_angles(speed_gauge, 2.35619f, 7.06858f);
    aroma_gauge_set_colors(speed_gauge, 0xFFFFFFFF, 0xFFFFFFFF);
    aroma_gauge_set_thickness(speed_gauge, 2, 4);
    aroma_gauge_set_ticks(speed_gauge, true, 17, 1, 20, 8, 0xFFFFFFFF, 2);
    aroma_gauge_set_labels_numeric(speed_gauge, true, 0, 20, 17, small_font, 0xFFFFFFFF, 9.0f);
    aroma_gauge_set_needle(speed_gauge, true, 0xFFFF2222, 4);
    aroma_ui_image(root, "../assets/needle_hub.png", 782, 271, 60, 60);
    aroma_ui_image(root, "../assets/v5_logo.png", 785, 360, 60, 60);

    AromaNode *time_lbl = aroma_ui_label(root, "08:10", 353, 20, LABEL_STYLE_LABEL_LARGE, title_font);
    aroma_label_set_color(time_lbl, 0xFFFFFFFF);

    AromaNode *temp_lbl = aroma_ui_label(root, "0.0°C", 463, 20, LABEL_STYLE_LABEL_LARGE, title_font);
    aroma_label_set_color(temp_lbl, 0xFFFFFFFF);

    AromaNode *topbar_fuel_e_label = aroma_ui_label(root, "E", 573, 22, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(topbar_fuel_e_label, 0xFFAAAAAA);

    fuel_bar = aroma_ui_progressbar(root, 587, 22, 70, 10, PROGRESS_TYPE_DETERMINATE, 0.0f);

    AromaNode *topbar_fuel_f_label = aroma_ui_label(root, "F", 661, 22, LABEL_STYLE_LABEL_LARGE, small_font);
    aroma_label_set_color(topbar_fuel_f_label, 0xFFAAAAAA);

    splash_bg = aroma_ui_card(root, 0, 0, 1024, 600, CARD_TYPE_FILLED);
    aroma_card_set_colors(splash_bg, 0xFF000000, 0xFF000000);

    const int SPLASH_LOGO_W = 1408;
    const int SPLASH_LOGO_H = 768;
    splash_logo = aroma_ui_image(root, "../assets/racetronic.png",
                                 (1024 - SPLASH_LOGO_W/3) / 2,
                                 (600 - SPLASH_LOGO_H/3) / 2,
                                 SPLASH_LOGO_W/3, SPLASH_LOGO_H/3);

    int splash_frames_elapsed = 0;
    bool splash_done = false;

    float rpm = 0.0f;
    float speed = 0.0f;
    int sweep_state = 0;
    int sweep_delay = 0;

    float fuel_level = 0.0f;
    const float FUEL_TARGET = 0.78f;
    float coolant_temp_c = 15.0f;
    const float COOLANT_COLD_C = 15.0f;
    const float COOLANT_OPERATING_C = 90.0f;
    const float COOLANT_MAX_C = 120.0f;

    float trip_km = 0.0f;
    float odo_km = 128430.0f;
    float range_km = 512.0f;
    bool center_content_shown = false;

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();

        if (!splash_done)
        {
            splash_frames_elapsed++;
            if (splash_frames_elapsed >= SPLASH_DURATION_FRAMES)
            {
                aroma_node_set_hidden(splash_logo, true);
                aroma_node_set_hidden(splash_bg, true);
                splash_done = true;
            }

            aroma_ui_render(window);
            usleep(1000 / 60 * 1000);
            continue;
        }

        if (sweep_state == 0)
        {
            if (sweep_delay < 30)
            {
                sweep_delay++;
            }
            else
            {
                rpm += 120.0f;
                speed += 4.8f;

                if (rpm >= 8000.0f)
                {
                    rpm = 8000.0f;
                    speed = 320.0f;
                    sweep_state = 1;
                    sweep_delay = 0;
                }

                aroma_gauge_set_value(rpm_gauge, rpm);
                aroma_gauge_set_value(speed_gauge, speed);

                fuel_level += (FUEL_TARGET / 30.0f);
                if (fuel_level > FUEL_TARGET)
                {
                    fuel_level = FUEL_TARGET;
                }
                aroma_progressbar_set_progress(fuel_bar, fuel_level);

                coolant_temp_c += 0.6f;
                if (coolant_temp_c > COOLANT_OPERATING_C)
                {
                    coolant_temp_c = COOLANT_OPERATING_C;
                }
                aroma_progressbar_set_progress(coolant_bar,
                                               (coolant_temp_c - COOLANT_COLD_C) / (COOLANT_MAX_C - COOLANT_COLD_C));
            }
        }
        else if (sweep_state == 1)
        {
            if (sweep_delay < 25)
            {
                sweep_delay++;
            }
            else
            {
                rpm -= 500.0f;
                speed -= 20.0f;

                if (rpm <= 0.0f)
                {
                    rpm = 0.0f;
                    speed = 0.0f;
                    sweep_state = 2;

                    aroma_node_set_hidden(rs_logo, true);
                    aroma_node_set_hidden(center_screen, false);

                    aroma_node_set_hidden(gear_caption, false);
                    aroma_node_set_hidden(gear_label, false);
                    aroma_node_set_hidden(center_divider, false);
                    aroma_node_set_hidden(odo_caption, false);
                    aroma_node_set_hidden(odo_label, false);
                    aroma_node_set_hidden(trip_caption, false);
                    aroma_node_set_hidden(trip_label, false);
                    aroma_node_set_hidden(bottom_divider, false);
                    aroma_node_set_hidden(range_caption, false);
                    aroma_node_set_hidden(range_label, false);

                    char odo_str[32];
                    snprintf(odo_str, sizeof(odo_str), "%.0f km", odo_km);
                    aroma_label_set_text(odo_label, odo_str);

                    char trip_str[32];
                    snprintf(trip_str, sizeof(trip_str), "%.1f km", trip_km);
                    aroma_label_set_text(trip_label, trip_str);

                    char range_str[32];
                    snprintf(range_str, sizeof(range_str), "%.0f km", range_km);
                    aroma_label_set_text(range_label, range_str);

                    center_content_shown = true;
                }

                aroma_gauge_set_value(rpm_gauge, rpm);
                aroma_gauge_set_value(speed_gauge, speed);
            }
        }

        if (center_content_shown)
        {
            aroma_label_set_text(gear_label, gear_for_speed(speed));
        }

        aroma_ui_render(window);
        usleep(1000 / 60 * 1000);
    }

    aroma_ui_destroy_window(window);
    aroma_font_destroy(small_font);
    aroma_font_destroy(title_font);
    aroma_font_destroy(rpm_font);
    aroma_font_destroy(speed_font);
    aroma_font_destroy(gear_font);
    aroma_ui_shutdown();

    return 0;
}