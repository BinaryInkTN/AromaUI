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
#include "map_panel.h"
#include "easter_egg.h"
#include "tabs_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

// Signal handler for clean shutdown
static void signal_handler(int sig)
{
    (void)sig;
    aroma_ui_quit();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize application state
    if (!init_app_state()) {
        fprintf(stderr, "FATAL: Failed to initialize application state\n");
        return EXIT_FAILURE;
    }
    
    // Initialize animation system
    aroma_animation_manager_init();

    // Show splash screen
    char build_info[MAX_STRING_LEN];
    snprintf(build_info, sizeof(build_info),
             "AromaOS v0.0.1 - Build: %s %s", __DATE__, __TIME__);
    aroma_splash(false, "AromaOS", build_info);
    
    // Initialize UI
    if (!aroma_ui_init()) {
        fprintf(stderr, "FATAL: Failed to initialize UI\n");
        cleanup_app_state();
        return EXIT_FAILURE;
    }
    
    // Initialize theme
    if (!init_theme()) {
        fprintf(stderr, "FATAL: Failed to initialize theme\n");
        aroma_ui_shutdown();
        cleanup_app_state();
        return EXIT_FAILURE;
    }
    
    // Initialize fonts
    if (!init_fonts()) {
        fprintf(stderr, "FATAL: Failed to initialize fonts\n");
        aroma_ui_shutdown();
        cleanup_app_state();
        return EXIT_FAILURE;
    }
    
    // Create main window
    state.window = aroma_ui_create_window("Automotive HMI", WIN_W, WIN_H);
    if (!state.window) {
        fprintf(stderr, "FATAL: Failed to create window\n");
        cleanup_fonts();
        aroma_ui_shutdown();
        cleanup_app_state();
        return EXIT_FAILURE;
    }
    
    // Setup event handling
    aroma_event_set_root((AromaNode *)state.window);
    aroma_ui_prepare_font_for_window(0, state.ui_font);

    // Build all UI components
    build_status_bar();
    build_voice_status_ui();
    build_vehicle_view((AromaNode *)state.window);
    build_map_panel((AromaNode *)state.window);
    build_settings_ui((AromaNode *)state.window);
    build_easter_egg_ui((AromaNode *)state.window);
    build_tabs();

    // Hide initial elements
    if (state.time_label) {
        aroma_node_set_hidden(state.time_label, true);
    }
    if (state.location_label) {
        aroma_node_set_hidden(state.location_label, true);
    }
    if (state.tabs) {
        aroma_node_set_hidden(state.tabs, true);
    }

    // Start background threads
    start_voice_control_thread();
    
#ifndef __EMSCRIPTEN__
    start_can_thread();
#endif

    // Run main loop
    main_loop();
    
    // Cleanup
#ifndef __EMSCRIPTEN__
    stop_can_thread();
#endif
    
    cleanup_fonts();
    aroma_ui_shutdown();
    cleanup_app_state();
    
    return EXIT_SUCCESS;
}