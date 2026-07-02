#include <unistd.h>

#include "main_loop.h"
#include "app_state.h"
#include "voice_handler.h"
#include "aroma.h"
#include "aroma_animation.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include "telemetry_shm.h"

static const char *get_fault_message(uint32_t fault)
{
    static const struct
    {
        uint32_t code;
        const char *message;
    } fault_table[] = {
        {0xB101, "BMS: Cell Overvoltage"},
        {0xB102, "BMS: Cell Undervoltage"},
        {0xB100, "BMS: Isolation Fault"},
        {0xC201, "Motor: Inverter Overtemp"},
        {0xC200, "Motor: Drive Inverter Fault"},
        {0xA100, "Battery: Critical Low"},
        {0xD300, "HVAC: Compressor Fault"},
        {0xD301, "HVAC: Coolant Pump Fault"},
        {0xE400, "Autopilot: Camera Blinded"},
        {0xE401, "Autopilot: Radar Fault"}};

    for (size_t i = 0; i < sizeof(fault_table) / sizeof(fault_table[0]); i++)
    {
        if (fault_table[i].code == fault)
        {
            return fault_table[i].message;
        }
    }
    return "Unknown Error";
}

static void update_label_safe(AromaNode *label, const char *text)
{
    if (label && safe_node_check((const AromaNode *)label, "label"))
    {
        aroma_label_set_text(label, text);
    }
}

static void update_vehicle_display(void)
{
    double spd, rng, soc, cab_temp;
    int gear_idx, fan_spd, hvac_active;
    uint32_t fault;

    if (pthread_mutex_lock(&state.can_mtx) != 0)
    {
        return;
    }

    spd = (double)state.vehicle_state.vehicle_speed;
    gear_idx = 0;
    rng = (double)state.vehicle_state.distance / 1000.0;
    soc = 75.0;
    cab_temp = (double)state.vehicle_state.temp_c;
    fan_spd = 2;
    hvac_active = state.vehicle_state.comfort_cold || state.vehicle_state.comfort_hot;
    fault = (uint32_t)state.vehicle_state.acm_system_status;

    if (spd < 0.5)
        gear_idx = 0;
    else if (spd < 10)
        gear_idx = 1;
    else
        gear_idx = 3;

    pthread_mutex_unlock(&state.can_mtx);

    if (spd < 0)
        spd = 0;
    if (spd > 999)
        spd = 999;
    if (gear_idx < 0)
        gear_idx = 0;
    if (gear_idx > 3)
        gear_idx = 3;
    if (soc < 0)
        soc = 0;
    if (soc > 100)
        soc = 100;

    if (state.speed_label)
    {
        char spd_str[16];
        snprintf(spd_str, sizeof(spd_str), "%.0f", spd);
        update_label_safe(state.speed_label, spd_str);
    }

    if (state.gear_fg_card && gear_idx >= 0 && gear_idx <= 3)
    {
        static int last_gear_idx = -1;
        static const uint32_t gear_colors[] = {
            0xFF00C853,
            0xFFD50000,
            0xFFFFD600,
            0xFF2196F3};

        if (gear_idx != last_gear_idx)
        {
            int target_x = 30 + gear_idx * 55;
            int start_x = (last_gear_idx == -1) ? target_x : (30 + last_gear_idx * 55);
            int duration = (last_gear_idx == -1) ? 1 : 300;

            aroma_animation_start(state.gear_fg_card,
                                  AROMA_ANIM_SLIDE_X,
                                  start_x, target_x, duration);
            last_gear_idx = gear_idx;
        }

        aroma_card_set_colors(state.gear_fg_card,
                              gear_colors[gear_idx],
                              gear_colors[gear_idx]);
    }

    if (state.range_label)
    {
        char rng_str[32];
        snprintf(rng_str, sizeof(rng_str), "Range: %.0f km", rng);
        update_label_safe(state.range_label, rng_str);
    }

    if (state.battery_percentage)
    {
        char bat_str[16];
        snprintf(bat_str, sizeof(bat_str), "%.0f%%", soc);
        update_label_safe(state.battery_percentage, bat_str);
    }

    if (state.climate_label)
    {
        char clim_str[64];
        if (hvac_active)
        {
            snprintf(clim_str, sizeof(clim_str),
                     "Inside: %.1f°C | AC: %.1f°C (Auto)",
                     cab_temp, 22.0);
        }
        else
        {
            snprintf(clim_str, sizeof(clim_str),
                     "Inside: %.1f°C | AC Off", cab_temp);
        }
        update_label_safe(state.climate_label, clim_str);
    }

    if (state.location_label)
    {
        char loc_str[64];
        snprintf(loc_str, sizeof(loc_str), "%.1f°C, San Francisco", cab_temp);
        update_label_safe(state.location_label, loc_str);
    }

    if (state.ac_temp_label)
    {
        char ac_str[32];
        if (hvac_active)
        {
            snprintf(ac_str, sizeof(ac_str), "%.1f°C (Fan %d)", 22.0, fan_spd);
        }
        else
        {
            safe_str_copy(ac_str, "Off", sizeof(ac_str));
        }
        update_label_safe(state.ac_temp_label, ac_str);
    }

    if (state.vehicle_view_lock_icon)
    {
    }

    if (state.vehicle_view_warning_message_card &&
        state.vehicle_view_warning_message_label)
    {
        static uint32_t last_fault = 0;

        if (fault != 0 && fault != last_fault)
        {
            char flt_str[MAX_FAULT_MSG];
            snprintf(flt_str, sizeof(flt_str), "FAULT 0x%04X: %s",
                     fault, get_fault_message(fault));

            update_label_safe(state.vehicle_view_warning_message_label, flt_str);

            if (last_fault == 0)
            {
                aroma_node_set_hidden(state.vehicle_view_warning_message_card, false);
                aroma_animation_start(state.vehicle_view_warning_message_card,
                                      AROMA_ANIM_SLIDE_Y, WIN_H + 100, WIN_H - 120, 400);
            }
        }

        last_fault = fault;
    }
}

static void update_telemetry_state(telemetry_bridge_t *bridge)
{
    if (!bridge || !bridge->shm)
        return;

    sdv_telemetry_t frame;
    int result = telemetry_bridge_read(bridge, &frame,
                                       TELEMETRY_READ_MAX_RETRIES);

    if (result != 1 || !telemetry_frame_valid(&frame))
        return;

    if (pthread_mutex_lock(&state.can_mtx) != 0)
        return;

    state.vehicle_state.wiper_speed = telemetry_wiper_speed(&frame);
    state.vehicle_state.headlight_state = telemetry_headlight(&frame);
    state.vehicle_state.indicator_left = telemetry_indicator_left(&frame);
    state.vehicle_state.indicator_right = telemetry_indicator_right(&frame);
    state.vehicle_state.buzzer = telemetry_buzzer(&frame);
    state.vehicle_state.door_locked = telemetry_door_locked(&frame);
    state.vehicle_state.interior_light = telemetry_interior_light(&frame);
    state.vehicle_state.rain_detected = telemetry_rain_detected(&frame);
    state.vehicle_state.door_open = telemetry_door_open(&frame);

    state.vehicle_state.throttle_cmd = telemetry_throttle_raw(&frame);
    state.vehicle_state.brake_cmd = telemetry_brake_raw(&frame);
    state.vehicle_state.fsr_value = telemetry_fsr(&frame);
    state.vehicle_state.vehicle_speed = telemetry_vehicle_speed_raw(&frame);
    state.vehicle_state.crash_detected = telemetry_crash_detected(&frame);
    state.vehicle_state.airbag_deployed = telemetry_airbag_deployed(&frame);
    state.vehicle_state.seatbelt_warn = telemetry_seatbelt_warning(&frame);
    state.vehicle_state.seat_occupied = telemetry_seat_occupied(&frame);
    state.vehicle_state.acm_system_status = telemetry_acm_status(&frame);

    state.vehicle_state.seat_position_cmd = telemetry_seat_position(&frame);
    state.vehicle_state.seat_profile = telemetry_seat_profile(&frame);

    state.vehicle_state.speed_raw = telemetry_vss_speed_raw(&frame);
    state.vehicle_state.speed_filtered = telemetry_vss_speed_filtered(&frame);
    state.vehicle_state.acceleration = telemetry_acceleration_raw(&frame);
    state.vehicle_state.avg_speed = telemetry_avg_speed(&frame);
    state.vehicle_state.max_speed = telemetry_max_speed(&frame);
    state.vehicle_state.distance = telemetry_distance(&frame);
    state.vehicle_state.kinetic_energy = telemetry_kinetic_energy(&frame);
    state.vehicle_state.high_speed_flag = telemetry_high_speed_warning(&frame);
    state.vehicle_state.harsh_braking = telemetry_harsh_braking(&frame);
    state.vehicle_state.vss_fault = telemetry_vss_fault(&frame);

    state.vehicle_state.temp_c = (int16_t)(telemetry_temp_c(&frame) * 100.0f);
    state.vehicle_state.humidity = (uint16_t)(telemetry_humidity_pct(&frame) * 100.0f);
    state.vehicle_state.dew_point_c = (int16_t)(telemetry_dew_point_c(&frame) * 100.0f);
    state.vehicle_state.altitude_m = (int16_t)(telemetry_altitude_m(&frame) * 100.0f);
    state.vehicle_state.pressure_pa = telemetry_pressure_pa(&frame);
    state.vehicle_state.ecs_sensor_fault = telemetry_ecs_fault(&frame);
    state.vehicle_state.comfort_cold = telemetry_comfort_cold(&frame);
    state.vehicle_state.comfort_hot = telemetry_comfort_hot(&frame);
    state.vehicle_state.high_humidity = telemetry_high_humidity(&frame);

    pthread_mutex_unlock(&state.can_mtx);
}

void main_loop(telemetry_bridge_t *telemetry_bridge)
{
    uint64_t last_time_update = aroma_time_now_ms();
    uint64_t last_telemetry_update = 0;

    if (state.vehicle_view_large_clock)
    {
        time_t rawtime;
        struct tm *timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        if (timeinfo)
        {
            char clock_str[16];
            strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
            update_label_safe(state.vehicle_view_large_clock, clock_str);
        }
    }

    while (aroma_ui_is_running())
    {
        uint64_t now = aroma_time_now_ms();

        if (now - last_time_update > 30000)
        {
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            if (timeinfo && state.vehicle_view_large_clock)
            {
                char clock_str[16];
                strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
                update_label_safe(state.vehicle_view_large_clock, clock_str);
            }
            last_time_update = now;
        }

        if (now - last_telemetry_update > 50)
        {
            update_telemetry_state(telemetry_bridge);
            last_telemetry_update = now;
        }

        if (state.speed_label)
        {
            char speed_buffer[16];
            if (pthread_mutex_lock(&state.can_mtx) == 0)
            {
                snprintf(speed_buffer, sizeof(speed_buffer), "%.0f",
                         (double)state.vehicle_state.vehicle_speed);
                pthread_mutex_unlock(&state.can_mtx);
                update_label_safe(state.speed_label, speed_buffer);
            }
        }

        update_vehicle_display();

        process_voice_commands();

        aroma_ui_process_events();
        aroma_ui_render(state.window);

#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
}