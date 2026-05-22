#include "voice_handler.h"
#include "app_state.h"
#include "aroma.h"
#include "aroma_animation.h"
#include "theme_manager.h"
#include "tabs_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef AROMA_USE_VOICE_CONTROL
#include "voice_control.h"
#else
static inline void start_voice_control_thread(void) {}
static inline void trigger_manual_wake(void) {}
static inline void aroma_voice_speak(const char *s) { (void)s; }
#endif

void set_voice_status(const char *status)
{
    if (!state.initialized) return;
    
    if (state.voice_status_label && safe_node_check(state.voice_status_label, "voice_status_label")) {
        aroma_label_set_text(state.voice_status_label, status ? status : "");
    }

    if (!state.voice_status_card || !safe_node_check(state.voice_status_card, "voice_status_card")) {
        return;
    }

    bool hide = (!status || strlen(status) == 0);
    
    if (!hide && !state.voice_is_visible) {
        aroma_node_set_hidden(state.voice_status_card, false);
        if (state.loading_spinner) {
            aroma_node_set_hidden(state.loading_spinner, false);
        }
        aroma_animation_start((AromaNode *)state.voice_status_card,
                              AROMA_ANIM_SLIDE_Y, -100, -20, 300);
        state.voice_is_visible = true;
    } else if (hide && state.voice_is_visible) {
        aroma_animation_start((AromaNode *)state.voice_status_card,
                              AROMA_ANIM_SLIDE_Y, -20, -100, 300);
        state.voice_is_visible = false;
        if (state.loading_spinner) {
            aroma_node_set_hidden(state.loading_spinner, true);
        }
    }
}

void queue_voice_navigation(const char *dest)
{
    if (!dest) return;
    
    if (pthread_mutex_lock(&state.voice_mutex) == 0) {
        safe_str_copy(state.voice_nav_dest, dest, sizeof(state.voice_nav_dest));
        state.voice_nav_trigger = true;
        pthread_mutex_unlock(&state.voice_mutex);
    }
}

void queue_voice_partial(const char *partial_text)
{
    if (!partial_text) return;
    
    if (pthread_mutex_lock(&state.voice_mutex) == 0) {
        size_t len = strlen(partial_text);
        if (len > 0 && len < sizeof(state.voice_partial_text)) {
            safe_str_copy(state.voice_partial_text, partial_text, sizeof(state.voice_partial_text));
            state.voice_partial_timeout = 180;
        }
        pthread_mutex_unlock(&state.voice_mutex);
    }
}

void queue_voice_theme(int dark_mode)
{
    if (pthread_mutex_lock(&state.voice_mutex) == 0) {
        state.voice_theme_change = dark_mode ? 1 : 0;
        pthread_mutex_unlock(&state.voice_mutex);
    }
}

void queue_voice_ac_action(int temp_delta)
{
    if (pthread_mutex_lock(&state.voice_mutex) == 0) {
        state.voice_ac_change = temp_delta;
        pthread_mutex_unlock(&state.voice_mutex);
    }
}

void queue_voice_info_request(int info_type)
{
    if (pthread_mutex_lock(&state.voice_mutex) == 0) {
        state.voice_info_request = info_type;
        pthread_mutex_unlock(&state.voice_mutex);
    }
}

void queue_voice_action(int tab_index, bool call, bool end_call, const char *status)
{
    (void)call;
    (void)end_call;
    
    if (pthread_mutex_lock(&state.voice_mutex) == 0) {
        if (tab_index >= 0) {
            state.voice_target_tab = tab_index;
        }
        if (status) {
            safe_str_copy(state.voice_status_text, status, sizeof(state.voice_status_text));
        }
        pthread_mutex_unlock(&state.voice_mutex);
    }
}

static void *beep_thread_func(void *arg)
{
    (void)arg;
#ifdef AROMA_USE_VOICE_CONTROL
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execlp("speaker-test", "speaker-test", "-t", "sine", "-f", "800", "-l", "1", NULL);
        _exit(1);
    } else if (pid > 0) {
        usleep(100000); // 100ms
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
#endif
    return NULL;
}

static void spawn_beep(void)
{
    pthread_t t;
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    if (pthread_create(&t, &attr, beep_thread_func, NULL) != 0) {
        // Failed to create thread, continue without beep
    }
    
    pthread_attr_destroy(&attr);
}

void voice_button_callback(void *user_data)
{
    (void)user_data;
#ifdef AROMA_USE_VOICE_CONTROL
    trigger_manual_wake();
    spawn_beep();
#endif
    queue_voice_partial("Listening...");
}

void process_voice_commands(void)
{
    if (!state.initialized) return;
    
    if (pthread_mutex_lock(&state.voice_mutex) != 0) {
        return;
    }

    // Process navigation
    if (state.voice_nav_trigger) {
        struct CityMap {
            const char *key;
            double lat;
            double lon;
        };
        
        static const struct CityMap cities[] = {
            {"paris",    48.8566,  2.3522},
            {"london",   51.5074, -0.1278},
            {"new york", 40.7128,-74.0060},
            {"tokyo",    35.6762,139.6503},
            {"berlin",   52.5200, 13.4050},
        };
        
        double lat = 37.7749;
        double lon = -122.4194;
        
        for (size_t i = 0; i < sizeof(cities)/sizeof(cities[0]); i++) {
            if (strcasestr(state.voice_nav_dest, cities[i].key)) {
                lat = cities[i].lat;
                lon = cities[i].lon;
                break;
            }
        }
        
        if (state.map_node && safe_node_check(state.map_node, "map_node")) {
            aroma_map_set_zoom(state.map_node, 10);
            aroma_map_pan_to(state.map_node, lat, lon);
        }
        
        extern void open_map_panel(void*);
        open_map_panel(NULL);
        state.voice_nav_trigger = false;
    }

    // Process tab navigation
    if (state.voice_target_tab != -1) {
        navigate_to_tab(state.voice_target_tab);
        state.voice_target_tab = -1;
    }

    // Process status text
    if (strlen(state.voice_status_text) > 0) {
        set_voice_status(state.voice_status_text);
        state.voice_partial_timeout = 180;
        memset(state.voice_status_text, 0, sizeof(state.voice_status_text));
    } else if (state.voice_partial_timeout > 0) {
        if (strlen(state.voice_partial_text) > 0) {
            set_voice_status(state.voice_partial_text);
        }
        state.voice_partial_timeout--;
        if (state.voice_partial_timeout == 0) {
            set_voice_status("");
            memset(state.voice_partial_text, 0, sizeof(state.voice_partial_text));
        }
    }

    // Process theme change
    if (state.voice_theme_change != -1) {
        bool want_dark = (state.voice_theme_change == 1);
        if (state.dark_theme_enabled != want_dark) {
            apply_theme(want_dark);
        }
        state.voice_theme_change = -1;
    }

    // Process AC change
    if (state.voice_ac_change != 0) {
        state.current_ac_temp += state.voice_ac_change;
        
        // Clamp temperature
        if (state.current_ac_temp < 16) state.current_ac_temp = 16;
        if (state.current_ac_temp > 30) state.current_ac_temp = 30;
        
        if (state.ac_temp_label && safe_node_check(state.ac_temp_label, "ac_temp_label")) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
            aroma_label_set_text(state.ac_temp_label, buf);
        }
        
        char speak[64];
        snprintf(speak, sizeof(speak), "Setting AC to %d degrees", state.current_ac_temp);
        aroma_voice_speak(speak);
        
        state.voice_ac_change = 0;
    }

    // Process info request
    if (state.voice_info_request != 0) {
        switch (state.voice_info_request) {
            case 1:
                aroma_voice_speak("Battery is at 75 percent charge.");
                break;
            case 2:
                aroma_voice_speak("Estimated range is 204 kilometers.");
                break;
            case 3:
                aroma_voice_speak("Battery is at 75 percent. Estimated range is 204 kilometers.");
                break;
            default:
                break;
        }
        state.voice_info_request = 0;
    }

    pthread_mutex_unlock(&state.voice_mutex);
}

void build_voice_status_ui(void)
{
    if (!state.window || !state.ui_font) {
        return;
    }
    
    state.voice_status_card = aroma_ui_card(
        (AromaNode *)state.window, 
        WIN_W / 2 - 300, -100, 600, 80, 
        CARD_TYPE_FILLED
    );
    
    if (!state.voice_status_card) return;
    
    aroma_node_set_z_index(state.voice_status_card, Z_LAYER_VOICE_CARD);
    
    state.voice_status_label = aroma_ui_label(
        state.voice_status_card, 
        "  ", 20, 40,
        LABEL_STYLE_LABEL_LARGE, 
        state.ui_font
    );
    
    if (state.voice_status_label) {
        aroma_node_set_z_index(state.voice_status_label, Z_LAYER_VOICE_CONTENT);
    }
    
    state.loading_spinner = aroma_ui_loading(
        state.voice_status_card, 
        530, 28, 22, 5, 
        state.theme.colors.primary
    );
    
    if (state.loading_spinner) {
        aroma_node_set_z_index(state.loading_spinner, Z_LAYER_VOICE_CONTENT);
        aroma_node_set_hidden(state.loading_spinner, true);
    }
}