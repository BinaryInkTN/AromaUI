#define LINUX_TOUCHSCREEN

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


    if (!aroma_ui_init())
    {
        fprintf(stderr, "FATAL: Failed to initialize UI\n");
        cleanup_app_state();
        return EXIT_FAILURE;
    }
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


    main_loop();


    cleanup_fonts();

    aroma_ui_shutdown();
    cleanup_app_state();

    return EXIT_SUCCESS;
}