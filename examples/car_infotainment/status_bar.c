#include "status_bar.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "voice_handler.h"
#include "settings_ui.h"

void build_status_bar(void)
{
    state.time_label = aroma_ui_label(
        (AromaNode *)state.window, "12:45 PM", 50, 30,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.time_label, Z_LAYER_STATUS_BAR);
    
    state.location_label = aroma_ui_label(
        (AromaNode *)state.window, "San Francisco, 68°F", 150, 30,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.location_label, Z_LAYER_STATUS_BAR);

    aroma_node_set_hidden(state.location_label, true);
    aroma_node_set_hidden(state.time_label, true);

    state.status_card = aroma_ui_card(
        (AromaNode *)state.window, WIN_W - 200, 10, 175, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.status_card, Z_LAYER_STATUS_BAR);

    state.signal_icon = aroma_ui_icon(
        (AromaNode *)state.window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, 
        WIN_W - 80, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.wifi_icon = aroma_ui_icon(
        (AromaNode *)state.window, AROMA_ICON_WIFI, 
        WIN_W - 40, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.battery_icon = aroma_ui_icon(
        (AromaNode *)state.window, AROMA_ICON_BATTERY_FULL, 
        WIN_W - 160, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.gps_icon = aroma_ui_icon(
        (AromaNode *)state.window, AROMA_ICON_GPS_FIXED, 
        WIN_W - 120, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.bluetooth_icon = aroma_ui_icon(
        (AromaNode *)state.window, AROMA_ICON_BLUETOOTH_AUDIO, 
        WIN_W - 200, 30, 24, state.theme.colors.text_primary, state.icon_font);
    aroma_node_set_hidden(state.bluetooth_icon, true);
    AromaNode *status_icons[] = {
        state.signal_icon, state.wifi_icon, state.battery_icon,
        state.gps_icon, state.bluetooth_icon
    };
    for (int i = 0; i < 5; i++)
        aroma_node_set_z_index(status_icons[i], Z_LAYER_STATUS_ICONS);

    state.voice_button = aroma_ui_iconbutton(
        (AromaNode *)state.window, AROMA_ICON_MIC,
        WIN_W - 250, 22, 40, ICON_BUTTON_FILLED,
        voice_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.voice_button, Z_LAYER_STATUS_ICONS);

    state.settings_icon = aroma_ui_iconbutton(
        (AromaNode *)state.window, AROMA_ICON_SETTINGS,
        WIN_W - 305, 22, 40, ICON_BUTTON_OUTLINED,
        settings_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.settings_icon, Z_LAYER_STATUS_ICONS);

    AromaNode *status_nodes[] = {
        state.time_label, state.status_card, state.location_label,
        state.signal_icon, state.wifi_icon, state.battery_icon,
        state.gps_icon, state.bluetooth_icon, state.voice_button
    };
    int status_y[] = { 30, 18, 30, 30, 30, 30, 30, 30, 22 };
    for (int i = 0; i < 9; i++)
        aroma_animation_start(status_nodes[i], AROMA_ANIM_SLIDE_Y, -40, status_y[i], 800);
}