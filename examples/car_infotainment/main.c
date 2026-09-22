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
#include "easter_egg.h"
#include "tabs_manager.h"
#include "app_registry.h"
#include "package_manager.h"
#include "setup_store.h"
#include "setup_wizard.h"
#include <stdio.h>

extern void register_settings_app(void);
extern void register_store_app(void);
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

    /* Persisted setup choices (theme, wifi, device name, ...). Loaded before
     * anything that consumes them. */
    setup_store_load();
    state.g_voice_assistant_enabled = setup_store_get_int("voice_enabled", 1) != 0;
 
    aroma_animation_manager_init();

    app_registry_init();
    register_settings_app();
    register_store_app();

    /* Scan installed .apak packages (UI is mounted later, once fonts and
     * the vehicle view exist). A broken packages dir must not kill boot. */
    package_manager_init();


    if (!aroma_ui_init())
    {
        fprintf(stderr, "FATAL: Failed to initialize UI\n");
        cleanup_app_state();
        return EXIT_FAILURE;
    }
    init_theme();
    apply_theme(setup_store_get_int("dark_theme", 0) != 0);

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

    if (state.g_voice_assistant_enabled)
        start_voice_control_thread();

    /* First-run setup overlays the lock screen until completed. */
    setup_wizard_maybe_show();

    /* Visual-test hook (example only): AROMA_DEBUG_SCREEN=drawer|store|
     * settings|<package-id> opens that screen right after boot. */
    {
        const char *dbg = getenv("AROMA_DEBUG_SCREEN");
        if (dbg && dbg[0])
        {
            unlock_screen();
            vehicle_view_debug_open(dbg);
        }
    }

    main_loop();


    cleanup_fonts();

    package_manager_shutdown();
    app_registry_cleanup();
    aroma_ui_shutdown();
    cleanup_app_state();

    return EXIT_SUCCESS;
}