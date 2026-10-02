#ifndef INTERIOR_CONTROLS_H
#define INTERIOR_CONTROLS_H

#include "aroma.h"

void toggle_seat_controls_callback(void *user_data);
void toggle_ac_controls_callback(void *user_data);
bool ac_power_callback(AromaNode *node, void *user_data);
void fan_up_callback(void *user_data);
void fan_down_callback(void *user_data);
bool ac_mode_callback(AromaNode *node, void *user_data);
void ac_temp_up_callback(void *user_data);
void ac_temp_down_callback(void *user_data);

#endif
