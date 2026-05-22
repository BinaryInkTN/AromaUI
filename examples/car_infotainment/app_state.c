#include "app_state.h"
#include <stdlib.h>
#include <stdio.h>

AppState state;

void safe_str_copy(char *dest, const char *src, size_t dest_size)
{
    if (!dest || !src || dest_size == 0) {
        return;
    }
    
    size_t i;
    for (i = 0; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

bool safe_node_check(const AromaNode *node, const char *node_name)
{
    if (!node) {
        fprintf(stderr, "WARNING: NULL node pointer for %s\n", 
                node_name ? node_name : "unknown");
        return false;
    }
    return true;
}

bool init_app_state(void)
{
    // Zero initialize entire structure
    memset(&state, 0, sizeof(AppState));
    
    // Initialize mutexes
    if (pthread_mutex_init(&state.can_mtx, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize CAN mutex\n");
        return false;
    }
    
    if (pthread_mutex_init(&state.pending_mtx, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize pending mutex\n");
        pthread_mutex_destroy(&state.can_mtx);
        return false;
    }
    
    if (pthread_mutex_init(&state.voice_mtx, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize voice mutex\n");
        pthread_mutex_destroy(&state.can_mtx);
        pthread_mutex_destroy(&state.pending_mtx);
        return false;
    }
    
    // Set default values
    state.current_ac_temp = 23;
    state.g_voice_assistant_enabled = true;
    state.voice_target_tab = -1;
    state.voice_theme_change = -1;
    state.dark_theme_enabled = false;
    
    // Clear all strings
    memset(&state.vehicle_state, 0, sizeof(EVState));
    
    state.initialized = true;
    return true;
}

void cleanup_app_state(void)
{
    if (!state.initialized) {
        return;
    }
    
    // Reset all pointers to NULL
    state.window = NULL;
    state.settings_root = NULL;
    state.vehicle_view_root = NULL;
    state.tabs = NULL;
    state.sidebar = NULL;
    state.overlay = NULL;
    state.map_node = NULL;
    state.map_panel = NULL;
    
    // Destroy mutexes
    pthread_mutex_destroy(&state.can_mtx);
    pthread_mutex_destroy(&state.pending_mtx);
    pthread_mutex_destroy(&state.voice_mtx);
    
    // Clear sensitive data
    memset(&state.vehicle_state, 0, sizeof(EVState));
    memset(state.voice_status_text, 0, sizeof(state.voice_status_text));
    memset(state.voice_partial_text, 0, sizeof(state.voice_partial_text));
    memset(state.voice_nav_dest, 0, sizeof(state.voice_nav_dest));
    
    state.initialized = false;
}