#include "vehicle_view.h"

static void toggle_seat_controls_callback(void *user_data)
{
    (void)user_data;
    if (!state.seat_controls_card)
        return;

    bool hidden = aroma_node_is_hidden(state.seat_controls_card);
    if (hidden)
    {
        aroma_node_set_hidden(state.seat_controls_card, false);
    }
    else
    {
        aroma_node_set_hidden(state.seat_controls_card, true);
    }
}

static void toggle_ac_controls_callback(void *user_data)
{
    (void)user_data;
    if (!state.ac_controls_card)
        return;

    bool hidden = aroma_node_is_hidden(state.ac_controls_card);
    if (hidden)
    {
        aroma_node_set_hidden(state.ac_controls_card, false);
    }
    else
    {
        aroma_node_set_hidden(state.ac_controls_card, true);
    }
}

static bool ac_power_callback(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    state.ac_auto_mode = !state.ac_auto_mode;
    return true;
}

static void fan_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_fan_speed < 5)
        state.current_fan_speed++;
}

static void fan_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_fan_speed > 0)
        state.current_fan_speed--;
}

static bool ac_mode_callback(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    state.ac_auto_mode = !state.ac_auto_mode;
    return true;
}

void ac_temp_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp < 30)
        state.current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
    if (state.ac_controls_temp_label)
        aroma_label_set_text(state.ac_controls_temp_label, buf);
}

void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16)
        state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
    if (state.ac_controls_temp_label)
        aroma_label_set_text(state.ac_controls_temp_label, buf);
}
