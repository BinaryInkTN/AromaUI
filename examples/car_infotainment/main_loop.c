#include "main_loop.h"
#include "app_state.h"
#include "voice_handler.h"
#include "aroma_animation.h"
#include "aroma.h"
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static const char *get_fault_message(uint32_t fault)
{
    switch (fault) {
        case 0xB101: return "BMS: Cell Overvoltage";
        case 0xB102: return "BMS: Cell Undervoltage";
        case 0xB100: return "BMS: Isolation Fault";
        case 0xC201: return "Motor: Inverter Overtemp";
        case 0xC200: return "Motor: Drive Inverter Fault";
        case 0xA100: return "Battery: Critical Low";
        case 0xD300: return "HVAC: Compressor Fault";
        case 0xD301: return "HVAC: Coolant Pump Fault";
        case 0xE400: return "Autopilot: Camera Blinded";
        case 0xE401: return "Autopilot: Radar Fault";
        default: return "Unknown Error";
    }
}

static void update_vehicle_display(void)
{
    pthread_mutex_lock(&state.can_mtx);
    double spd = state.vehicle_state.speed;
    int gear_idx = state.vehicle_state.gear;
    double rng = state.vehicle_state.range;
    double soc = state.vehicle_state.soc;
    double cab_temp = state.vehicle_state.cabin_temp;
    double tgt_temp = state.vehicle_state.target_temp;
    int fan_spd = state.vehicle_state.fan_speed;
    int hvac_active = state.vehicle_state.hvac_on;
    uint32_t fault = state.vehicle_state.fault_code;
    pthread_mutex_unlock(&state.can_mtx);

    if (state.speed_label) {
        char spd_str[16];
        snprintf(spd_str, sizeof(spd_str), "%.0f", spd);
        aroma_label_set_text(state.speed_label, spd_str);
    }

    if (state.gear_fg_card) {
        static int last_gear_idx = -1;
        if (gear_idx >= 0 && gear_idx <= 3 && gear_idx != last_gear_idx) {
            int target_x = 30 + gear_idx * 55;
            int start_x = (last_gear_idx == -1) ? target_x : (30 + last_gear_idx * 55);
            aroma_animation_start(state.gear_fg_card, AROMA_ANIM_SLIDE_X,
                                  start_x, target_x,
                                  (last_gear_idx == -1) ? 1 : 300);
            last_gear_idx = gear_idx;
        }
        static const uint32_t gear_colors[] = {
            0xFF00C853, 0xFFD50000, 0xFFFFD600, 0xFF2196F3
        };
        if (gear_idx >= 0 && gear_idx <= 3)
            aroma_card_set_colors(state.gear_fg_card,
                                  gear_colors[gear_idx], gear_colors[gear_idx]);
    }

    if (state.range_label) {
        char rng_str[32];
        snprintf(rng_str, sizeof(rng_str), "Range: %.0f km", rng);
        aroma_label_set_text(state.range_label, rng_str);
    }

    if (state.battery_percentage) {
        char bat_str[16];
        snprintf(bat_str, sizeof(bat_str), "%.0f%%", soc);
        aroma_label_set_text(state.battery_percentage, bat_str);
    }

    if (state.climate_label) {
        char clim_str[64];
        if (hvac_active)
            snprintf(clim_str, sizeof(clim_str), "Inside: %.1f°C | AC: %.1f°C (Auto)", cab_temp, tgt_temp);
        else
            snprintf(clim_str, sizeof(clim_str), "Inside: %.1f°C | AC Off", cab_temp);
        aroma_label_set_text(state.climate_label, clim_str);
    }

    if (state.location_label) {
        char loc_str[64];
        snprintf(loc_str, sizeof(loc_str), "%.1f°C, San Francisco", cab_temp);
        aroma_label_set_text(state.location_label, loc_str);
    }

    if (state.ac_temp_label) {
        char ac_str[32];
        if (hvac_active) 
            snprintf(ac_str, sizeof(ac_str), "%.1f°C (Fan %d)", tgt_temp, fan_spd);
        else             
            strncpy(ac_str, "Off", sizeof(ac_str));
        aroma_label_set_text(state.ac_temp_label, ac_str);
    }

    if (state.vehicle_view_warning_message_card) {
        static uint32_t last_fault = 0;
        if (fault != 0) {
            char flt_str[128];
            snprintf(flt_str, sizeof(flt_str), "FAULT 0x%04X: %s", fault, get_fault_message(fault));
            aroma_label_set_text(state.vehicle_view_warning_message_label, flt_str);

            if (last_fault == 0) {
                aroma_node_set_hidden(state.vehicle_view_warning_message_card, false);
                aroma_animation_start(state.vehicle_view_warning_message_card,
                                     AROMA_ANIM_SLIDE_Y, WIN_H + 100, WIN_H - 120, 400);
                if (state.ac_card)    
                    aroma_animation_start(state.ac_card, AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                if (state.music_card) 
                    aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                if (state.nav_card)   
                    aroma_animation_start(state.nav_card, AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
            }
        } else {
            if (last_fault != 0) {
                aroma_animation_start(state.vehicle_view_warning_message_card,
                                     AROMA_ANIM_SLIDE_Y, WIN_H - 120, WIN_H + 100, 400);
                if (state.ac_card)    
                    aroma_animation_start(state.ac_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
                if (state.music_card) 
                    aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
                if (state.nav_card)   
                    aroma_animation_start(state.nav_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
            }
        }
        last_fault = fault;
    }
}

void main_loop(void)
{
    uint64_t last_time_update = aroma_time_now_ms();

    while (aroma_ui_is_running())
    {
        uint64_t now = aroma_time_now_ms();

        // Update clock every minute
        if (now - last_time_update > 60000) {
            time_t rawtime; 
            time(&rawtime);
            struct tm *timeinfo = localtime(&rawtime);
            char clock_str[16];
            strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
            if (state.vehicle_view_large_clock)
                aroma_label_set_text(state.vehicle_view_large_clock, clock_str);
            last_time_update = now;
        }

        // Process pending map open from CAN
        pthread_mutex_lock(&state.pending_mtx);
        int do_map_open = state.pending_map_open;
        state.pending_map_open = 0;
        pthread_mutex_unlock(&state.pending_mtx);
        if (do_map_open) {
            extern void open_map_panel(void*);
            open_map_panel(NULL);
        }

        // Process voice commands
        process_voice_commands();

        // Update vehicle display from CAN data
        update_vehicle_display();

        // Process UI events and render
        aroma_ui_process_events();
        aroma_ui_render(state.window);
        
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
}