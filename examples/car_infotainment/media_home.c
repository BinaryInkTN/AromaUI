/* Host-side mini media player (home-screen card).
 *
 * The full-screen Music app now ships as the com.aroma.media .apak package;
 * this file keeps what must stay in the host: the mini player card state,
 * its display refresh, its monitor thread, and the card icon that opens the
 * package. Full-app widgets live in the plugin.
 */

#include "apps/media/media_controls.h"
#include "vehicle_view.h"
#include "app_state.h"
#include "apps/phone/bt_speaker_api.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern pthread_mutex_t g_bt_mutex;
extern bt_media_info_t g_bt_media_info;
extern bt_state_t g_bt_state;

MediaPlayerUI media_ui = {
    .is_playing = false,
    .bottom_bar_expanded = false,
    .ui_initialized = false,
    .first_media_check_done = false};

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

void *media_home_monitor_thread_func(void *arg)
{
    (void)arg;
    usleep(3000000);
    while (media_ui.ui_initialized)
    {
        update_media_card_display();
        update_bt_info_card();
        usleep(MEDIA_UPDATE_INTERVAL_US);
    }
    return NULL;
}

void on_music_icon_click(void *user_data)
{
    (void)user_data;
    vehicle_view_open_package("com.aroma.media");
}
