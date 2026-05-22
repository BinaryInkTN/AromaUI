#include "app_state.h"
#include <string.h>
#include <stdlib.h>

AppState state = {0};

void init_app_state(void)
{
    // Zero out everything first
    memset(&state, 0, sizeof(AppState));
    
    // Initialize all pointers to NULL explicitly
    state.window = NULL;
    state.settings_root = NULL;
    state.vehicle_view_root = NULL;
    state.tabs = NULL;
    state.sidebar = NULL;
    
    state.time_label = NULL;
    state.location_label = NULL;
    state.status_card = NULL;
    state.signal_icon = NULL;
    state.wifi_icon = NULL;
    state.battery_icon = NULL;
    state.gps_icon = NULL;
    state.bluetooth_icon = NULL;
    
    state.vehicle_view_lock_icon = NULL;
    state.vehicle_view_lock_divider = NULL;
    state.vehicle_view_charge_port_divider = NULL;
    state.vehicle_view_charge_port_icon = NULL;
    state.vehicle_view_frunk_header = NULL;
    state.vehicle_view_frunk_desc = NULL;
    state.vehicle_view_frunk_divider = NULL;
    state.vehicle_view_trunk_divider = NULL;
    state.vehicle_view_trunk_header = NULL;
    state.vehicle_view_trunk_desc = NULL;
    state.vehicle_view_warning_message_card = NULL;
    state.vehicle_view_warning_message_label = NULL;
    state.vehicle_view_warning_warning_icon = NULL;
    state.vehicle_view_warning_message_action = NULL;
    state.vehicle_view_large_clock = NULL;
    state.vehicle_view_large_clock_pm_am = NULL;
    state.vehicle_view_hints = NULL;
    state.vehicle_view_side_arrow_icon_button = NULL;
    state.overlay = NULL;
    state.recent_lv = NULL;
    
    state.speed_label = NULL;
    state.speed_gauge = NULL;
    state.range_label = NULL;
    state.climate_label = NULL;
    state.gear_bg_card = NULL;
    state.gear_fg_card = NULL;
    
    state.ac_card = NULL;
    state.music_card = NULL;
    state.nav_card = NULL;
    
    state.vehicle_view_battery_divider = NULL;
    state.vehicle_view_battery_percentage = NULL;
    
    state.battery_image = NULL;
    state.battery_health = NULL;
    state.battery_percentage = NULL;
    
    state.map_node = NULL;
    state.map_panel = NULL;
    state.map_overlay_background = NULL;
    
    state.settings_panel_node = NULL;
    
    state.ac_temp_label = NULL;
    
    state.voice_button = NULL;
    state.settings_icon = NULL;
    state.voice_status_label = NULL;
    state.voice_status_card = NULL;
    state.loading_spinner = NULL;
    
    for (int i = 0; i < 8; i++) {
        state.listviews[i] = NULL;
        state.listview_containers[i] = NULL;
    }
    
    state.easter_egg_overlay = NULL;
    state.easter_egg_icon = NULL;
    state.setup_overlay = NULL;
    state.battery_button = NULL;
    
    // Initialize non-pointer members
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
    
    // Clear strings
    memset(state.voice_status_text, 0, sizeof(state.voice_status_text));
    memset(state.voice_partial_text, 0, sizeof(state.voice_partial_text));
    memset(state.voice_nav_dest, 0, sizeof(state.voice_nav_dest));
    
    // Initialize mutexes
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