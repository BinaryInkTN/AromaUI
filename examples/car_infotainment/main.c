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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    aroma_animation_manager_init();

    char build_info[256];
    snprintf(build_info, sizeof(build_info),
             "AromaOS v0.0.1 - Build: %s %s", __DATE__, __TIME__);
    aroma_splash(false, "AromaOS", build_info);
    
    bool ui_ok = aroma_ui_init();
    if (!ui_ok) {
        return 1;
    }

    init_app_state();
    init_theme();
    init_fonts();
    
    state.window = aroma_ui_create_window("Automotive HMI", WIN_W, WIN_H);
    aroma_event_set_root((AromaNode *)state.window);
    aroma_ui_prepare_font_for_window(0, state.ui_font);

    build_status_bar();
    build_voice_status_ui();
    build_vehicle_view((AromaNode *)state.window);
    build_map_panel((AromaNode *)state.window);
    build_settings_ui((AromaNode *)state.window);
    build_easter_egg_ui((AromaNode *)state.window);
    build_tabs();

    aroma_node_set_hidden(state.time_label, true);
    aroma_node_set_hidden(state.location_label, true);
    aroma_node_set_hidden(state.tabs, true);

    //start_voice_control_thread();
    
#ifndef __EMSCRIPTEN__
    start_can_thread();
#endif

    main_loop();
    
    cleanup_fonts();
    aroma_ui_shutdown();
    cleanup_app_state();
    
    return 0;
}