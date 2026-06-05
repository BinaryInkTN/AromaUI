#include "aroma.h"
#include "aroma_animation.h"
#include "app_state.h"
#include "main_loop.h"
#include "theme_manager.h"
#include "font_manager.h"
#include "voice_handler.h"
#include "can_handler.h"
#include "status_bar.h"
#include "vehicle_view.h"
#include "settings_ui.h"
#include "easter_egg.h"
#include "tabs_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "shared_memory_bridge.h"


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

        // shared memory bridge initialization
    telemetry_bridge_t telemetry_bridge;

    if (!telemetry_bridge_open(&telemetry_bridge))
    {
        fprintf(stderr, "WARN: telemetry bridge unavailable, speed defaulting to 0\n");
        state.vehicle_state.speed = 0.0f;
    }
    else
    {
        struct telemetry_frame frame;
        int result = telemetry_bridge_read(&telemetry_bridge, &frame,
                                        TELEMETRY_READ_MAX_RETRIES);
        if (result == 1)
            state.vehicle_state.speed = telemetry_speed_kmh(&frame);
        else
            state.vehicle_state.speed = 0.0f;
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

#ifndef __EMSCRIPTEN__
    start_can_thread();
#endif

    main_loop(&telemetry_bridge);

#ifndef __EMSCRIPTEN__
    stop_can_thread();
#endif

    cleanup_fonts();
    aroma_ui_shutdown();
    cleanup_app_state();

    telemetry_bridge_close(&telemetry_bridge);
    return EXIT_SUCCESS;
}