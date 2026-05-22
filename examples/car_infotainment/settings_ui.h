#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "aroma.h"
#include "can_handler.h"

void build_settings_ui(AromaNode *window);
void open_settings_panel(void *user_data);
void close_settings_panel(void *user_data);
void listview_callback(int index, void *user_data);
void settings_button_callback(void *user_data);

#endif