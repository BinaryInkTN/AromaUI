/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_native_utils.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
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
    AromaNode *loading_spinner;

    AromaTheme theme;
    bool dark_theme_enabled;
    char current_number[20];
    AromaNode *easter_egg_overlay;
    AromaNode *easter_egg_icon;
    AromaNode *debug_overlay;
    bool debug_overlay_visible;
} AppState;

static AppState state = {0};

void build_general_ui(AromaContainer *root);
void build_settings_ui(AromaNode *window);

void toggle_recent_card_cb(void* user_data) {
    AromaNode* card = (AromaNode*)user_data;
    if (!card) return;

    AromaRect* rect = (AromaRect*)card->node_widget_ptr;
    if (rect) {
        if (rect->x < 0) {
            
            aroma_animation_start(card, AROMA_ANIM_SLIDE_X, rect->x, 20, 300);
        } else {
            
            aroma_animation_start(card, AROMA_ANIM_SLIDE_X, rect->x, -350, 300);
        }
    }
}

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

void ac_temp_up_callback(AromaNode* node, void *user_data) {
    if (current_ac_temp < 30) current_ac_temp++;
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%d°C", current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, temp_str);
}

void ac_temp_down_callback(AromaNode* node, void *user_data) {
    if (current_ac_temp > 16) current_ac_temp--;
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%d°C", current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, temp_str);
}

void music_play_callback(AromaNode* node, void *user_data) {
    if (!music_playing) {
        music_playing = true;
        aroma_iconbutton_set_icon(state.music_control_play, AROMA_ICON_PAUSE);
        system("aplay ../assets/sample.wav >/dev/null 2>&1 &");
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

static bool voice_is_visible = false;

void set_voice_status(const char* status) {
    if (state.voice_status_label) {
        aroma_label_set_text(state.voice_status_label, status);
    }
    if (state.voice_status_card) {
        bool hide = (status == NULL || strlen(status) == 0);
        if (!hide && !voice_is_visible) {
            aroma_node_set_hidden(state.voice_status_card, false);
            aroma_node_set_hidden(state.loading_spinner, false);
            aroma_animation_start((AromaNode*)state.voice_status_card, AROMA_ANIM_SLIDE_Y, -100, -20, 300);
            voice_is_visible = true;
        } else if (hide && voice_is_visible) {
            aroma_animation_start((AromaNode*)state.voice_status_card, AROMA_ANIM_SLIDE_Y, -20, -100, 300);
            voice_is_visible = false;
            aroma_node_set_hidden(state.loading_spinner, true);
        }
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
static int voice_music_action = 0; 

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
        voice_partial_timeout = 180; 
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
        AromaNode* map = (AromaNode*)user_data;
        aroma_map_clear_route(map);
        switch (index) {
            case 0:
                aroma_map_pan_to(map, 48.8566, 2.3522); 
                aroma_map_set_zoom(map, 12);
                aroma_map_set_route(map, 48.8566, 2.3522, 48.8049, 2.1204, 0xFF35A8FE); 
                aroma_map_add_popup_marker(map, 48.8566, 2.3522, 0xFF00C853, "Start: Paris");
                aroma_map_add_popup_marker(map, 48.8049, 2.1204, 0xFFD50000, "Home: Versailles");
                break;
            case 1:
                aroma_map_pan_to(map, 51.5074, -0.1278); 
                aroma_map_set_zoom(map, 11);
                aroma_map_set_route(map, 51.5074, -0.1278, 51.4700, -0.4543, 0xFF35A8FE); 
                aroma_map_add_popup_marker(map, 51.5074, -0.1278, 0xFF00C853, "Start: London");
                aroma_map_add_popup_marker(map, 51.4700, -0.4543, 0xFFD50000, "Work: Heathrow");
                break;
            case 2:
                aroma_map_pan_to(map, 52.5200, 13.4050); 
                aroma_map_set_zoom(map, 11);
                aroma_map_set_route(map, 52.5200, 13.4050, 52.3667, 13.5033, 0xFF35A8FE); 
                aroma_map_add_popup_marker(map, 52.5200, 13.4050, 0xFF00C853, "Start: Berlin");
                aroma_map_add_popup_marker(map, 52.3667, 13.5033, 0xFFD50000, "Gym: BER Airport");
                break;
            case 3:
                aroma_map_pan_to(map, 41.9028, 12.4964); 
                aroma_map_set_zoom(map, 12);
                aroma_map_set_route(map, 41.9028, 12.4964, 41.7999, 12.2462, 0xFF35A8FE); 
                aroma_map_add_popup_marker(map, 41.9028, 12.4964, 0xFF00C853, "Start: Colosseum");
                aroma_map_add_popup_marker(map, 41.7999, 12.2462, 0xFFD50000, "Supermarket: FCO");
                break;
            case 4:
                aroma_map_pan_to(map, 48.1351, 11.5820); 
                aroma_map_set_zoom(map, 11);
                aroma_map_set_route(map, 48.1351, 11.5820, 48.3537, 11.7861, 0xFF35A8FE); 
                aroma_map_add_popup_marker(map, 48.1351, 11.5820, 0xFF00C853, "Start: Marienplatz");
                aroma_map_add_popup_marker(map, 48.3537, 11.7861, 0xFFD50000, "Cafe: MUC Airport");
                break;
            default:
                break;
         }
    }
    
}

void close_easter_egg_cb(void* user_data) {
    if (state.easter_egg_overlay) {
        aroma_node_set_hidden(state.easter_egg_overlay, true);
    }
}

static float bounce_start_x = 0;
static float bounce_start_y = 0;
static float bounce_end_x = 0;
static float bounce_end_y = 0;

void bounce_anim_cb(AromaNode* target, float current_val, void* user_data) {
    if (!target) return;
    AromaRect* r = (AromaRect*)target->node_widget_ptr;
    if (r) {
        r->x = bounce_start_x + (bounce_end_x - bounce_start_x) * current_val;
        r->y = bounce_start_y + (bounce_end_y - bounce_start_y) * current_val;
    }
}

void interact_easter_egg_cb(void* user_data) {
    if (state.easter_egg_icon) {
        AromaRect* r = (AromaRect*)state.easter_egg_icon->node_widget_ptr;
        if (r) {
            bounce_start_x = r->x;
            bounce_start_y = r->y;
            bounce_end_x = 50 + (rand() % (WIN_W - 200));
            bounce_end_y = 50 + (rand() % (WIN_H - 200));
            
            AromaAnimation* anim = aroma_animation_start_custom(state.easter_egg_icon, 0.0f, 1.0f, 800, bounce_anim_cb, NULL);
            if (anim) {
                aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);
            }
        }
    }
}

void build_easter_egg_ui(AromaNode *window) {
    state.easter_egg_overlay = aroma_ui_card(window, 0, 0, WIN_W, WIN_H, CARD_TYPE_GLASS);
    if (!state.easter_egg_overlay) return;
    
    aroma_node_set_z_index(state.easter_egg_overlay, INT_MAX);
    aroma_node_set_hidden(state.easter_egg_overlay, true);

    state.easter_egg_icon = aroma_ui_iconbutton(state.easter_egg_overlay, AROMA_ICON_BUG_REPORT, WIN_W/2 - 50, WIN_H/2 - 50, 100, ICON_BUTTON_FILLED, interact_easter_egg_cb, NULL, state.icon_font);
    aroma_node_set_z_index(state.easter_egg_icon, INT_MAX);

    AromaNode* close_btn = aroma_ui_iconbutton(state.easter_egg_overlay, AROMA_ICON_CLOSE, WIN_W - 80, 30, 50, ICON_BUTTON_OUTLINED, close_easter_egg_cb, NULL, state.icon_font);
    aroma_node_set_z_index(close_btn, INT_MAX);
}

bool global_keyboard_event_handler(AromaEvent *event, void *user_data) {
    if (event->event_type == EVENT_TYPE_KEY_PRESS) {
        if ((event->data.key.key_code == 'i' || event->data.key.key_code == 'I') && (event->data.key.modifiers & AROMA_KEY_MOD_CTRL)) {
            state.debug_overlay_visible = !state.debug_overlay_visible;
            if (state.debug_overlay) {
                aroma_debug_overlay_set_visible(state.debug_overlay, state.debug_overlay_visible);
                aroma_node_invalidate((AromaNode *)state.window);
            }
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv) 
{

    aroma_animation_manager_init();
    aroma_splash(true, "AromaHMI 0.0.1", "The Ultimate Car Infotainment Demo");
  
    aroma_ui_init();

    state.theme = aroma_theme_create_material_blue_dark();

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

    aroma_event_subscribe(((AromaNode *)state.window)->node_id, EVENT_TYPE_KEY_PRESS, global_keyboard_event_handler, NULL, 0);

    aroma_ui_prepare_font_for_window(0, state.ui_font);

    state.time_label = aroma_ui_label(
        (AromaNode *)state.window,
        "12:45 PM",
        50, 30,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.time_label, 999999);

    state.location_label = aroma_ui_label(
        (AromaNode *)state.window,
        "San Francisco, 68°F",
        150, 30,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.location_label, 999999);

    state.status_card = aroma_ui_card((AromaNode *)state.window, WIN_W - 235, 18, 200, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.status_card, 999999);
    
    state.signal_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.signal_icon, 999999);
    
    state.wifi_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_WIFI, WIN_W - 80, 30, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.wifi_icon, 999999);
    
    state.battery_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BATTERY_FULL, WIN_W - 40, 30, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.battery_icon, 999999);
    
    state.gps_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_GPS_FIXED, WIN_W - 160, 30, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.gps_icon, 999999);
    
    state.bluetooth_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BLUETOOTH_AUDIO, WIN_W - 200, 30, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.bluetooth_icon, 999999);

    state.voice_button = aroma_ui_iconbutton((AromaNode *)state.window, AROMA_ICON_MIC, WIN_W - 290, 22, 40, ICON_BUTTON_FILLED, voice_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.voice_button, 999999);
    
    aroma_animation_start(state.time_label, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.status_card, AROMA_ANIM_SLIDE_Y, -40, 18, 800);
    aroma_animation_start(state.location_label, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.signal_icon, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.wifi_icon, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.battery_icon, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.gps_icon, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.bluetooth_icon, AROMA_ANIM_SLIDE_Y, -40, 30, 800);
    aroma_animation_start(state.voice_button, AROMA_ANIM_SLIDE_Y, -40, 22, 800  );

    state.voice_status_card = aroma_ui_card((AromaNode *)state.window, WIN_W/2 - 300, -100, 600, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.voice_status_card, 99998);
    aroma_node_set_hidden(state.voice_status_card, false);

    state.voice_status_label = aroma_ui_label(state.voice_status_card, "  ", 20, 40, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    state.loading_spinner = aroma_ui_loading(state.voice_status_card, 530, 28, 22, 5, state.theme.colors.primary);
    aroma_node_set_z_index(state.loading_spinner, 99999);
    aroma_node_set_z_index(state.voice_status_label, 99999);
    aroma_node_set_hidden(state.loading_spinner, true);
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

    aroma_animation_start(state.tabs, AROMA_ANIM_SLIDE_Y, WIN_H+80, WIN_H - 80, 1200);

    aroma_tabs_set_content(state.tabs, 0, (AromaNode **)&state.general_root, 1);
    aroma_tabs_set_content(state.tabs, 1, &state.music_root, 1);
    aroma_tabs_set_content(state.tabs, 2, &state.phone_root, 1);
    aroma_tabs_set_content(state.tabs, 3, &state.settings_root, 1);

    aroma_animation_start(state.general_root, AROMA_ANIM_FADE, 0, 1, 1200);
      
    state.map_root = (AromaNode*)aroma_ui_container((AromaNode *)state.window, 0, 0, WIN_W, WIN_H - 80, AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    actual_map = aroma_ui_map((AromaNode *)state.map_root, 0, 0, WIN_W, WIN_H - 80);
    
    aroma_node_set_z_index(actual_map, 0);
    aroma_map_set_show_attribution(actual_map, false);
    aroma_map_set_center(actual_map, 48.8566, 2.3522);
    aroma_map_set_zoom(actual_map, 12);
    aroma_map_set_route(actual_map, 48.8566, 2.3522, 48.8049, 2.1204, 0xFF35A8FE); 
    aroma_map_add_popup_marker(actual_map, 48.8566, 2.3522, 0xFF00C853, "Start: Paris");
    aroma_map_add_popup_marker(actual_map, 48.8049, 2.1204, 0xFFD50000, "Home: Versailles");
    aroma_map_add_icon_marker(actual_map, 48.8606, 2.3376, 0xFFFFD600, AROMA_ICON_STAR); 
    AromaNode* map_recently_visited_card = aroma_ui_card(state.map_root, -350, WIN_H - 500, 300, 400, CARD_TYPE_GLASS);
    aroma_node_set_z_index(map_recently_visited_card, 10);
    
    AromaNode* toggle_recent_btn = aroma_ui_iconbutton(state.map_root, AROMA_ICON_HISTORY,  WIN_W - 70, 240, 50, ICON_BUTTON_FILLED, toggle_recent_card_cb, (void*)map_recently_visited_card, state.icon_font);
    aroma_node_set_z_index(toggle_recent_btn, 5);

    AromaNode* map_recently_visited_title = aroma_ui_label(map_recently_visited_card, "Recently Visited", 20, 20, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    
    AromaNode* close_recent_btn = aroma_ui_iconbutton(map_recently_visited_card, AROMA_ICON_CLOSE, 240, 10, 40, ICON_BUTTON_OUTLINED, toggle_recent_card_cb, (void*)map_recently_visited_card, state.icon_font);

    AromaNode *recent_listview = aroma_ui_listview(map_recently_visited_card, 0, 60, 300, 300, navigate, actual_map,state.ui_font);
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

    aroma_tabs_set_transition(state.tabs, AROMA_ANIM_SLIDE_X, 400);
    aroma_sidebar_set_transition(state.sidebar, AROMA_ANIM_FADE, 300);

    build_easter_egg_ui((AromaNode*)state.window);

    state.debug_overlay = aroma_ui_debug_overlay((AromaNode *)state.window, WIN_W - 220, 100, 200, state.ui_font);
    state.debug_overlay_visible = false;
    aroma_debug_overlay_set_visible(state.debug_overlay, false);
    aroma_node_set_z_index(state.debug_overlay, INT_MAX - 1);

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
            voice_partial_timeout = 180; 
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
            if (voice_music_action == 1) { 
                if (!music_playing) {
                    music_play_callback(NULL, NULL);
                    aroma_voice_speak("Playing music");
                } else {
                    aroma_voice_speak("Music is already playing");
                }
            } else if (voice_music_action == 2) { 
                if (music_playing) {
                    music_play_callback(NULL, NULL);
                    aroma_voice_speak("Paused music");
                } else {
                    aroma_voice_speak("Music is not playing");
                }
            } else if (voice_music_action == 3) { 
                music_volume += 0.1f;
                if (music_volume > 1.0f) music_volume = 1.0f;
                aroma_progressbar_set_progress(state.volume_slider, music_volume);
                aroma_voice_speak("Volume increased");
            } else if (voice_music_action == 4) { 
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
  
static AromaNode *devices_dialog = NULL;

static void close_devices_dialog(void *user_data) {
    if (devices_dialog) {
        aroma_dialog_hide(devices_dialog);
    }
}

static void connect_device_callback(void *user_data) {
    if (devices_dialog) {
        aroma_dialog_hide(devices_dialog);
        aroma_label_set_text(state.bt_device_name, "Connected to new device");
    }
}

static void open_devices_dialog_cb(AromaNode *node, void *user_data) {
    if (!devices_dialog) {
        devices_dialog = aroma_dialog_create((AromaNode*)state.window, "Available Devices", "", 460, 400, DIALOG_TYPE_BASIC);
        if (devices_dialog) {
            aroma_dialog_set_font(devices_dialog, state.ui_font);
            
            AromaNode *content = aroma_dialog_get_content_area(devices_dialog);
            if (content) {
                AromaNode *devices_listview = aroma_ui_listview(content, 0, 0, 420, 200, NULL, NULL, state.ui_font);
                if (devices_listview) {
                    aroma_listview_set_icon_font(devices_listview, state.icon_font);
                    aroma_listview_add_item_with_icon(devices_listview, "BMW X5 Audio", "Connected", "\ue328", NULL); // AROMA_ICON_BLUETOOTH
                    aroma_listview_add_item_with_icon(devices_listview, "AirPods Pro", "Paired", "\ue310", NULL); // AROMA_ICON_HEADPHONES
                    aroma_listview_add_item_with_icon(devices_listview, "iPhone 15", "Available", "\ue32c", NULL); // AROMA_ICON_SMARTPHONE
                }
            }
            
            aroma_dialog_add_action(devices_dialog, "Cancel", close_devices_dialog, NULL);
            aroma_dialog_add_action(devices_dialog, "Connect", connect_device_callback, NULL);
        }
    }
    
    if (devices_dialog) {
        aroma_dialog_show(devices_dialog);
    }
}
void build_music_ui(AromaNode *window)
{
    int area_w = WIN_W - 250;
    int area_h = WIN_H - 210;

    state.music_root = aroma_container_create(window, 125, 90, area_w, area_h);

    state.music_container = aroma_ui_container(
        state.music_root, 20, 20, area_w - 40, area_h - 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_CENTER);

    AromaNode *bt_header = aroma_ui_container(
        state.music_container, 0, 0, area_w - 40, 60,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_CENTER);

    AromaNode *bt_info = aroma_ui_container(
        bt_header, 0, 0, 300, 60,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_START);

    state.bt_status_label = aroma_ui_label(
        bt_info,
        "Your Library", 0, 0,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);

    state.bt_device_name = aroma_ui_label(
        bt_info,
        "Playing from iPhone", 0, 25,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    state.bt_connect_button = aroma_ui_button(
        bt_header,
        "Devices", 0, 0, 100, 40,
        open_devices_dialog_cb, NULL,
        state.ui_font);

    AromaNode *middle_content = aroma_ui_container(
        state.music_container, 0, 0, area_w - 40, 280,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)middle_content, 40);

    state.now_playing_art =  state.now_playing_art = aroma_ui_image(
        middle_content, "../assets/album_cover.jpg", 0, 0, 280, 280);


    AromaNode *now_playing_info = aroma_ui_container(
        middle_content, 0, 0, 300, 280,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_START);
    aroma_node_set_gap((AromaNode *)now_playing_info, 10);

    state.now_playing_title = aroma_ui_label(
        now_playing_info,
        "good kid, m.A.A.d city", 0, 0,
        LABEL_STYLE_LABEL_LARGE, state.now_playing_font);

    state.now_playing_artist = aroma_ui_label(
        now_playing_info,
        "Kendrick Lamar", 0, 0,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    AromaNode *bottom_bar = aroma_ui_container(
        state.music_container, 0, 0, area_w - 40, 120,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_STRETCH);
    aroma_node_set_gap((AromaNode *)bottom_bar, 10);

    AromaNode *progress_container = aroma_ui_container(
        bottom_bar, 0, 0, area_w - 40, 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)progress_container, 15);

    state.now_playing_time_elapsed = aroma_ui_label(
        progress_container,
        "1:23", 0, 0,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    int progress_bar_width = area_w - 40 - 15 - 40 - 15 - 40;
    state.now_playing_progress = aroma_ui_progressbar(
        progress_container, 0, 0, progress_bar_width, 6,
        PROGRESS_TYPE_DETERMINATE, 0.45f);

    state.now_playing_time_total = aroma_ui_label(
        progress_container,
        "3:45", 0, 0,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);

    AromaNode *controls_row = aroma_ui_container(
        bottom_bar, 0, 0, area_w - 40, 64,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_CENTER);

    AromaNode *controls_left = aroma_ui_container(
        controls_row, 0, 0, 150, 64, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    
    AromaNode *like_button = aroma_ui_iconbutton(
        controls_left, AROMA_ICON_FAVORITE_BORDER, 0, 0, 48,
        ICON_BUTTON_STANDARD, NULL, NULL, state.icon_font);

    AromaNode *play_controls = aroma_ui_container(
        controls_row, 0, 0, 250, 64,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)play_controls, 15);

    state.music_control_prev = aroma_ui_iconbutton(
        play_controls, AROMA_ICON_SKIP_PREVIOUS, 0, 0, 48,
        ICON_BUTTON_STANDARD, NULL, NULL, state.icon_font);

    state.music_control_play = aroma_ui_iconbutton(
        play_controls, AROMA_ICON_PLAY_ARROW, 0, 0, 64,
        ICON_BUTTON_FILLED, music_play_callback, NULL, state.icon_font);

    state.music_control_next = aroma_ui_iconbutton(
        play_controls, AROMA_ICON_SKIP_NEXT, 0, 0, 48,
        ICON_BUTTON_STANDARD, NULL, NULL, state.icon_font);

    AromaNode *controls_right = aroma_ui_container(
        controls_row, 0, 0, 200, 64,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_END, AROMA_ALIGN_CENTER);
    aroma_node_set_gap((AromaNode *)controls_right, 10);

    state.music_control_volume = aroma_ui_icon(
        controls_right, AROMA_ICON_VOLUME_UP, 0, 0, 24,
        state.theme.colors.primary, state.icon_font);

    state.volume_slider = aroma_ui_progressbar(
        controls_right, 0, 0, 100, 4,
        PROGRESS_TYPE_DETERMINATE, 0.7f);
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
    int selected = aroma_sidebar_get_selected(state.sidebar);
    if (selected == 1)
    {
        if (index == 1)
        {
            if (state.dark_theme_enabled)
            {
                state.theme = aroma_theme_create_high_contrast();
                state.theme.colors.primary = 0xFF2196F3;
                state.theme.colors.primary_dark = 0xFF1976D2;
                state.theme.colors.primary_light = 0xFFBBDEFB;
                aroma_ui_set_theme(&state.theme);
            }
            else
            {
                state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
                aroma_ui_set_theme(&state.theme);
            }
            state.dark_theme_enabled = !state.dark_theme_enabled;
        }
    }
    else if (selected == 6)
    {
        static int build_clicks = 0;
        if (index == 4) 
        {
            build_clicks++;
            if (build_clicks > 2 && build_clicks < 7) {
                char msg[64];
                snprintf(msg, sizeof(msg), "You are now %d steps away from being a developer.", 7 - build_clicks);
                queue_voice_partial(msg);
            } else if (build_clicks >= 7) {
                if (build_clicks == 7) {
                    queue_voice_action(-1, false, false, "You are now a developer!");
                    system("(speaker-test -t sine -f 1200 -l 1 >/dev/null 2>&1 & pid=$!; sleep 0.15; kill -9 $pid >/dev/null 2>&1) &");
                }
                aroma_node_set_hidden(state.easter_egg_overlay, false);
            }
        }
        else
        {
            build_clicks = 0;
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

bool g_voice_assistant_enabled = true;

void toggle_tab_animation_cb(AromaNode* sender, void* user_data) {
    static bool is_slide = true;
    is_slide = !is_slide;
    if (is_slide) {
        aroma_tabs_set_transition(state.tabs, AROMA_ANIM_SLIDE_X, 300);
    } else {
        aroma_tabs_set_transition(state.tabs, AROMA_ANIM_FADE, 300);
    }
}

void toggle_voice_assistant_cb(AromaNode* sender, void* user_data) {
    g_voice_assistant_enabled = !g_voice_assistant_enabled;
    printf("Voice assistant %s\n", g_voice_assistant_enabled ? "enabled" : "disabled");
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
        "Connectivity",
        "Display & Theme",
        "Sound & Media",
        "Navigation",
        "Vehicle & Climate",
        "Behaviors",
        "System & About"
    };
    const char *icons[] = {
        AROMA_ICON_WIFI,
        AROMA_ICON_BRIGHTNESS_HIGH,
        AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP,
        AROMA_ICON_DIRECTIONS_CAR,
        AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO
    };
    int num_sections = 7;
    state.settings_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        18);

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections,
        NULL, NULL, state.settings_font, state.icon_font);

    state.listviews[0] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[0], "Wi-Fi", "Connected - AutoNet", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Bluetooth", "1 Device Paired", AROMA_ICON_BLUETOOTH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Mobile data", "5G connection active", AROMA_ICON_NETWORK_CELL, NULL);
    state.listview_containers[0] = aroma_listview_get_scroll_container(state.listviews[0]);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[1], "Brightness level", "Adaptive", AROMA_ICON_BRIGHTNESS_HIGH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Dark theme", "Toggle dark/light mode", AROMA_ICON_INVERT_COLORS, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Auto-rotate screen", "On", AROMA_ICON_SCREEN_ROTATION, NULL);
    state.listview_containers[1] = aroma_listview_get_scroll_container(state.listviews[1]);

    state.listviews[2] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[2], "Media volume", "70%", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Navigation volume", "80%", AROMA_ICON_NAVIGATION, NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "System sounds", "On", AROMA_ICON_NOTIFICATIONS, NULL);
    state.listview_containers[2] = aroma_listview_get_scroll_container(state.listviews[2]);

    state.listviews[3] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[3], "Location services", "High accuracy", AROMA_ICON_GPS_FIXED, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Live traffic", "On", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Voice guidance", "On", AROMA_ICON_VOLUME_UP, NULL);
    state.listview_containers[3] = aroma_listview_get_scroll_container(state.listviews[3]);

    state.listviews[4] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[4], "Climate settings", "Auto mode", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Vehicle diagnostics", "All systems normal", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Drive mode", "Comfort", AROMA_ICON_DIRECTIONS_CAR, NULL);
    state.listview_containers[4] = aroma_listview_get_scroll_container(state.listviews[4]);

    state.listviews[5] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);
    aroma_listview_add_item_with_icon(state.listviews[5], "Tab Transition", "Toggle Fade/Slide", AROMA_ICON_SETTINGS, toggle_tab_animation_cb);
    aroma_listview_add_item_with_icon(state.listviews[5], "Voice Assistant", "Enable/Disable Assistant", AROMA_ICON_VOLUME_UP, toggle_voice_assistant_cb);
    state.listview_containers[5] = aroma_listview_get_scroll_container(state.listviews[5]);

    state.listviews[6] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h, state.settings_font);

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

    aroma_listview_add_item_with_icon(state.listviews[6], "Processor", processor_name, AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "RAM", ram_str, AROMA_ICON_STORAGE, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Vehicle name", "Aroma Automotive", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Software", "AromaHMI v0.0.1 Built with AromaSDK", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Build date", __DATE__ " " __TIME__, AROMA_ICON_BUILD, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Security patch", "March 1, 2026", AROMA_ICON_SECURITY, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Uptime", uptime_str, AROMA_ICON_ACCESS_TIME, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Load average", load_str, AROMA_ICON_COMPUTER, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Current time", time_str, AROMA_ICON_ACCESS_TIME, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Platform Backend", "GLPS (X11)", AROMA_ICON_VERIFIED_USER, NULL);
    aroma_listview_add_item_with_icon(state.listviews[6], "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);

    state.listview_containers[6] = aroma_listview_get_scroll_container(state.listviews[6]);

    for (int i = 0; i < num_sections; i++) {
        aroma_sidebar_set_content(state.sidebar, i, &state.listview_containers[i], 1);
    }

    aroma_sidebar_set_selected(state.sidebar, 0);

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
    AromaNode *applet2 = aroma_ui_image((AromaNode *)state.applets_row, "../assets/test.png", 0, 0, 280, 70);
    aroma_node_set_gap((AromaNode *)state.applets_row, 20);
    aroma_node_set_flex_grow(state.system_status_card, 1);
    aroma_node_set_flex_grow(applet2, 1);

    state.vehicle_image = aroma_ui_image(
        (AromaNode *)state.general_info_card,
        "../assets/car.png",
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
    state.ac_temp_label = aroma_ui_label((AromaNode *)state.ac_applet, "23°C", 225, 60, LABEL_STYLE_LABEL_LARGE, state.speed_font);

    state.ac_temp_down_btn = aroma_ui_iconbutton((AromaNode *)state.ac_applet, AROMA_ICON_REMOVE, 145, 100, 48, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    state.ac_temp_up_btn = aroma_ui_iconbutton((AromaNode *)state.ac_applet, AROMA_ICON_ADD, 400, 100, 48, ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);

    state.ac_control_card = aroma_ui_card((AromaNode *)state.ac_applet, 130, 200, 330, 80, CARD_TYPE_FILLED);

    state.ac_image1 = aroma_ui_image(
        (AromaNode *)state.ac_applet,
        "../assets/air-conditioner.png",
        170, 215,
        48, 48);

    state.ac_image2 = aroma_ui_image(
        (AromaNode *)state.ac_applet,
        "../assets/under.png",
        270, 215,
        48, 48);

    state.ac_image3 = aroma_ui_image(
        (AromaNode *)state.ac_applet,
        "../assets/both.png",
        370, 215,
        48, 48);

    state.system_status_title = aroma_ui_label((AromaNode *)state.system_status_card, "System Status", 20, 20, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);

    int icon_x_positions[] = {20, 70, 115, 120, 170, 220, 20, 70};
    int icon_y_positions[] = {60, 60, 55, 60, 60, 60, 120, 120};
    const char *icon_paths[] = {
        "../assets/brake_indicator.png",
        "../assets/abs_indicator.png",
        NULL,
        "../assets/high_beams.png",
        "../assets/low_beams.png",
        "../assets/seatbelt.png",
        "../assets/battery_indicator.png",
        "../assets/temperature.png"
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
