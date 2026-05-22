#ifndef APP_STATE_H
#define APP_STATE_H

#include "aroma.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define WIN_W 1280
#define WIN_H 800

#define Z_LAYER_BACKGROUND       1
#define Z_LAYER_VEHICLE_IMAGE    2
#define Z_LAYER_VEHICLE_OVERLAYS 3
#define Z_LAYER_CARDS_BOTTOM     10
#define Z_LAYER_MAP_PANEL        20
#define Z_LAYER_MAP_BUTTON       15
#define Z_LAYER_MAP_CONTROLS     21
#define Z_LAYER_MAP_CLOSE        22
#define Z_LAYER_STATUS_BAR       100
#define Z_LAYER_STATUS_ICONS     101
#define Z_LAYER_SETTINGS_PANEL   150
#define Z_LAYER_VOICE_CARD       999998
#define Z_LAYER_VOICE_CONTENT    999999

#define MAP_PANEL_WIDTH  WIN_W
#define MAP_PANEL_OFFSET 0
#define SETTINGS_PANEL_W 800
#define SETTINGS_ANIM_MS 350

typedef struct {
    double   speed;
    int      rpm;
    int      gear;
    double   soc;
    double   voltage;
    double   current;
    double   cabin_temp;
    double   target_temp;
    int      hvac_on;
    int      fan_speed;
    int      seat_heaters;
    int      doors;
    uint32_t fault_code;
    double   range;
} EVState;

typedef struct {
    AromaFont *icon_font;
    AromaFont *ui_font;
    AromaFont *tab_font;
    AromaFont *settings_font;
    AromaFont *clock_font;
    AromaFont *clock_pm_am_font;

    AromaWindow *window;
    AromaNode   *settings_root;
    AromaNode   *vehicle_view_root;
    AromaNode   *tabs;
    AromaNode   *sidebar;

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
    bool       map_panel_open;

    AromaNode *settings_panel_node;
    bool       settings_panel_open;

    AromaNode *ac_temp_label;

    AromaNode *voice_button;
    AromaNode *settings_icon;
    AromaNode *voice_status_label;
    AromaNode *voice_status_card;
    AromaNode *loading_spinner;

    AromaNode *listviews[8];
    AromaNode *listview_containers[8];

    AromaNode *easter_egg_overlay;
    AromaNode *easter_egg_icon;

    AromaNode *setup_overlay;
    AromaNode *battery_button;

    AromaTheme theme;
    bool       dark_theme_enabled;
    
    EVState vehicle_state;
    pthread_mutex_t can_mtx;
    volatile int pending_map_open;
    pthread_mutex_t pending_mtx;
    
    bool voice_is_visible;
    pthread_mutex_t voice_mutex;
    int voice_target_tab;
    char voice_status_text[256];
    char voice_partial_text[512];
    char voice_nav_dest[128];
    bool voice_nav_trigger;
    int voice_partial_timeout;
    int voice_theme_change;
    int voice_ac_change;
    int voice_info_request;
    int current_ac_temp;
    bool g_voice_assistant_enabled;
} AppState;

extern AppState state;

void init_app_state(void);
void cleanup_app_state(void);

#endif