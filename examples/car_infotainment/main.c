#include <aroma.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "voice_control.h"

AromaNode* actual_map = NULL;

#define WIN_W 1280
#define WIN_H 800

typedef struct {
    AromaFont *icon_font;
    AromaFont *ui_font;
    AromaFont *tab_font;
    AromaFont *settings_font;
    AromaFont *speed_font;
    AromaFont *ac_font;
    AromaFont *dial_font;
    AromaFont *now_playing_font;
    AromaWindow *window;
    AromaContainer *general_root;
    AromaNode *settings_root;
    AromaNode *map_root;
    AromaNode *phone_root;
    AromaNode *music_root;
    AromaNode *tabs;
    AromaNode *time_label;
    AromaNode *location_label;
    AromaNode *status_card;
    AromaNode *signal_icon;
    AromaNode *wifi_icon;
    AromaNode *battery_icon;
    AromaNode *gps_icon;
    AromaNode *bluetooth_icon;
    AromaNode *general_info_card;
    AromaNode *vehicle_image;
    AromaNode *vehicle_status_label;
    AromaNode *battery_progress;
    AromaNode *battery_icon_large;
    AromaNode *range_value_label;
    AromaNode *range_unit_label;
    AromaNode *range_desc_label;
    AromaNode *consumption_value_label;
    AromaNode *consumption_unit_label;
    AromaNode *consumption_desc_label;
    AromaNode *capacity_value_label;
    AromaNode *capacity_unit_label;
    AromaNode *capacity_desc_label;
    AromaNode *divider1;
    AromaNode *divider2;
    AromaNode *speed_card;
    AromaNode *speed_title_label;
    AromaNode *speed_value_label;
    AromaNode *speed_unit_label;
    AromaNode *speed_divider;
    AromaNode *gear_label;
    AromaNode *gear_card;
    AromaNode *gear_labels[4];
    AromaNode *gear_highlight_card;
    AromaNode *applets_container;
    AromaNode *ac_applet;
    AromaNode *ac_title_label;
    AromaNode *ac_temp_label;
    AromaNode *ac_temp_down_btn;
    AromaNode *ac_temp_up_btn;
    AromaNode *ac_control_card;
    AromaNode *ac_image1;
    AromaNode *ac_image2;
    AromaNode *ac_image3;
    AromaNode *applets_row;
    AromaNode *system_status_card;
    AromaNode *system_status_title;
    AromaNode *status_icons[7];
    AromaNode *sidebar;
    AromaNode *listviews[9];
    AromaNode *listview_containers[9];
    AromaNode *dial_container;
    AromaNode *dial_number_display;
    AromaNode *dial_buttons[12];
    AromaNode *dial_call_button;
    AromaNode *dial_end_button;
    AromaNode *dial_delete_button;
    AromaNode *call_status_label;
    AromaNode *recent_calls_list;
    AromaNode *contacts_list;
    AromaNode *music_container;
    AromaNode *bt_status_label;
    AromaNode *bt_device_name;
    AromaNode *bt_connect_button;
    AromaNode *now_playing_art;
    AromaNode *now_playing_title;
    AromaNode *now_playing_artist;
    AromaNode *now_playing_progress;
    AromaNode *now_playing_time_elapsed;
    AromaNode *now_playing_time_total;
    AromaNode *music_control_play;
    AromaNode *music_control_prev;
    AromaNode *music_control_next;
    AromaNode *music_control_volume;
    AromaNode *volume_slider;
    AromaNode *available_devices_list;
    
    AromaNode *voice_button;
    AromaNode *voice_status_label;
    AromaNode *voice_status_card;

    AromaTheme theme;
    bool dark_theme_enabled;
    char current_number[20];
} AppState;

static AppState state = {0};

void build_general_ui(AromaContainer *root);
void build_settings_ui(AromaNode *window);
void build_phone_ui(AromaNode *window);
void build_music_ui(AromaNode *window);
void listview_callback(int index, void *user_data);
static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h, AromaFont *font);

void dial_button_callback(AromaNode* node, void *user_data) {
    int digit = (int)(intptr_t)user_data;
       const char *dial_labels[] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "*", "0", "#"
    };

    if (digit >= 0 && digit < 12) {
        strncat(state.current_number, dial_labels[digit], sizeof(state.current_number) - strlen(state.current_number) - 1);
        aroma_label_set_text(state.dial_number_display, state.current_number);
    }
}

void dial_delete_callback(AromaNode* node, void *user_data) {
    int len = strlen(state.current_number);
    if (len > 0) {
        state.current_number[len - 1] = '\0';
        aroma_label_set_text(state.dial_number_display, state.current_number);
    }
}

void dial_call_callback(AromaNode* node, void *user_data) {
    aroma_label_set_text(state.call_status_label, "Calling...");
}

void dial_end_callback(AromaNode* node,     void *user_data) {
    aroma_label_set_text(state.call_status_label, "");
    state.current_number[0] = '\0';
    aroma_label_set_text(state.dial_number_display, "");
}

void bt_connect_callback(AromaNode* node, void *user_data) {
    aroma_label_set_text(state.bt_status_label, "Connected");
    aroma_label_set_text(state.bt_device_name, "BMW X5 Audio");
}

static int current_ac_temp = 23;
static bool music_playing = false;
static float music_volume = 0.7f;

void music_play_callback(AromaNode* node, void *user_data) {
    if (!music_playing) {
        music_playing = true;
        aroma_iconbutton_set_icon(state.music_control_play, AROMA_ICON_PAUSE);
        system("aplay ../sample.wav >/dev/null 2>&1 &");
    } else {
        music_playing = false;
        aroma_iconbutton_set_icon(state.music_control_play, AROMA_ICON_PLAY_ARROW);
        system("pkill aplay >/dev/null 2>&1");
    }
}

void navigate_to_tab(int index) {
    if (state.tabs) {
        aroma_tabs_set_selected(state.tabs, index);
    }
}

void set_voice_status(const char* status) {
    if (state.voice_status_label) {
        aroma_label_set_text(state.voice_status_label, status);
    }
    if (state.voice_status_card) {
        bool hide = (status == NULL || strlen(status) == 0);
        aroma_node_set_hidden(state.voice_status_card, hide);
    }
}

static pthread_mutex_t voice_mutex = PTHREAD_MUTEX_INITIALIZER;
static int voice_target_tab = -1;
static bool voice_make_call = false;
static bool voice_end_call = false;
static char voice_status_text[256] = "";

static char voice_partial_text[512] = "";
static char voice_nav_dest[128] = {0};
static bool voice_nav_trigger = false;

void queue_voice_navigation(const char* dest) {
    pthread_mutex_lock(&voice_mutex);
    strncpy(voice_nav_dest, dest, sizeof(voice_nav_dest)-1);
    voice_nav_trigger = true;
    pthread_mutex_unlock(&voice_mutex);
}
static int voice_partial_timeout = 0;
static int voice_theme_change = -1;
static int voice_ac_change = 0;
static int voice_info_request = 0;
static int voice_music_action = 0; // 1=play, 2=pause, 3=vol_up, 4=vol_down

void queue_voice_music_action(int action) {
    pthread_mutex_lock(&voice_mutex);
    voice_music_action = action;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_partial(const char* partial_text) {
    pthread_mutex_lock(&voice_mutex);
    if (partial_text && strlen(partial_text) > 0) {
        strncpy(voice_partial_text, partial_text, sizeof(voice_partial_text) - 1);
        voice_partial_text[sizeof(voice_partial_text) - 1] = '\0';
        voice_partial_timeout = 180; // keep visible for ~3 seconds
    }
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_theme(int dark_mode) {
    pthread_mutex_lock(&voice_mutex);
    voice_theme_change = dark_mode;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_ac_action(int temp_delta) {
    pthread_mutex_lock(&voice_mutex);
    voice_ac_change = temp_delta;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_info_request(int info_type) {
    pthread_mutex_lock(&voice_mutex);
    voice_info_request = info_type;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_action(int tab_index, bool call, bool end_call, const char* status) {
    pthread_mutex_lock(&voice_mutex);
    if (tab_index >= 0) voice_target_tab = tab_index;
    if (call) voice_make_call = true;
    if (end_call) voice_end_call = true;
    if (status) strncpy(voice_status_text, status, sizeof(voice_status_text) - 1);
    pthread_mutex_unlock(&voice_mutex);
}

void voice_button_callback(AromaNode* node, void *user_data) {
    trigger_manual_wake();
    system("(speaker-test -t sine -f 800 -l 1 >/dev/null 2>&1 & pid=$!; sleep 0.1; kill -9 $pid >/dev/null 2>&1) &");
    queue_voice_partial("Listening...");
}

void map_zoom_in_cb(void* user_data) {
    if (user_data) {
        aroma_map_zoom_in((AromaNode*)user_data);
    }
}

void map_zoom_out_cb(void* user_data) {
    if (user_data) {
        aroma_map_zoom_out((AromaNode*)user_data);
    }
}

void navigate(int index, void* user_data)
{  
    if(user_data) {
        switch (index) {
            case 0:
                aroma_map_pan_to((AromaNode*)user_data, 37.7749, -122.4194); // Pan to San Francisco
                break;
            case 1:
                aroma_map_pan_to((AromaNode*)user_data, 34.0522, -118.2437); // Pan to Los Angeles
                break;
            case 2:
                aroma_map_pan_to((AromaNode*)user_data, 40.7128, -74.0060); // Pan to New York
                break;
            case 3:
                aroma_map_pan_to((AromaNode*)user_data, 41.8781, -87.6298); // Pan to Chicago
                break;
            case 4:
                aroma_map_pan_to((AromaNode*)user_data, 47.6062, -122.3321); // Pan to Seattle
                break;
            default:
                break;
         }
    }
    
}

int main(void)
{
    aroma_ui_init();
    aroma_splash(true, "AromaOS", "Automotive HMI Demo");

    state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
    state.dark_theme_enabled = true;
    aroma_ui_set_theme(&state.theme);

    state.ui_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        16);
    state.icon_font = aroma_font_create_from_memory(
        icon_ttf,
        icon_ttf_len,
        24);
    state.dial_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        32);
    state.now_playing_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        20);

    state.window = aroma_ui_create_window(
        "Automotive HMI",
        WIN_W, WIN_H);

    aroma_event_set_root((AromaNode *)state.window);
    aroma_ui_prepare_font_for_window(0, state.ui_font);

    state.time_label = aroma_ui_label(
        (AromaNode *)state.window,
        "12:45 PM",
        50, 30,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);

    state.location_label = aroma_ui_label(
        (AromaNode *)state.window,
        "San Francisco, 68°F",
        150, 30,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.status_card = aroma_ui_card((AromaNode *)state.window, WIN_W - 235, 18, 200, 50, CARD_TYPE_FILLED);
    state.signal_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, state.theme.colors.primary, state.icon_font);
    state.wifi_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_WIFI, WIN_W - 80, 30, 24, state.theme.colors.primary, state.icon_font);
    state.battery_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BATTERY_FULL, WIN_W - 40, 30, 24, state.theme.colors.primary, state.icon_font);
    state.gps_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_GPS_FIXED, WIN_W - 160, 30, 24, state.theme.colors.primary, state.icon_font);
    state.bluetooth_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BLUETOOTH_AUDIO, WIN_W - 200, 30, 24, state.theme.colors.primary, state.icon_font);

    state.voice_button = aroma_ui_iconbutton((AromaNode *)state.window, AROMA_ICON_MIC, WIN_W - 290, 22, 40, ICON_BUTTON_FILLED, voice_button_callback, NULL, state.icon_font);
    
    state.voice_status_card = aroma_ui_card((AromaNode *)state.window, WIN_W/2 - 300, -20, 600, 80, CARD_TYPE_FILLED);
    aroma_node_set_hidden(state.voice_status_card, true);

    state.voice_status_label = aroma_ui_label(state.voice_status_card, "  ", 20, 40, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    
    state.general_root = aroma_ui_container((AromaNode *)state.window, 125, 90, WIN_W - 250, WIN_H - 210, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_gap((AromaNode *)state.general_root, 20);

    build_general_ui(state.general_root);
    build_settings_ui((AromaNode *)state.window);
    build_phone_ui((AromaNode *)state.window);
    build_music_ui((AromaNode *)state.window);

    state.tab_font = aroma_font_create_from_memory(
        icon_ttf, icon_ttf_len, 128);
    state.tabs = aroma_ui_tabs_with_icons((AromaNode *)state.window, 0, WIN_H - 80, WIN_W, 80,
                                          (const char *[]){"Main Screen", "Music", "Phone", "Settings", "Map"},
                                          (const char *[]){AROMA_ICON_DASHBOARD, AROMA_ICON_MUSIC_NOTE, AROMA_ICON_PHONE, AROMA_ICON_SETTINGS, AROMA_ICON_MAP},
                                          5, NULL, NULL, state.ui_font, state.tab_font);

    aroma_tabs_set_content(state.tabs, 0, (AromaNode **)&state.general_root, 1);
    aroma_tabs_set_content(state.tabs, 1, &state.music_root, 1);
    aroma_tabs_set_content(state.tabs, 2, &state.phone_root, 1);
    aroma_tabs_set_content(state.tabs, 3, &state.settings_root, 1);
      
    state.map_root = (AromaNode*)aroma_ui_container((AromaNode *)state.window, 0, 0, WIN_W, WIN_H - 80, AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    actual_map = (AromaNode *)aroma_map_create((AromaNode *)state.map_root, 0, 0, WIN_W, WIN_H - 80);
        aroma_map_set_show_attribution(actual_map, true);
        aroma_node_set_z_index(actual_map, -1);
    aroma_map_set_center(actual_map, 48.8566, 2.3522);
    aroma_map_add_marker(actual_map, 48.8566, 2.3522, 0xFFFF0000); 
    AromaNode* map_recently_visited_card = aroma_ui_card(state.map_root, 20, WIN_H - 500, 300, 400, CARD_TYPE_GLASS);
    aroma_node_set_z_index(map_recently_visited_card, 10);
    AromaNode* map_recently_visited_title = aroma_ui_label(map_recently_visited_card, "Recently Visited", 20, 20, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *recent_listview = aroma_ui_listview(map_recently_visited_card, 0, 60, 300, 400, navigate, actual_map,state.ui_font);
    aroma_listview_add_item(recent_listview, "Home", "123 Main St", NULL);
    aroma_listview_add_item(recent_listview, "Work", "456 Business Rd", NULL);
    aroma_listview_add_item(recent_listview, "Gym", "789 Fitness Ave", NULL);
    aroma_listview_add_item(recent_listview, "Supermarket", "321 Grocery Ln", NULL);
    aroma_listview_add_item(recent_listview, "Cafe", "654 Coffee St", NULL);
    aroma_listview_add_item(recent_listview, "Library", "987 Book Rd", NULL);
    aroma_listview_add_item(recent_listview, "Park", "246 Greenway Blvd", NULL);

    AromaNode* zoom_in = aroma_ui_iconbutton(state.map_root, AROMA_ICON_ADD, WIN_W - 70, 120, 50, ICON_BUTTON_FILLED, map_zoom_in_cb, (void*)actual_map, state.icon_font);
    AromaNode* zoom_out = aroma_ui_iconbutton(state.map_root, AROMA_ICON_REMOVE, WIN_W - 70, 180, 50, ICON_BUTTON_FILLED, map_zoom_out_cb, (void*)actual_map, state.icon_font);
    
    aroma_button_set_colors(zoom_in, state.theme.colors.primary, state.theme.colors.primary, state.theme.colors.secondary, state.theme.colors.text_primary);
    aroma_button_set_colors(zoom_out, state.theme.colors.primary, state.theme.colors.primary, state.theme.colors.secondary, state.theme.colors.text_primary);

    aroma_tabs_set_content(state.tabs, 4, &state.map_root, 1);

    start_voice_control_thread();

    while (aroma_ui_is_running())
    {
        pthread_mutex_lock(&voice_mutex);
        
        if (voice_nav_trigger) {
            double lat = 37.7749, lon = -122.4194;
            if (strstr(voice_nav_dest, "paris")) { lat = 48.8566; lon = 2.3522; }
            else if (strstr(voice_nav_dest, "london")) { lat = 51.5074; lon = -0.1278; }
            else if (strstr(voice_nav_dest, "new york")) { lat = 40.7128; lon = -74.0060; }
            else if (strstr(voice_nav_dest, "tokyo")) { lat = 35.6762; lon = 139.6503; }
            else if (strstr(voice_nav_dest, "berlin")) { lat = 52.5200; lon = 13.4050; }
            
            if (actual_map) {
                aroma_map_set_zoom(actual_map, 10);
                aroma_map_pan_to(actual_map, lat, lon);
            }
            voice_nav_trigger = false;
        }
        if (voice_target_tab != -1) {
            navigate_to_tab(voice_target_tab);
            voice_target_tab = -1;
        }
        if (voice_make_call) {
            dial_call_callback(NULL, NULL);
            voice_make_call = false;
        }
        if (voice_end_call) {
            dial_end_callback(NULL, NULL);
            voice_end_call = false;
        }
        if (strlen(voice_status_text) > 0) {
            set_voice_status(voice_status_text);
            voice_partial_timeout = 180; // Keep the final action message for 3 secs
            voice_status_text[0] = '\0';
        } else if (voice_partial_timeout > 0) {
            if (strlen(voice_partial_text) > 0) {
                set_voice_status(voice_partial_text);
            }
            voice_partial_timeout--;
            if (voice_partial_timeout == 0) {
                set_voice_status("");
                voice_partial_text[0] = '\0';
            }
        }
        
        if (voice_theme_change != -1) {
            bool want_dark = (voice_theme_change == 1);
            if (state.dark_theme_enabled != want_dark) {
                state.dark_theme_enabled = want_dark;
                if (want_dark) {
                    state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
                } else {
                    state.theme = aroma_theme_create_high_contrast();
                    state.theme.colors.primary = 0xFF2196F3;
                    state.theme.colors.primary_dark = 0xFF1976D2;
                    state.theme.colors.primary_light = 0xFFBBDEFB;
                }
                aroma_ui_set_theme(&state.theme);
            }
            voice_theme_change = -1;
        }

        if (voice_ac_change != 0) {
            current_ac_temp += voice_ac_change;
            if (current_ac_temp < 16) current_ac_temp = 16;
            if (current_ac_temp > 30) current_ac_temp = 30;
            char temp_str[16];
            snprintf(temp_str, sizeof(temp_str), "%d°C", current_ac_temp);
            aroma_label_set_text(state.ac_temp_label, temp_str);
            char speak_str[64];
            snprintf(speak_str, sizeof(speak_str), "Setting AC to %d degrees", current_ac_temp);
            aroma_voice_speak(speak_str);
            voice_ac_change = 0;
        }

        if (voice_info_request != 0) {
            if (voice_info_request == 1) {
                aroma_voice_speak("Battery is at 75 percent charge.");
            } else if (voice_info_request == 2) {
                aroma_voice_speak("Estimated range is 204 kilometers.");
            } else if (voice_info_request == 3) {
                aroma_voice_speak("Battery is at 75 percent. Estimated range is 204 kilometers.");
            }
            voice_info_request = 0;
        }

        if (voice_music_action != 0) {
            if (voice_music_action == 1) { // play
                if (!music_playing) {
                    music_play_callback(NULL, NULL);
                    aroma_voice_speak("Playing music");
                } else {
                    aroma_voice_speak("Music is already playing");
                }
            } else if (voice_music_action == 2) { // pause
                if (music_playing) {
                    music_play_callback(NULL, NULL);
                    aroma_voice_speak("Paused music");
                } else {
                    aroma_voice_speak("Music is not playing");
                }
            } else if (voice_music_action == 3) { // volume up
                music_volume += 0.1f;
                if (music_volume > 1.0f) music_volume = 1.0f;
                aroma_progressbar_set_progress(state.volume_slider, music_volume);
                aroma_voice_speak("Volume increased");
            } else if (voice_music_action == 4) { // volume down
                music_volume -= 0.1f;
                if (music_volume < 0.0f) music_volume = 0.0f;
                aroma_progressbar_set_progress(state.volume_slider, music_volume);
                aroma_voice_speak("Volume decreased");
            }
            voice_music_action = 0;
        }
        pthread_mutex_unlock(&voice_mutex);

        aroma_ui_process_events();
        aroma_ui_render(state.window);
        usleep(16000);
    }

    aroma_ui_destroy_window(state.window);
    aroma_ui_unload_font(state.ui_font);
    aroma_ui_shutdown();
    return 0;
}
  
void build_music_ui(AromaNode *window)
{
    int area_w = WIN_W - 250;
    int area_h = WIN_H - 210;

    state.music_root = aroma_container_create(window, 125, 90, area_w, area_h);

    state.music_container = aroma_ui_container(
        state.music_root, 20, 20, area_w - 40, area_h - 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)state.music_container, 20);

    AromaNode *bt_header = aroma_ui_container(
        state.music_container, 0, 0, area_w - 80, 60,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_CENTER);

    AromaNode *bt_info = aroma_ui_container(
        bt_header, 0, 0, 300, 60,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_START);

    state.bt_status_label = aroma_ui_label(
        bt_info,
        "Bluetooth Audio", 0, 0,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);

    state.bt_device_name = aroma_ui_label(
        bt_info,
        "Not Connected", 0, 25,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.bt_connect_button = aroma_ui_button(
        bt_header,
        "Connect", 0, 0, 100, 40,
        NULL, NULL,
        state.ui_font);

    AromaNode *now_playing_card = aroma_ui_card(
        state.music_container, 0, 0, area_w - 80, 300, CARD_TYPE_ELEVATED);

    AromaNode *now_playing_content = aroma_ui_container(
        now_playing_card, 20, 20, area_w - 120, 260,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)now_playing_content, 30);

    state.now_playing_art = aroma_ui_card(
        now_playing_content, 0, 0, 200, 200, CARD_TYPE_FILLED);

    AromaNode *now_playing_info = aroma_ui_container(
        now_playing_content, 0, 0, 300, 200,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_START);
    aroma_node_set_gap((AromaNode *)now_playing_info, 10);

    state.now_playing_title = aroma_ui_label(
        now_playing_info,
        "Blinding Lights", 0, 0,
        LABEL_STYLE_LABEL_LARGE, state.now_playing_font);

    state.now_playing_artist = aroma_ui_label(
        now_playing_info,
        "The Weeknd", 0, 0,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    AromaNode *progress_container = aroma_ui_container(
        now_playing_info, 0, 0, 300, 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_STRETCH);

    state.now_playing_progress = aroma_ui_progressbar(
        progress_container, 0, 0, 300, 4,
        PROGRESS_TYPE_DETERMINATE, 0.45f);

    AromaNode *time_container = aroma_ui_container(
        progress_container, 0, 5, 300, 20,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_CENTER);

    state.now_playing_time_elapsed = aroma_ui_label(
        time_container,
        "1:23", 0, 0,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.now_playing_time_total = aroma_ui_label(
        time_container,
        "3:45", 0, 0,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    AromaNode *controls = aroma_ui_container(
        now_playing_info, 0, 0, 300, 60,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)controls, 20);

    state.music_control_prev = aroma_ui_iconbutton(
        controls, AROMA_ICON_SKIP_PREVIOUS, 0, 0, 48,
        ICON_BUTTON_FILLED, NULL, NULL, state.icon_font);

    state.music_control_play = aroma_ui_iconbutton(
        controls, AROMA_ICON_PLAY_ARROW, 0, 0, 64,
        ICON_BUTTON_FILLED, music_play_callback, NULL, state.icon_font);

    state.music_control_next = aroma_ui_iconbutton(
        controls, AROMA_ICON_SKIP_NEXT, 0, 0, 48,
        ICON_BUTTON_FILLED, NULL, NULL, state.icon_font);

    AromaNode *volume_container = aroma_ui_container(
        state.music_container, 0, 0, area_w - 80, 60,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)volume_container, 20);

    state.music_control_volume = aroma_ui_icon(
        volume_container, AROMA_ICON_VOLUME_UP, 0, 0, 32,
        state.theme.colors.primary, state.icon_font);

    state.volume_slider = aroma_ui_progressbar(
        volume_container, 0, 0, 400, 4,
        PROGRESS_TYPE_DETERMINATE, 0.7f);

    AromaNode *devices_section = aroma_ui_container(
        state.music_container, 0, 0, area_w - 80, 150,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);

}
void build_phone_ui(AromaNode *window)
{
    int area_w = WIN_W - 250;
    int area_h = WIN_H - 210;

    state.phone_root = aroma_container_create(window, 125, 90, area_w, area_h);

    AromaNode *main_row = aroma_ui_container(
        state.phone_root, area_w/2 - 200, area_h/2 - 300, area_w - 40, area_h - 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)main_row, 20);

    AromaNode *dial_section = aroma_ui_container(
        main_row, 0, 0, 400, area_h - 80,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)dial_section, 20);

    state.dial_number_display = aroma_ui_label(
        dial_section,
        "", 0, 0,
        LABEL_STYLE_LABEL_LARGE, state.dial_font);

    state.call_status_label = aroma_ui_label(
        dial_section,
        "", 0, 0,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    AromaNode *dial_grid = aroma_ui_container(
        dial_section, 0, 0, 360, 360,
        AROMA_LAYOUT_MODE_GRID, 0, 0, 0);

    const char *dial_labels[] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "*", "0", "#"
    };

    aroma_node_set_grid_cols(dial_grid, 3);
    aroma_node_set_grid_rows(dial_grid, 4);
    aroma_node_set_gap((AromaNode *)dial_grid, 10);

    int button_index = 0;
    for (int row = 0; row < 12; row++) {
        AromaNode *btn = aroma_ui_button(
            dial_grid,
            dial_labels[button_index], 0, 0, 80, 80,
            dial_button_callback, (void *)(intptr_t)button_index,
            state.dial_font);
        state.dial_buttons[button_index] = btn;
        button_index++;
    }

    AromaNode *action_row = aroma_ui_container(
        dial_section, 0, 0, 360, 80,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)action_row, 20);

    state.dial_call_button = aroma_ui_iconbutton(
        action_row, AROMA_ICON_CALL, 0, 0, 64,
        ICON_BUTTON_FILLED, dial_call_callback, NULL, state.icon_font);

    state.dial_end_button = aroma_ui_iconbutton(
        action_row, AROMA_ICON_CALL_END, 0, 0, 64,
        ICON_BUTTON_FILLED, dial_end_callback, NULL, state.icon_font);

    state.dial_delete_button = aroma_ui_iconbutton(
        action_row, AROMA_ICON_BACKSPACE, 0, 0, 48,
        ICON_BUTTON_FILLED, dial_delete_callback, NULL, state.icon_font);

    AromaNode *lists_section = aroma_ui_container(
        main_row, 0, 300, area_w - 460, area_h - 80,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_gap((AromaNode *)lists_section, 20);

}

void listview_callback(int index, void *user_data)
{
    if (aroma_sidebar_get_selected(state.sidebar) == 0)
    {
    }
    else if (aroma_sidebar_get_selected(state.sidebar) == 1)
    {
        if (index == 2)
        {
            if (state.dark_theme_enabled)
            {
                state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
                aroma_ui_set_theme(&state.theme);
            }
            else
            {
                state.theme = aroma_theme_create_high_contrast();
                state.theme.colors.primary = 0xFF2196F3;
                state.theme.colors.primary_dark = 0xFF1976D2;
                state.theme.colors.primary_light = 0xFFBBDEFB;
                aroma_ui_set_theme(&state.theme);
            }
            state.dark_theme_enabled = !state.dark_theme_enabled;
        }
    }
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h, AromaFont *font)
{
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h, listview_callback, NULL, font);
    if (lv)
    {
        aroma_listview_set_icon_font(lv, state.icon_font);
    }
    return lv;
}

void build_settings_ui(AromaNode *window)
{
    int area_w = WIN_W - 250;
    int area_h = WIN_H - 210;
    int sidebar_w = 220;
    int panel_x = sidebar_w + 8;
    int panel_w = area_w - sidebar_w - 8;

    state.settings_root = aroma_container_create(window, 125, 90, area_w, area_h);

    const char *labels[] = {
        "Network",
        "Display",
        "Sound",
        "Navigation",
        "Security",
        "Apps",
        "Storage",
        "System",
        "About"};
    const char *icons[] = {
        AROMA_ICON_WIFI,
        AROMA_ICON_BRIGHTNESS_HIGH,
        AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP,
        AROMA_ICON_LOCK,
        AROMA_ICON_APPS,
        AROMA_ICON_STORAGE,
        AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO};
    int num_sections = 9;
    state.settings_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        18);

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections,
        NULL, NULL, state.settings_font, state.icon_font);

    state.listviews[0] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[0], "Wi-Fi", "HomeNetwork_5G", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Bluetooth", "3 devices paired", AROMA_ICON_BLUETOOTH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Hotspot & tethering", "Off", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Mobile data", "1.2 GB used", AROMA_ICON_DATA_USAGE, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Mobile network", "T-Mobile LTE", AROMA_ICON_NETWORK_CELL, NULL);
    state.listview_containers[0] = aroma_listview_get_scroll_container(state.listviews[0]);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[1], "Brightness level", "75%", AROMA_ICON_BRIGHTNESS_HIGH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Adaptive brightness", "On", AROMA_ICON_BRIGHTNESS_AUTO, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Dark theme", "On", AROMA_ICON_INVERT_COLORS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Auto-rotate screen", "On", AROMA_ICON_SCREEN_ROTATION, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Font size", "Medium", AROMA_ICON_VISIBILITY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Screen timeout", "5 minutes", AROMA_ICON_ACCESS_TIME, NULL);
    state.listview_containers[1] = aroma_listview_get_scroll_container(state.listviews[1]);

    state.listviews[2] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[2], "Media volume", "60%", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Navigation volume", "80%", AROMA_ICON_NAVIGATION, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Call volume", "90%", AROMA_ICON_NOTIFICATIONS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Notification sound", "Pixie Dust", AROMA_ICON_NOTIFICATIONS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Do Not Disturb", "Off", AROMA_ICON_DO_NOT_DISTURB, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Touch feedback", "On", AROMA_ICON_TUNE, NULL);
    state.listview_containers[2] = aroma_listview_get_scroll_container(state.listviews[2]);

    state.listviews[3] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[3], "Default navigation", "Built-in Maps", AROMA_ICON_MAP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Location services", "On", AROMA_ICON_GPS_FIXED, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Live traffic", "On", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Satellite view", "Off", AROMA_ICON_LOCATION_ON, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Voice guidance", "On", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_separator(state.listviews[3]);
    aroma_listview_add_item_with_icon(state.listviews[3], "Avoid toll roads", NULL, AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Avoid highways", NULL, AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Avoid ferries", NULL, AROMA_ICON_DIRECTIONS_CAR, NULL);
    state.listview_containers[3] = aroma_listview_get_scroll_container(state.listviews[3]);

    state.listviews[4] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[4], "Screen lock", "PIN", AROMA_ICON_LOCK, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Camera access", "Allowed", AROMA_ICON_VISIBILITY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Microphone access", "Allowed", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Location access", "Allowed", AROMA_ICON_LOCATION_ON, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Security scan", "Last scan: today", AROMA_ICON_SECURITY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Permission manager", NULL, AROMA_ICON_VERIFIED_USER, NULL);
    state.listview_containers[4] = aroma_listview_get_scroll_container(state.listviews[4]);

    state.listviews[5] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[5], "See all apps", "24 apps installed", AROMA_ICON_APPS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[5], "Notifications", "On", AROMA_ICON_NOTIFICATIONS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[5], "Default browser", "Chrome", AROMA_ICON_LINK, NULL);
    aroma_listview_add_item_with_icon(state.listviews[5], "Special app access", NULL, AROMA_ICON_ACCESSIBILITY, NULL);
    state.listview_containers[5] = aroma_listview_get_scroll_container(state.listviews[5]);

    state.listviews[6] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[6], "Internal storage", "32 GB / 64 GB used", AROMA_ICON_STORAGE, NULL);
    aroma_listview_add_separator(state.listviews[6]);
    aroma_listview_add_item_with_icon(state.listviews[6], "Apps", "18.2 GB", AROMA_ICON_APPS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Images & videos", "8.4 GB", AROMA_ICON_VISIBILITY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Audio", "3.1 GB", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "System", "2.3 GB", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "SD card", "Not inserted", AROMA_ICON_SD_STORAGE, NULL);
    state.listview_containers[6] = aroma_listview_get_scroll_container(state.listviews[6]);

    state.listviews[7] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[7], "Language", "English (US)", AROMA_ICON_LANGUAGE, NULL);
    aroma_listview_add_item_with_icon(state.listviews[7], "System update", "Up to date", AROMA_ICON_SYSTEM_UPDATE, NULL);
    aroma_listview_add_item_with_icon(state.listviews[7], "Backup", "Last: Mar 3, 2026", AROMA_ICON_BACKUP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[7], "Reset options", NULL, AROMA_ICON_RESTORE, NULL);
    aroma_listview_add_item_with_icon(state.listviews[7], "Developer options", "Off", AROMA_ICON_DEVELOPER_MODE, NULL);
    state.listview_containers[7] = aroma_listview_get_scroll_container(state.listviews[7]);

    state.listviews[8] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);

    char processor_name[256] = "Unknown";
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo)
    {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo))
        {
            if (strncmp(line, "model name", 10) == 0)
            {
                char *colon = strchr(line, ':');
                if (colon)
                {
                    strcpy(processor_name, colon + 2);
                    char *newline = strchr(processor_name, '\n');
                    if (newline)
                        *newline = '\0';
                }
                break;
            }
        }
        fclose(cpuinfo);
    }

    long total_ram_mb = 0;
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo)
    {
        char line[256];
        while (fgets(line, sizeof(line), meminfo))
        {
            if (strncmp(line, "MemTotal", 8) == 0)
            {
                long total_ram_kb;
                sscanf(line, "MemTotal: %ld kB", &total_ram_kb);
                total_ram_mb = total_ram_kb / 1024;
                break;
            }
        }
        fclose(meminfo);
    }

    char ram_str[64];
    if (total_ram_mb > 0)
    {
        if (total_ram_mb > 1024)
        {
            snprintf(ram_str, sizeof(ram_str), "%.1f GB", total_ram_mb / 1024.0);
        }
        else
        {
            snprintf(ram_str, sizeof(ram_str), "%ld MB", total_ram_mb);
        }
    }
    else
    {
        strcpy(ram_str, "NaN");
    }

    char uptime_str[64] = "Unknown";
    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file)
    {
        double uptime_seconds;
        if (fscanf(uptime_file, "%lf", &uptime_seconds) == 1)
        {
            int days = (int)(uptime_seconds / 86400);
            int hours = (int)((uptime_seconds - (days * 86400)) / 3600);
            int minutes = (int)((uptime_seconds - (days * 86400) - (hours * 3600)) / 60);

            if (days > 0)
            {
                snprintf(uptime_str, sizeof(uptime_str), "%d days, %d hrs", days, hours);
            }
            else if (hours > 0)
            {
                snprintf(uptime_str, sizeof(uptime_str), "%d hrs, %d min", hours, minutes);
            }
            else
            {
                snprintf(uptime_str, sizeof(uptime_str), "%d min", minutes);
            }
        }
        fclose(uptime_file);
    }

    char load_str[64] = "Unknown";
    FILE *loadavg = fopen("/proc/loadavg", "r");
    if (loadavg)
    {
        double load1, load5, load15;
        if (fscanf(loadavg, "%lf %lf %lf", &load1, &load5, &load15) == 3)
        {
            snprintf(load_str, sizeof(load_str), "%.2f, %.2f, %.2f", load1, load5, load15);
        }
        fclose(loadavg);
    }

    time_t rawtime;
    struct tm *timeinfo;
    char time_str[64];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    aroma_listview_add_item_with_icon(state.listviews[8], "Processor", processor_name, AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "RAM", ram_str, AROMA_ICON_STORAGE, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Vehicle name", "Aroma Automotive", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Software", "AromaHMI v0.0.1 Built with AromaSDK", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Build date", __DATE__ " " __TIME__, AROMA_ICON_BUILD, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Security patch", "March 1, 2026", AROMA_ICON_SECURITY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Uptime", uptime_str, AROMA_ICON_ACCESS_TIME, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Load average", load_str, AROMA_ICON_COMPUTER, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Current time", time_str, AROMA_ICON_ACCESS_TIME, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Platform Backend", "GLPS (X11)", AROMA_ICON_VERIFIED_USER, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[8], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);

    state.listview_containers[8] = aroma_listview_get_scroll_container(state.listviews[8]);

    for (int i = 0; i < 9; i++) {
        aroma_sidebar_set_content(state.sidebar, i, &state.listview_containers[i], 1);
    }

    aroma_sidebar_set_selected(state.sidebar, 8);

    aroma_node_set_hidden(state.settings_root, true);
}

void build_general_ui(AromaContainer *root)
{
    state.general_info_card = aroma_ui_card((AromaNode *)root, 0, 0, 400, 100, CARD_TYPE_FILLED);
    aroma_node_set_flex_grow(state.general_info_card, 1);

    state.applets_container = aroma_ui_container((AromaNode *)root, 0, 120, 610, 300, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    state.ac_applet = aroma_ui_card((AromaNode *)state.applets_container, 0, 0, 400, 300, CARD_TYPE_ELEVATED);
    aroma_node_set_gap((AromaNode *)state.applets_container, 20);

    state.applets_row = aroma_ui_container((AromaNode *)state.applets_container, 0, 130, 610, 270, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    state.system_status_card = aroma_ui_card((AromaNode *)state.applets_row, 0, 0, 280, 70, CARD_TYPE_ELEVATED);
    AromaNode *applet2 = aroma_ui_image((AromaNode *)state.applets_row, "../test.png", 0, 0, 280, 70);
    aroma_node_set_gap((AromaNode *)state.applets_row, 20);
    aroma_node_set_flex_grow(state.system_status_card, 1);
    aroma_node_set_flex_grow(applet2, 1);

    state.vehicle_image = aroma_ui_image(
        (AromaNode *)state.general_info_card,
        "../car.png",
        40, 20,
        264 * 1.2, 126 * 1.2);

    state.vehicle_status_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "Vehicle Status: All systems normal",
        90, 200,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.battery_progress = aroma_ui_progressbar(
        (AromaNode *)state.general_info_card,
        30, 260,
        360, 20,
        PROGRESS_TYPE_DETERMINATE, 0.75f);

    state.battery_icon_large = aroma_ui_icon((AromaNode *)state.general_info_card, AROMA_ICON_BATTERY_CHARGING_FULL, 40, 305, 48, state.theme.colors.primary, state.icon_font);

    state.ac_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);

    state.range_value_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "204",
        90, 310,
        LABEL_STYLE_LABEL_MEDIUM, state.ac_font);

    state.range_unit_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "km",
        90 + aroma_font_get_line_width(state.ac_font, "204"), 320,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.range_desc_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "Remaining",
        90, 340,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.divider1 = aroma_ui_divider(
        (AromaNode *)state.general_info_card,
        180, 300,
        80, DIVIDER_ORIENTATION_VERTICAL);

    state.consumption_value_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "128",
        200, 310,
        LABEL_STYLE_LABEL_MEDIUM, state.ac_font);

    state.consumption_unit_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "Wh",
        200 + aroma_font_get_line_width(state.ac_font, "128"), 320,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.consumption_desc_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "Average",
        200, 340,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.divider2 = aroma_ui_divider(
        (AromaNode *)state.general_info_card,
        290, 300,
        80, DIVIDER_ORIENTATION_VERTICAL);

    state.capacity_value_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "35.5",
        310, 310,
        LABEL_STYLE_LABEL_MEDIUM, state.ac_font);

    state.capacity_unit_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "kWh",
        310 + aroma_font_get_line_width(state.ac_font, "35.5"), 320,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.capacity_desc_label = aroma_ui_label(
        (AromaNode *)state.general_info_card,
        "Fuel Capacity",
        310, 340,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    state.speed_card = aroma_ui_card(
        (AromaNode *)state.general_info_card,
        20, 380,
        200, 200, CARD_TYPE_FILLED);

    state.speed_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 76);

    state.speed_title_label = aroma_ui_label((AromaNode *)state.speed_card, "Speed", 20, 20, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.speed_value_label = aroma_ui_label(
        (AromaNode *)state.speed_card,
        "88",
        20, 40,
        LABEL_STYLE_LABEL_LARGE, state.speed_font);

    state.speed_unit_label = aroma_ui_label(
        (AromaNode *)state.speed_card,
        "km/h",
        20, 160,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.speed_divider = aroma_ui_divider(
        (AromaNode *)state.speed_card,
        150, 20,
        170, DIVIDER_ORIENTATION_VERTICAL);

    state.gear_label = aroma_ui_label(
        (AromaNode *)state.speed_card,
        "Gear: Drive",
        170, 40,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.gear_card = aroma_ui_card(
        (AromaNode *)state.general_info_card,
        190, 450,
        200, 50, CARD_TYPE_ELEVATED);

    static const char *gears[4] = {"P", "R", "N", "D"};

    for (int i = 0; i < 4; ++i)
    {
        if (i == 3)
        {
            state.gear_highlight_card = aroma_ui_card(
                (AromaNode *)state.gear_card,
                (i * 40) + 20, 10,
                40, 30, CARD_TYPE_FILLED);
        }
        state.gear_labels[i] = aroma_ui_label(
            (AromaNode *)state.gear_card,
            gears[i],
            (i * 41) + 30, 8,
            LABEL_STYLE_LABEL_MEDIUM, state.ac_font);
        aroma_node_set_z_index(state.gear_labels[i], 99999999);
    }

    state.ac_title_label = aroma_ui_label((AromaNode *)state.ac_applet, "Adjust AC to your comfort level", 20, 20, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    state.ac_temp_label = aroma_ui_label((AromaNode *)state.ac_applet, "23°F", 225, 60, LABEL_STYLE_LABEL_LARGE, state.speed_font);

    state.ac_temp_down_btn = aroma_ui_iconbutton((AromaNode *)state.ac_applet, AROMA_ICON_REMOVE, 145, 100, 48, ICON_BUTTON_FILLED, NULL, NULL, state.icon_font);
    state.ac_temp_up_btn = aroma_ui_iconbutton((AromaNode *)state.ac_applet, AROMA_ICON_ADD, 400, 100, 48, ICON_BUTTON_FILLED, NULL, NULL, state.icon_font);

    state.ac_control_card = aroma_ui_card((AromaNode *)state.ac_applet, 130, 200, 330, 80, CARD_TYPE_FILLED);

    state.ac_image1 = aroma_ui_image(
        (AromaNode *)state.ac_applet,
        "../air-conditioner.png",
        170, 215,
        48, 48);

    state.ac_image2 = aroma_ui_image(
        (AromaNode *)state.ac_applet,
        "../under.png",
        270, 215,
        48, 48);

    state.ac_image3 = aroma_ui_image(
        (AromaNode *)state.ac_applet,
        "../both.png",
        370, 215,
        48, 48);

    state.system_status_title = aroma_ui_label((AromaNode *)state.system_status_card, "System Status", 20, 20, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    int icon_x_positions[] = {20, 70, 115, 120, 170, 220, 20, 70};
    int icon_y_positions[] = {60, 60, 55, 60, 60, 60, 120, 120};
    const char *icon_paths[] = {
        "../brake_indicator.png",
        "../abs_indicator.png",
        NULL,
        "../high_beams.png",
        "../low_beams.png",
        "../seatbelt.png",
        "../battery_indicator.png",
        "../temperature.png"
    };

    for (int i = 0; i < 8; i++) {
        if (i == 2) {
            aroma_ui_card((AromaNode *)state.system_status_card, icon_x_positions[i], icon_y_positions[i], 42, 42, CARD_TYPE_FILLED);
        } else {
            state.status_icons[i < 2 ? i : (i > 2 ? i-1 : i)] = aroma_ui_image(
                (AromaNode *)state.system_status_card,
                icon_paths[i],
                icon_x_positions[i], icon_y_positions[i],
                32, 32);
        }
    }
}
