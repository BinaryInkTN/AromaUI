#ifndef APP_STATE_H
#define APP_STATE_H

#include "aroma.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "bt_speaker_hfp.h"
#define WIN_W 1024
#define WIN_H 600

#define Z_LAYER_BACKGROUND 1
#define Z_LAYER_VEHICLE_IMAGE 2
#define Z_LAYER_VEHICLE_OVERLAYS 3
#define Z_LAYER_CARDS_BOTTOM 10
#define Z_LAYER_MAP_PANEL 20
#define Z_LAYER_MAP_BUTTON 15
#define Z_LAYER_MAP_CONTROLS 21
#define Z_LAYER_MAP_CLOSE 22
#define Z_LAYER_STATUS_BAR 100
#define Z_LAYER_STATUS_ICONS 101
#define Z_LAYER_SETTINGS_PANEL 150
#define Z_LAYER_VOICE_CARD 999998
#define Z_LAYER_VOICE_CONTENT 999999

#define MAP_PANEL_WIDTH WIN_W
#define MAP_PANEL_OFFSET 0
#define SETTINGS_PANEL_W 800
#define SETTINGS_ANIM_MS 200

#define MAX_STRING_LEN 256
#define MAX_PATH_LEN 512
#define MAX_VOICE_TEXT 512
#define MAX_FAULT_MSG 128
#define MAX_CONTACTS 100
typedef struct {
    char             path[256];  
    char             number[64];
    char             name[128];  
    bt_call_state_t  state;
    bool             multiparty;
} CallInfo;

typedef struct
{
    char name[128];
    char number[64];
} ContactInfo;

typedef struct
{

    uint8_t wiper_speed;
    uint8_t headlight_state;
    uint8_t indicator_left;
    uint8_t indicator_right;
    uint8_t buzzer;
    uint8_t door_locked;
    uint8_t interior_light;
    uint8_t rain_detected;
    uint8_t door_open;

    uint16_t throttle_cmd;
    uint16_t brake_cmd;
    uint16_t fsr_value;
    uint16_t vehicle_speed;
    uint8_t crash_detected;
    uint8_t airbag_deployed;
    uint8_t seatbelt_warn;
    uint8_t acm_seat_occupied;
    uint8_t acm_system_status;

    int16_t seat_position_cmd;
    uint8_t seat_profile;
    uint8_t seat_occupied;

    uint16_t speed_raw;
    uint16_t speed_filtered;
    int16_t acceleration;
    uint16_t avg_speed;
    uint16_t max_speed;
    uint32_t distance;
    uint32_t kinetic_energy;
    uint8_t high_speed_flag;
    uint8_t harsh_braking;
    uint8_t vss_fault;

    int16_t temp_c;
    uint16_t humidity;
    int16_t dew_point_c;
    int16_t altitude_m;
    uint32_t pressure_pa;
    uint8_t ecs_sensor_fault;
    uint8_t comfort_cold;
    uint8_t comfort_hot;
    uint8_t high_humidity;

} EVState;

typedef struct
{

    AromaFont *icon_font;
    AromaFont *huge_icon_font;
    AromaFont *ui_font;
    AromaFont *tab_font;
    AromaFont *settings_font;
    AromaFont *clock_font;
    AromaFont *clock_pm_am_font;
    AromaFont *ac_font;

    AromaWindow *window;

    AromaNode *settings_root;
    AromaNode *vehicle_view_root;
    AromaNode *tabs;
    AromaNode *sidebar;
    AromaNode *car_img;
    AromaNode *time_label;
    AromaNode *location_label;
    AromaNode *status_card;
    AromaNode *signal_icon;
    AromaNode *wifi_icon;
    AromaNode *battery_icon;
    AromaNode *gps_icon;
    AromaNode *bluetooth_icon;

    AromaNode *vehicle_view_lock_icon;
    AromaNode *vehicle_view_lock_divider;
    AromaNode *vehicle_view_charge_port_divider;
    AromaNode *vehicle_view_charge_port_icon;
    AromaNode *vehicle_view_frunk_header;
    AromaNode *vehicle_view_frunk_desc;
    AromaNode *vehicle_view_frunk_divider;
    AromaNode *vehicle_view_trunk_divider;
    AromaNode *vehicle_view_trunk_header;
    AromaNode *vehicle_view_trunk_desc;
    AromaNode *vehicle_view_warning_message_card;
    AromaNode *vehicle_view_warning_message_label;
    AromaNode *vehicle_view_warning_warning_icon;
    AromaNode *vehicle_view_warning_message_action;
    AromaNode *vehicle_view_large_clock;
    AromaNode *vehicle_view_large_clock_pm_am;
    AromaNode *vehicle_view_hints;
    AromaNode *vehicle_view_side_arrow_icon_button;
    AromaNode *overlay;
    AromaNode *recent_lv;

    AromaNode *speed_label;
    AromaNode *speed_gauge;
    AromaNode *range_label;
    AromaNode *climate_label;
    AromaNode *gear_bg_card;
    AromaNode *gear_fg_card;

    AromaNode *ac_card;
    AromaNode *music_card;
    AromaNode *nav_card;

    AromaNode *vehicle_view_battery_divider;
    AromaNode *vehicle_view_battery_percentage;
    AromaNode *battery_image;
    AromaNode *battery_health;
    AromaNode *battery_percentage;

    AromaNode *map_node;
    AromaNode *map_panel;
    AromaNode *map_overlay_background;
    AromaNode *map_close_btn;
    AromaNode *maps_app_icon;
    AromaNode *map_recently_viewed_card;

    AromaNode *phone_app_icon;
    AromaNode *phone_node;
    AromaNode *phone_close_btn;
    AromaNode *phone_content_card;
    AromaNode *call_history_listview;

    AromaNode *music_app_icon;
    AromaNode *music_node;
    AromaNode *music_close_btn;
    AromaNode *music_content_card;
    AromaNode *contact_listview;
    AromaNode *settings_panel_node;
    AromaNode *listviews[8];
    AromaNode *listview_containers[8];

    AromaNode *ac_temp_label;

    AromaNode *voice_button;
    AromaNode *settings_icon;
    AromaNode *voice_status_label;
    AromaNode *voice_status_card;
    AromaNode *loading_spinner;

    AromaNode *easter_egg_overlay;
    AromaNode *easter_egg_icon;
    AromaNode *setup_overlay;
    AromaNode *battery_button;

    AromaNode *bt_container;
    AromaNode *range_card;
    AromaNode *secondary_notification_card;

    AromaNode *phone_app_header_bar;
    AromaNode *bottom_bar;
    AromaNode *phone_app_tabs;
    AromaNode* backroad;
    AromaTheme theme;

    AromaFont *big_icon_font;

    bool map_panel_open;
    bool settings_panel_open;
    bool dark_theme_enabled;
    bool voice_is_visible;
    bool voice_nav_trigger;
    bool g_voice_assistant_enabled;
    bool initialized;

    EVState vehicle_state;

    pthread_mutex_t can_mtx;
    pthread_mutex_t pending_mtx;
    pthread_mutex_t voice_mtx;
    volatile int pending_map_open;

    int voice_target_tab;
    int voice_partial_timeout;
    int voice_theme_change;
    int voice_ac_change;
    int voice_info_request;
    int current_ac_temp;

    char voice_status_text[MAX_VOICE_TEXT];
    char voice_partial_text[MAX_VOICE_TEXT];
    char voice_nav_dest[MAX_STRING_LEN];
    ContactInfo contacts[MAX_CONTACTS];
    int contact_count;
    bool contacts_fetched;
    // Add to AppState struct:
    CallInfo call_history[100];
    int call_history_count;
    bool call_history_fetched;
} AppState;



extern AppState state;

bool init_app_state(void);
void cleanup_app_state(void);
void safe_str_copy(char *dest, const char *src, size_t dest_size);
bool safe_node_check(const AromaNode *node, const char *node_name);

#endif