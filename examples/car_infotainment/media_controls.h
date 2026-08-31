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
void update_music_now_playing_display(void);
void update_music_device_display(void);
void on_music_icon_click(void *user_data);
void on_music_prev_click(void *user_data);
void on_music_play_pause_click(void *user_data);
void on_music_next_click(void *user_data);
bool open_music(AromaNode *node, void *user_data);
void close_music(void *user_data);

void apply_deferred_bottom_bar_position(void);
void restore_app_drawer_from_behind(void);

extern bool music_app_open;
extern int music_active_tab;
extern MediaPlayerUI media_ui;

extern AromaNode *music_app_tabs;
extern AromaNode *music_now_playing_card;
extern AromaNode *music_art_placeholder;
extern AromaNode *music_track_title_label;
extern AromaNode *music_track_artist_label;
extern AromaNode *music_track_album_label;
extern AromaNode *music_status_label;
extern AromaNode *music_prev_button;
extern AromaNode *music_play_pause_button;
extern AromaNode *music_next_button;
extern AromaNode *music_no_media_label;
extern AromaNode *music_open_btn;
extern AromaNode *music_device_card;
extern AromaNode *music_device_status_icon;
extern AromaNode *music_device_status_label;
extern AromaNode *music_device_name_label;
extern AromaNode *music_device_address_label;
extern AromaNode *music_device_stats_label;
extern AromaNode *music_device_no_phone_label;

#endif
