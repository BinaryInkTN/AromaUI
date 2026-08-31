#include "media_controls.h"
#include "vehicle_view.h"
#include "aroma_animation.h"
#include "bluetooth_phone.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>

void apply_deferred_bottom_bar_position(void);
void restore_app_drawer_from_behind(void);
bool open_music(AromaNode *node, void *user_data);
void set_app_open(bool open);
extern bool app_drawer_visible;
void send_app_drawer_behind(void);
extern AppDefinition app_definitions[];
bool is_any_app_open(void);

AromaNode *music_app_tabs = NULL;

AromaNode *music_now_playing_card = NULL;
AromaNode *music_art_placeholder = NULL;
AromaNode *music_track_title_label = NULL;
AromaNode *music_track_artist_label = NULL;
AromaNode *music_track_album_label = NULL;
AromaNode *music_status_label = NULL;
AromaNode *music_prev_button = NULL;
AromaNode *music_play_pause_button = NULL;
AromaNode *music_next_button = NULL;
AromaNode *music_no_media_label = NULL;
AromaNode *music_open_btn = NULL;

AromaNode *music_device_card = NULL;
AromaNode *music_device_status_icon = NULL;
AromaNode *music_device_status_label = NULL;
AromaNode *music_device_name_label = NULL;
AromaNode *music_device_address_label = NULL;
AromaNode *music_device_stats_label = NULL;
AromaNode *music_device_no_phone_label = NULL;

MediaPlayerUI media_ui = {
    .is_playing = false,
    .bottom_bar_expanded = false,
    .ui_initialized = false,
    .first_media_check_done = false};

bool music_app_open = false;
int music_active_tab = 0;

static void update_play_pause_button_icon(void)
{
    if (!media_ui.media_play_pause_button)
        return;
    if (media_ui.is_playing)
    {
        aroma_iconbutton_set_icon(media_ui.media_play_pause_button, AROMA_ICON_PAUSE);
    }
    else
    {
        aroma_iconbutton_set_icon(media_ui.media_play_pause_button, AROMA_ICON_PLAY_ARROW);
    }
}

static void on_media_prev_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_previous();
}

static void on_media_play_pause_click(void *user_data)
{
    (void)user_data;
    if (media_ui.is_playing)
    {
        bt_speaker_avrcp_pause();
        media_ui.is_playing = false;
    }
    else
    {
        bt_speaker_avrcp_play();
        media_ui.is_playing = true;
    }
    update_play_pause_button_icon();
}

static void on_media_next_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_next();
}

void update_media_card_display(void)
{
    if (!media_ui.ui_initialized || !media_ui.media_card)
        return;

    pthread_mutex_lock(&g_bt_mutex);
    bt_media_info_t media = g_bt_media_info;
    bt_state_t current_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);

    bool is_playing = (current_state == BT_STATE_PLAYING);
    bool is_connected = (current_state == BT_STATE_CONNECTED || is_playing);
    bool has_media = (media.title[0] != '\0' || media.artist[0] != '\0');

    if (!is_connected || !has_media)
    {
        aroma_node_set_hidden(media_ui.media_card, true);
        media_ui.first_media_check_done = false;
        return;
    }

    if (!media_ui.first_media_check_done)
    {
        media_ui.first_media_check_done = true;
    }
    if (!is_any_app_open())
    {
        aroma_node_set_hidden(media_ui.media_card, false);
    }
    else
    {
        aroma_node_set_hidden(media_ui.media_card, true);
    }
    if (strcmp(media.status, "playing") == 0)
    {
        if (!media_ui.is_playing)
        {
            media_ui.is_playing = true;
            update_play_pause_button_icon();
        }
    }
    else if (strcmp(media.status, "paused") == 0)
    {
        if (media_ui.is_playing)
        {
            media_ui.is_playing = false;
            update_play_pause_button_icon();
        }
    }
    if (media_ui.media_title_label && media.title[0])
        aroma_label_set_text(media_ui.media_title_label, media.title);
    if (media_ui.media_artist_label)
    {
        if (media.artist[0])
            aroma_label_set_text(media_ui.media_artist_label, media.artist);
        else
            aroma_label_set_text(media_ui.media_artist_label, "Unknown Artist");
    }
}

void *media_monitor_thread_func(void *arg)
{
    (void)arg;
    usleep(3000000);
    while (media_ui.ui_initialized)
    {
        update_media_card_display();
        update_bt_info_card();
        if (music_app_open)
        {
            if (music_active_tab == 0)
                update_music_now_playing_display();
            else
                update_music_device_display();
        }
        usleep(MEDIA_UPDATE_INTERVAL_US);
    }
    return NULL;
}

static void update_music_play_pause_icon(void)
{
    if (!music_play_pause_button)
        return;
    aroma_iconbutton_set_icon(music_play_pause_button,
                              media_ui.is_playing ? AROMA_ICON_PAUSE : AROMA_ICON_PLAY_ARROW);
}

void on_music_prev_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_previous();
}

void on_music_play_pause_click(void *user_data)
{
    (void)user_data;
    if (media_ui.is_playing)
    {
        bt_speaker_avrcp_pause();
        media_ui.is_playing = false;
    }
    else
    {
        bt_speaker_avrcp_play();
        media_ui.is_playing = true;
    }
    update_play_pause_button_icon();
    update_music_play_pause_icon();
}

void on_music_next_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_next();
}

void on_music_icon_click(void *user_data)
{
    (void)user_data;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }
    open_music(NULL, app_definitions[2].app_root);
}

static void on_music_tab_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    music_active_tab = tab_index;
    if (music_now_playing_card)
        aroma_node_set_hidden(music_now_playing_card, tab_index != 0);
    if (music_device_card)
        aroma_node_set_hidden(music_device_card, tab_index != 1);
    if (tab_index == 0)
        update_music_now_playing_display();
    else
        update_music_device_display();
}

void update_music_now_playing_display(void)
{
    if (!music_now_playing_card)
        return;

    pthread_mutex_lock(&g_bt_mutex);
    bt_media_info_t media = g_bt_media_info;
    bt_state_t current_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);

    bool is_playing = (current_state == BT_STATE_PLAYING);
    bool is_connected = (current_state == BT_STATE_CONNECTED || is_playing);
    bool has_media = (media.title[0] != '\0' || media.artist[0] != '\0');

    if (!is_connected || !has_media)
    {
        if (music_no_media_label)
            aroma_node_set_hidden(music_no_media_label, false);
        if (music_art_placeholder)
            aroma_node_set_hidden(music_art_placeholder, true);
        if (music_track_title_label)
            aroma_node_set_hidden(music_track_title_label, true);
        if (music_track_artist_label)
            aroma_node_set_hidden(music_track_artist_label, true);
        if (music_track_album_label)
            aroma_node_set_hidden(music_track_album_label, true);
        if (music_status_label)
            aroma_node_set_hidden(music_status_label, true);
        if (music_prev_button)
            aroma_node_set_hidden(music_prev_button, true);
        if (music_play_pause_button)
            aroma_node_set_hidden(music_play_pause_button, true);
        if (music_next_button)
            aroma_node_set_hidden(music_next_button, true);
        return;
    }

    if (music_no_media_label)
        aroma_node_set_hidden(music_no_media_label, true);
    if (music_art_placeholder)
        aroma_node_set_hidden(music_art_placeholder, false);
    if (music_prev_button)
        aroma_node_set_hidden(music_prev_button, false);
    if (music_play_pause_button)
        aroma_node_set_hidden(music_play_pause_button, false);
    if (music_next_button)
        aroma_node_set_hidden(music_next_button, false);

    if (music_track_title_label)
    {
        aroma_node_set_hidden(music_track_title_label, false);
        aroma_label_set_text(music_track_title_label, media.title[0] ? media.title : "Unknown Track");
    }
    if (music_track_artist_label)
    {
        aroma_node_set_hidden(music_track_artist_label, false);
        aroma_label_set_text(music_track_artist_label, media.artist[0] ? media.artist : "Unknown Artist");
    }
    if (music_track_album_label)
    {
        if (media.album[0])
        {
            aroma_node_set_hidden(music_track_album_label, false);
            aroma_label_set_text(music_track_album_label, media.album);
        }
        else
        {
            aroma_node_set_hidden(music_track_album_label, true);
        }
    }
    if (music_status_label)
    {
        aroma_node_set_hidden(music_status_label, false);
        if (strcmp(media.status, "playing") == 0)
        {
            aroma_label_set_text(music_status_label, "Playing");
            aroma_label_set_color(music_status_label, 0xFF4CAF50);
        }
        else if (strcmp(media.status, "paused") == 0)
        {
            aroma_label_set_text(music_status_label, "Paused");
            aroma_label_set_color(music_status_label, 0xFFFF9800);
        }
        else
        {
            aroma_label_set_text(music_status_label, "Connected");
            aroma_label_set_color(music_status_label, 0xFF9E9E9E);
        }
    }

    update_music_play_pause_icon();
}

void update_music_device_display(void)
{
    if (!music_device_card)
        return;

    pthread_mutex_lock(&g_bt_mutex);
    bt_device_info_t device = g_bt_device_info;
    bt_state_t current_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);

    bool is_connected = (current_state == BT_STATE_CONNECTED ||
                         current_state == BT_STATE_PLAYING);

    if (!is_connected || !device.connected || !device.name[0])
    {
        if (music_device_no_phone_label)
            aroma_node_set_hidden(music_device_no_phone_label, false);
        if (music_device_name_label)
            aroma_node_set_hidden(music_device_name_label, true);
        if (music_device_address_label)
            aroma_node_set_hidden(music_device_address_label, true);
        if (music_device_stats_label)
            aroma_node_set_hidden(music_device_stats_label, true);
        if (music_device_status_label)
            aroma_label_set_text(music_device_status_label, "No Phone Connected");
        if (music_device_status_icon)
            aroma_icon_set_color(music_device_status_icon, 0xFF9E9E9E);
        return;
    }

    if (music_device_no_phone_label)
        aroma_node_set_hidden(music_device_no_phone_label, true);

    if (music_device_status_label)
        aroma_label_set_text(music_device_status_label,
                             current_state == BT_STATE_PLAYING ? "Connected - Playing" : "Connected");
    if (music_device_status_icon)
        aroma_icon_set_color(music_device_status_icon, 0xFF4CAF50);

    if (music_device_name_label)
    {
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "Name: %s", device.name);
        aroma_node_set_hidden(music_device_name_label, false);
        aroma_label_set_text(music_device_name_label, name_buf);
    }
    if (music_device_address_label)
    {
        char address_buf[128];
        snprintf(address_buf, sizeof(address_buf), "Address: %s", device.address[0] ? device.address : "Unknown");
        aroma_node_set_hidden(music_device_address_label, false);
        aroma_label_set_text(music_device_address_label, address_buf);
    }
    if (music_device_stats_label)
    {
        pthread_mutex_lock(&g_bt_mutex);
        bt_stats_t stats = g_bt_stats;
        pthread_mutex_unlock(&g_bt_mutex);
        char stats_buf[160];
        unsigned long minutes = stats.connected_time_sec / 60;
        unsigned long seconds = stats.connected_time_sec % 60;
        if (stats.audio_active && stats.audio_time_sec > 0)
        {
            unsigned long audio_min = stats.audio_time_sec / 60;
            unsigned long audio_sec = stats.audio_time_sec % 60;
            snprintf(stats_buf, sizeof(stats_buf),
                     "Connected: %lumin %lus | Audio: %lumin %lus",
                     minutes, seconds, audio_min, audio_sec);
        }
        else
        {
            snprintf(stats_buf, sizeof(stats_buf), "Connected: %lumin %lus", minutes, seconds);
        }
        aroma_node_set_hidden(music_device_stats_label, false);
        aroma_label_set_text(music_device_stats_label, stats_buf);
    }
}

void music_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *tabs_rect = aroma_node_get_rect(music_app_tabs);
    if (!tabs_rect)
        return;

    int start_y = WIN_H;
    int end_y = 0;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

    tabs_rect->x = rect->x;
    tabs_rect->y = rect->y;
    tabs_rect->width = rect->width;
    tabs_rect->height = 100;
    aroma_node_invalidate(target);
}

bool open_music(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, music_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_node_set_hidden(music_app_tabs, false);
    aroma_node_set_hidden(music_now_playing_card, false);
    aroma_node_set_hidden(music_device_card, true);
    if (music_app_tabs)
        aroma_tabs_set_selected(music_app_tabs, 0);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    music_app_open = true;
    music_active_tab = 0;
    update_music_now_playing_display();

    return true;
}

void music_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *tabs_rect = aroma_node_get_rect(music_app_tabs);
    if (!tabs_rect)
        return;

    int start_y = 0;
    int end_y = WIN_H;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

    tabs_rect->x = rect->x;
    tabs_rect->y = rect->y + 90;
    tabs_rect->width = rect->width;
    tabs_rect->height = 100;
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(music_app_tabs, true);
        aroma_node_set_hidden(target, true);
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        music_app_open = false;
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    aroma_node_invalidate(target);
}

void close_music(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    set_app_open(false);
    music_app_open = false;
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, music_closing_anim, NULL);
    aroma_node_set_hidden(music_now_playing_card, true);
    aroma_node_set_hidden(music_device_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
}
