#include "aroma.h"
#include "aroma_animation.h"
#include "app_state.h"
#include "main_loop.h"
#include "theme_manager.h"
#include "font_manager.h"
#include "voice_handler.h"
#include "status_bar.h"
#include "vehicle_view.h"
#include "settings_ui.h"
#include "easter_egg.h"
#include "tabs_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "telemetry_shm.h" 


int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!init_app_state())
    {
        fprintf(stderr, "FATAL: Failed to initialize application state\n");
        return EXIT_FAILURE;
    }
 
    aroma_animation_manager_init();

    char build_info[MAX_STRING_LEN];
    snprintf(build_info, sizeof(build_info),
             "AromaOS v0.0.1 - Build: %s %s", __DATE__, __TIME__);
    aroma_splash(false, "AromaOS", build_info);

    if (!aroma_ui_init())
    {
        fprintf(stderr, "FATAL: Failed to initialize UI\n");
        cleanup_app_state();
        return EXIT_FAILURE;
    }
    set_minimum_log_level(DEBUG_LEVEL_ERROR);
    init_theme();

    if (!init_fonts())
    {
        fprintf(stderr, "FATAL: Failed to initialize fonts\n");
        aroma_ui_shutdown();
        cleanup_app_state();
        return EXIT_FAILURE;
    }

    state.window = aroma_ui_create_window("Automotive HMI", WIN_W, WIN_H);
    if (!state.window)
    {
        fprintf(stderr, "FATAL: Failed to create window\n");
        cleanup_fonts();
        aroma_ui_shutdown();
        cleanup_app_state();
        return EXIT_FAILURE;
    }

    aroma_event_set_root((AromaNode *)state.window);
    aroma_ui_prepare_font_for_window(0, state.ui_font);

   
    telemetry_bridge_t telemetry_bridge;

    if (!telemetry_bridge_open(&telemetry_bridge))
    {
        fprintf(stderr, "WARN: telemetry bridge unavailable, using defaults\n");
       
        memset(&state.vehicle_state, 0, sizeof(EVState));
    }
    else
    {
        sdv_telemetry_t frame;
        int result = telemetry_bridge_read(&telemetry_bridge, &frame,
                                           TELEMETRY_READ_MAX_RETRIES);
        if (result == 1 && telemetry_frame_valid(&frame))
        {
           
            
           
            state.vehicle_state.wiper_speed      = telemetry_wiper_speed(&frame);
            state.vehicle_state.headlight_state   = telemetry_headlight(&frame);
            state.vehicle_state.indicator_left    = telemetry_indicator_left(&frame);
            state.vehicle_state.indicator_right   = telemetry_indicator_right(&frame);
            state.vehicle_state.buzzer            = telemetry_buzzer(&frame);
            state.vehicle_state.door_locked       = telemetry_door_locked(&frame);
            state.vehicle_state.interior_light    = telemetry_interior_light(&frame);
            state.vehicle_state.rain_detected     = telemetry_rain_detected(&frame);
            state.vehicle_state.door_open         = telemetry_door_open(&frame);

           
            state.vehicle_state.throttle_cmd      = telemetry_throttle_raw(&frame);
            state.vehicle_state.brake_cmd         = telemetry_brake_raw(&frame);
            state.vehicle_state.fsr_value         = telemetry_fsr(&frame);
            state.vehicle_state.vehicle_speed     = telemetry_vehicle_speed_raw(&frame);
            state.vehicle_state.crash_detected    = telemetry_crash_detected(&frame);
            state.vehicle_state.airbag_deployed   = telemetry_airbag_deployed(&frame);
            state.vehicle_state.seatbelt_warn     = telemetry_seatbelt_warning(&frame);
            state.vehicle_state.seat_occupied     = telemetry_seat_occupied(&frame);
            state.vehicle_state.acm_system_status = telemetry_acm_status(&frame);

           
            state.vehicle_state.seat_position_cmd = telemetry_seat_position(&frame);
            state.vehicle_state.seat_profile      = telemetry_seat_profile(&frame);

           
            state.vehicle_state.speed_raw         = telemetry_vss_speed_raw(&frame);
            state.vehicle_state.speed_filtered    = telemetry_vss_speed_filtered(&frame);
            state.vehicle_state.acceleration      = telemetry_acceleration_raw(&frame);
            state.vehicle_state.avg_speed         = telemetry_avg_speed(&frame);
            state.vehicle_state.max_speed         = telemetry_max_speed(&frame);
            state.vehicle_state.distance          = telemetry_distance(&frame);
            state.vehicle_state.kinetic_energy    = telemetry_kinetic_energy(&frame);
            state.vehicle_state.high_speed_flag   = telemetry_high_speed_warning(&frame);
            state.vehicle_state.harsh_braking     = telemetry_harsh_braking(&frame);
            state.vehicle_state.vss_fault         = telemetry_vss_fault(&frame);

           
            state.vehicle_state.temp_c            = (int16_t)(telemetry_temp_c(&frame) * 100.0f);
            state.vehicle_state.humidity          = (uint16_t)(telemetry_humidity_pct(&frame) * 100.0f);
            state.vehicle_state.dew_point_c       = (int16_t)(telemetry_dew_point_c(&frame) * 100.0f);
            state.vehicle_state.altitude_m        = (int16_t)(telemetry_altitude_m(&frame) * 100.0f);
            state.vehicle_state.pressure_pa       = telemetry_pressure_pa(&frame);
            state.vehicle_state.ecs_sensor_fault  = telemetry_ecs_fault(&frame);
            state.vehicle_state.comfort_cold      = telemetry_comfort_cold(&frame);
            state.vehicle_state.comfort_hot       = telemetry_comfort_hot(&frame);
            state.vehicle_state.high_humidity     = telemetry_high_humidity(&frame);

            printf("Telemetry bridge connected successfully\n");
            printf("  Speed: %u, Throttle: %u, Brake: %u\n",
                   state.vehicle_state.vehicle_speed,
                   state.vehicle_state.throttle_cmd,
                   state.vehicle_state.brake_cmd);
            printf("  Doors: %s, Lights: %u, Wipers: %u\n",
                   state.vehicle_state.door_open ? "OPEN" : "CLOSED",
                   state.vehicle_state.headlight_state,
                   state.vehicle_state.wiper_speed);
            printf("  Temp: %.1f°C, Humidity: %.1f%%\n",
                   telemetry_temp_c(&frame),
                   telemetry_humidity_pct(&frame));
        }
        else
        {
            fprintf(stderr, "WARN: Failed to read initial telemetry frame, using defaults\n");
            memset(&state.vehicle_state, 0, sizeof(EVState));
        }
    }

    build_status_bar();
    build_voice_status_ui();
    build_vehicle_view((AromaNode *)state.window);
    build_settings_ui((AromaNode *)state.window);
    build_easter_egg_ui((AromaNode *)state.window);
    build_tabs();
    
    if (state.time_label)
    {
        aroma_node_set_hidden(state.time_label, true);
    }
    if (state.location_label)
    {
        aroma_node_set_hidden(state.location_label, true);
    }
    if (state.tabs)
    {
        aroma_node_set_hidden(state.tabs, true);
    }

    start_voice_control_thread();


    main_loop(&telemetry_bridge);


    cleanup_fonts();
    aroma_ui_shutdown();
    cleanup_app_state();

    telemetry_bridge_close(&telemetry_bridge);
    return EXIT_SUCCESS;
}