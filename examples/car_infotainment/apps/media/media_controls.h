#ifndef MEDIA_CONTROLS_H
#define MEDIA_CONTROLS_H

#include "aroma.h"

typedef struct
{
    AromaNode *media_card;
    AromaNode *media_title_label;
    AromaNode *media_artist_label;
    AromaNode *media_prev_button;
    AromaNode *media_play_pause_button;
    AromaNode *media_next_button;
    bool is_playing;
    bool bottom_bar_expanded;
    bool ui_initialized;
    bool first_media_check_done;
} MediaPlayerUI;

void update_media_card_display(void);
void on_music_icon_click(void *user_data);
void *media_home_monitor_thread_func(void *arg);

void apply_deferred_bottom_bar_position(void);
void restore_app_drawer_from_behind(void);

extern MediaPlayerUI media_ui;

#endif
