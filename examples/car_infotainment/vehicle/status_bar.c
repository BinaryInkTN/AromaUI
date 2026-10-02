#include "status_bar.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "voice_handler.h"

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

    state.status_card = NULL;
    state.signal_icon = NULL;
    state.wifi_icon = NULL;
    state.battery_icon = NULL;
    state.gps_icon = NULL;
    state.bluetooth_icon = NULL;

    AromaNode *status_nodes[] = {
        state.time_label, state.location_label, state.voice_button
    };
    int status_y[] = { 30, 30, 22 };
    for (int i = 0; i < 3; i++)
        aroma_animation_start(status_nodes[i], AROMA_ANIM_SLIDE_Y, -40, status_y[i], 800);
}
