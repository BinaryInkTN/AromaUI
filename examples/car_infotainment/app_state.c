#include "app_state.h"
#include <string.h>
#include <stdlib.h>

AppState state = {0};

void init_app_state(void)
{
    memset(&state, 0, sizeof(AppState));
    
    state.map_panel_open = false;
    state.settings_panel_open = false;
    state.dark_theme_enabled = false;
    state.pending_map_open = 0;
    state.voice_is_visible = false;
    state.voice_target_tab = -1;
    state.voice_nav_trigger = false;
    state.voice_partial_timeout = 0;
    state.voice_theme_change = -1;
    state.voice_ac_change = 0;
    state.voice_info_request = 0;
    state.current_ac_temp = 23;
    state.g_voice_assistant_enabled = true;
    
    pthread_mutex_init(&state.can_mtx, NULL);
    pthread_mutex_init(&state.pending_mtx, NULL);
    pthread_mutex_init(&state.voice_mutex, NULL);
    
    memset(&state.vehicle_state, 0, sizeof(EVState));
}

void cleanup_app_state(void)
{
    pthread_mutex_destroy(&state.can_mtx);
    pthread_mutex_destroy(&state.pending_mtx);
    pthread_mutex_destroy(&state.voice_mutex);
}