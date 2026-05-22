#include "main_loop.h"
#include "app_state.h"
#include "voice_handler.h"
#include "aroma.h"
#include "aroma_animation.h"
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static const char* get_fault_message(uint32_t fault)
{
    static const struct {
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
        {0xE401, "Autopilot: Radar Fault"}
    };
    
    for (size_t i = 0; i < sizeof(fault_table)/sizeof(fault_table[0]); i++) {
        if (fault_table[i].code == fault) {
            return fault_table[i].message;
        }
    }
    return "Unknown Error";
}

static void update_label_safe(AromaNode *label, const char *text)
{
    if (label && safe_node_check((const AromaNode *)label, "label")) {
        aroma_label_set_text(label, text);
    }
}

static void update_vehicle_display(void)
{
    double spd, rng, soc, cab_temp, tgt_temp;
    int gear_idx, fan_spd, hvac_active;
    uint32_t fault;
    
    if (pthread_mutex_lock(&state.can_mtx) != 0) {
        return;
    }
    
    spd = state.vehicle_state.speed;
    gear_idx = state.vehicle_state.gear;
    rng = state.vehicle_state.range;
    soc = state.vehicle_state.soc;
    cab_temp = state.vehicle_state.cabin_temp;
    tgt_temp = state.vehicle_state.target_temp;
    fan_spd = state.vehicle_state.fan_speed;
    hvac_active = state.vehicle_state.hvac_on;
    fault = state.vehicle_state.fault_code;
    
    pthread_mutex_unlock(&state.can_mtx);
    
    
    if (spd < 0) spd = 0;
    if (spd > 999) spd = 999;
    if (gear_idx < 0) gear_idx = 0;
    if (gear_idx > 3) gear_idx = 3;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    
    if (state.speed_label) {
        char spd_str[16];
        snprintf(spd_str, sizeof(spd_str), "%.0f", spd);
        update_label_safe(state.speed_label, spd_str);
    }

    if (state.gear_fg_card && gear_idx >= 0 && gear_idx <= 3) {
        static int last_gear_idx = -1;
        static const uint32_t gear_colors[] = {
            0xFF00C853, 0xFFD50000, 0xFFFFD600, 0xFF2196F3
        };
        
        if (gear_idx != last_gear_idx) {
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

    if (state.range_label) {
        char rng_str[32];
        snprintf(rng_str, sizeof(rng_str), "Range: %.0f km", rng);
        update_label_safe(state.range_label, rng_str);
    }

    if (state.battery_percentage) {
        char bat_str[16];
        snprintf(bat_str, sizeof(bat_str), "%.0f%%", soc);
        update_label_safe(state.battery_percentage, bat_str);
    }

    if (state.climate_label) {
        char clim_str[64];
        if (hvac_active) {
            snprintf(clim_str, sizeof(clim_str), 
                    "Inside: %.1f°C | AC: %.1f°C (Auto)", cab_temp, tgt_temp);
        } else {
            snprintf(clim_str, sizeof(clim_str), 
                    "Inside: %.1f°C | AC Off", cab_temp);
        }
        update_label_safe(state.climate_label, clim_str);
    }

    if (state.location_label) {
        char loc_str[64];
        snprintf(loc_str, sizeof(loc_str), "%.1f°C, San Francisco", cab_temp);
        update_label_safe(state.location_label, loc_str);
    }

    if (state.ac_temp_label) {
        char ac_str[32];
        if (hvac_active) {
            snprintf(ac_str, sizeof(ac_str), "%.1f°C (Fan %d)", tgt_temp, fan_spd);
        } else {
            safe_str_copy(ac_str, "Off", sizeof(ac_str));
        }
        update_label_safe(state.ac_temp_label, ac_str);
    }

    if (state.vehicle_view_warning_message_card && 
        state.vehicle_view_warning_message_label) {
        
        static uint32_t last_fault = 0;
        
        if (fault != 0 && fault != last_fault) {
            char flt_str[MAX_FAULT_MSG];
            snprintf(flt_str, sizeof(flt_str), "FAULT 0x%04X: %s", 
                    fault, get_fault_message(fault));
            
            update_label_safe(state.vehicle_view_warning_message_label, flt_str);
            
            if (last_fault == 0) {
                aroma_node_set_hidden(state.vehicle_view_warning_message_card, false);
                aroma_animation_start(state.vehicle_view_warning_message_card,
                                     AROMA_ANIM_SLIDE_Y, WIN_H + 100, WIN_H - 120, 400);
                
                if (state.ac_card) {
                    aroma_animation_start(state.ac_card, 
                                         AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                }
                if (state.music_card) {
                    aroma_animation_start(state.music_card, 
                                         AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                }
                if (state.nav_card) {
                    aroma_animation_start(state.nav_card, 
                                         AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                }
            }
        } else if (fault == 0 && last_fault != 0) {
            aroma_animation_start(state.vehicle_view_warning_message_card,
                                 AROMA_ANIM_SLIDE_Y, WIN_H - 120, WIN_H + 100, 400);
            
            if (state.ac_card) {
                aroma_animation_start(state.ac_card, 
                                     AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
            }
            if (state.music_card) {
                aroma_animation_start(state.music_card, 
                                     AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
            }
            if (state.nav_card) {
                aroma_animation_start(state.nav_card, 
                                     AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
            }
        }
        
        last_fault = fault;
    }
}

void main_loop(void)
{
    uint64_t last_time_update = aroma_time_now_ms();
    
    
    if (state.vehicle_view_large_clock) {
        time_t rawtime;
        struct tm *timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        if (timeinfo) {
            char clock_str[16];
            strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
            update_label_safe(state.vehicle_view_large_clock, clock_str);
        }
    }

    while (aroma_ui_is_running())
    {
        uint64_t now = aroma_time_now_ms();

        if (now - last_time_update > 30000) {
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            if (timeinfo && state.vehicle_view_large_clock) {
                char clock_str[16];
                strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
                update_label_safe(state.vehicle_view_large_clock, clock_str);
            }
            last_time_update = now;
        }

        int do_map_open = 0;
        if (pthread_mutex_lock(&state.pending_mtx) == 0) {
            do_map_open = state.pending_map_open;
            state.pending_map_open = 0;
            pthread_mutex_unlock(&state.pending_mtx);
        }
        
        if (do_map_open) {
            extern void open_map_panel(void*);
            open_map_panel(NULL);
        }

        process_voice_commands();
        update_vehicle_display();
        aroma_ui_process_events();
        aroma_ui_render(state.window);
        
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
}