#include "vehicle_view.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "bt_speaker_api.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define MEDIA_UPDATE_INTERVAL_US 500000

typedef struct
{
    AromaNode *media_card;
    AromaNode *media_title_label;
    AromaNode *media_artist_label;
    AromaNode *media_progress_bar;
    AromaNode *media_time_elapsed_label;
    AromaNode *media_time_remaining_label;
    AromaNode *media_prev_button;
    AromaNode *media_play_pause_button;
    AromaNode *media_next_button;
    bool is_playing;
    bool monitor_running;
    pthread_t monitor_thread;
    pthread_mutex_t lock;
    uint32_t last_position;
    uint32_t last_duration;
    time_t last_position_update;
    char current_title[256];
    bool marquee_active;
    int marquee_offset;
    time_t last_marquee_update;
} MediaPlayerUI;
typedef struct
{
    const char *name;
    double lat;
    double lon;
} MapDestination;

static const MapDestination recently_viewed_destinations[] = {
    {"Home", 37.7749, -122.4194},
    {"Work", 37.3382, -121.8863},
    {"Supercharger", 37.8044, -122.2711},
    {"Grocery Store", 37.7600, -122.4477},
    {"Coffee Shop", 37.7749, -122.449},
};
#define RECENTLY_VIEWED_COUNT (sizeof(recently_viewed_destinations) / sizeof(recently_viewed_destinations[0]))
static MediaPlayerUI media_ui = {
    .is_playing = false,
    .monitor_running = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .last_position = 0,
    .last_duration = 0,
    .last_position_update = 0,
    .current_title = {0},
    .marquee_active = false,
    .marquee_offset = 0,
    .last_marquee_update = 0};

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
static void on_destination_click(void *user_data)
{
    const MapDestination *dest = (const MapDestination *)user_data;
    if (!dest)
        return;
    aroma_map_pan_to(state.map_node, dest->lat, dest->lon);
}

static void update_marquee_title(void)
{
    if (!media_ui.media_title_label || !media_ui.current_title[0])
        return;

    int title_len = strlen(media_ui.current_title);

    if (title_len <= 22)
    {
        aroma_label_set_text(media_ui.media_title_label, media_ui.current_title);
        media_ui.marquee_active = false;
        return;
    }

    if (!media_ui.marquee_active)
    {
        media_ui.marquee_offset = 0;
        media_ui.marquee_active = true;
        media_ui.last_marquee_update = time(NULL);
    }

    time_t now = time(NULL);
    if (now - media_ui.last_marquee_update >= 1)
    {
        media_ui.marquee_offset++;
        media_ui.last_marquee_update = now;

        if (media_ui.marquee_offset > title_len + 5)
            media_ui.marquee_offset = 0;
    }

    char display_text[64];
    int max_chars = 22;
    int remaining = title_len - media_ui.marquee_offset;

    if (remaining >= max_chars)
    {
        strncpy(display_text, media_ui.current_title + media_ui.marquee_offset, max_chars);
        display_text[max_chars] = '\0';
    }
    else
    {
        int first_part = remaining;
        if (first_part > 0)
        {
            strncpy(display_text, media_ui.current_title + media_ui.marquee_offset, first_part);
            display_text[first_part] = ' ';
            display_text[first_part + 1] = ' ';
            int second_part = max_chars - first_part - 2;
            if (second_part > 0 && second_part <= media_ui.marquee_offset)
            {
                strncpy(display_text + first_part + 2, media_ui.current_title, second_part);
                display_text[first_part + 2 + second_part] = '\0';
            }
            else if (second_part > 0)
            {
                strncpy(display_text + first_part + 2, media_ui.current_title, media_ui.marquee_offset);
                display_text[first_part + 2 + media_ui.marquee_offset] = '\0';
            }
            else
            {
                display_text[first_part + 2] = '\0';
            }
        }
        else
        {
            display_text[0] = ' ';
            display_text[1] = ' ';
            strncpy(display_text + 2, media_ui.current_title, max_chars - 2);
            display_text[max_chars] = '\0';
        }
    }

    aroma_label_set_text(media_ui.media_title_label, display_text);
}

static void on_media_prev_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_previous();
}

static void on_media_play_pause_click(void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&media_ui.lock);
    if (media_ui.is_playing)
    {
        bt_speaker_avrcp_pause();
    }
    else
    {
        bt_speaker_avrcp_play();
    }
    media_ui.is_playing = !media_ui.is_playing;
    update_play_pause_button_icon();
    media_ui.last_position_update = time(NULL);
    pthread_mutex_unlock(&media_ui.lock);
}

static void on_media_next_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_next();
}

static char *format_time_ms(uint32_t ms, char *buf, size_t bufsz)
{
    uint32_t total_sec = ms / 1000;
    uint32_t min = total_sec / 60;
    uint32_t sec = total_sec % 60;
    snprintf(buf, bufsz, "%u:%02u", min, sec);
    return buf;
}

static void update_media_ui(void)
{
    pthread_mutex_lock(&media_ui.lock);

    bt_media_info_t media = bt_speaker_get_media_info();
    bt_state_t current_state = bt_speaker_get_state();

    bool is_connected = (current_state == BT_STATE_CONNECTED ||
                         current_state == BT_STATE_PLAYING);
    bool has_media = (media.title[0] != '\0' || media.artist[0] != '\0');

    if (!has_media || !is_connected)
    {
        if (media_ui.media_card)
            aroma_node_set_hidden(media_ui.media_card, true);
        media_ui.is_playing = false;
        media_ui.last_position = 0;
        media_ui.last_duration = 0;
        media_ui.last_position_update = 0;
        media_ui.current_title[0] = '\0';
        media_ui.marquee_active = false;
        update_play_pause_button_icon();
        pthread_mutex_unlock(&media_ui.lock);
        return;
    }

    static bool first_update = false;
    if (!first_update)
    {
        first_update = true;
        if (state.bottom_bar)
        {
            AromaAnimation *anim = aroma_animation_start(state.bottom_bar, AROMA_ANIM_SLIDE_X, 230, 460, 1200);
            aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
        }

        if (state.range_card)
        {
            AromaAnimation *anim = aroma_animation_start(state.range_card, AROMA_ANIM_SLIDE_X, 680, 910, 1200);
            aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
        }

        if (state.bluetooth_icon)
        {
            aroma_node_set_hidden(state.bluetooth_icon, false);
            AromaAnimation *anim = aroma_animation_start(state.status_card, AROMA_ANIM_SCALE_X, 175, 205, 1200);

            aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
        }
    }

    if (media_ui.media_card)
    {
        aroma_node_set_hidden(media_ui.media_card, false);
    }

    if (media.status[0] != '\0')
    {
        bool currently_playing = (strcmp(media.status, "playing") == 0);
        if (media_ui.is_playing != currently_playing)
        {
            media_ui.is_playing = currently_playing;
            update_play_pause_button_icon();
        }
    }
    else if (media.title[0] != '\0' || media.artist[0] != '\0')
    {
        if (!media_ui.is_playing)
        {
            media_ui.is_playing = true;
            update_play_pause_button_icon();
        }
    }

    if (media.title[0] && strcmp(media.title, media_ui.current_title) != 0)
    {
        strncpy(media_ui.current_title, media.title, sizeof(media_ui.current_title) - 1);
        media_ui.current_title[sizeof(media_ui.current_title) - 1] = '\0';
        media_ui.marquee_active = false;
        media_ui.marquee_offset = 0;
    }

    update_marquee_title();

    if (media_ui.media_artist_label)
    {
        if (media.artist[0])
            aroma_label_set_text(media_ui.media_artist_label, media.artist);
        else
            aroma_label_set_text(media_ui.media_artist_label, "Unknown Artist");
    }

    uint32_t display_position = media.position;
    uint32_t display_duration = media.duration;

    if (display_duration == 0 && media_ui.last_duration > 0)
    {
        display_duration = media_ui.last_duration;
    }

    if (media_ui.is_playing && display_duration > 0)
    {
        time_t now = time(NULL);
        if (media_ui.last_position_update > 0 && media.position > 0)
        {
            time_t elapsed = now - media_ui.last_position_update;
            uint32_t estimated_position = media.position + (uint32_t)(elapsed * 1000);

            if (estimated_position < display_duration)
            {
                display_position = estimated_position;
            }
            else
            {
                display_position = display_duration;
            }
        }

        if (media.position > 0)
        {
            media_ui.last_position_update = now;
            media_ui.last_position = media.position;
        }
    }
    else if (!media_ui.is_playing && media.position > 0)
    {
        media_ui.last_position_update = 0;
    }

    if (display_duration > 0)
    {
        media_ui.last_duration = display_duration;
    }

    if (media_ui.media_progress_bar)
    {
        if (display_duration > 0)
        {
            float progress = (float)display_position / (float)display_duration;
            if (progress < 0.0f)
                progress = 0.0f;
            if (progress > 1.0f)
                progress = 1.0f;
            aroma_progressbar_set_progress(media_ui.media_progress_bar, progress);
        }
        else
        {
            aroma_progressbar_set_progress(media_ui.media_progress_bar, 0.0f);
        }
    }

    if (media_ui.media_time_elapsed_label)
    {
        char buf[16];
        format_time_ms(display_position, buf, sizeof(buf));
        aroma_label_set_text(media_ui.media_time_elapsed_label, buf);
    }

    if (media_ui.media_time_remaining_label)
    {
        char buf[16];
        if (display_duration > display_position)
            format_time_ms(display_duration - display_position, buf, sizeof(buf));
        else if (display_duration > 0)
            snprintf(buf, sizeof(buf), "0:00");
        else
            snprintf(buf, sizeof(buf), "--:--");
        aroma_label_set_text(media_ui.media_time_remaining_label, buf);
    }

    pthread_mutex_unlock(&media_ui.lock);
}

static void *media_monitor_thread_func(void *arg)
{
    (void)arg;

    while (media_ui.monitor_running)
    {
        update_media_ui();
        usleep(MEDIA_UPDATE_INTERVAL_US);
    }

    return NULL;
}

static void ac_temp_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp < 30)
        state.current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16)
        state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static bool car_frontdoor_open(AromaNode *node, void *user_data)
{

    (void)user_data;
    aroma_image_set_source(state.overlay,
#ifdef __EMSCRIPTEN__
                           "/assets/car_frontdoor.png"
#elif defined(__arm__) || defined(__aarch64__)
                           "/usr/share/infotainment/assets/car_frontdoor.png"
#else
                           "../assets/car_frontdoor.png"
#endif
    );
    return true;
}
void opening_anim(AromaNode *target, float progress, void *user_data)
{
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(state.map_node);
    if (!maps_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.maps_app_icon);
    if (!icon_rect)
        return;

    AromaRect *recently_viewed_rect = aroma_node_get_rect(state.map_recently_viewed_card);
    if (!recently_viewed_rect)
        return;

    const int start_x = 260;
    const int start_y = WIN_H - 95;
    const int start_w = 48;
    const int start_h = 48;

    rect->x = start_x + (int)((0 - start_x) * progress);
    rect->y = start_y + (int)((0 - start_y) * progress);
    rect->width = start_w + (int)((WIN_W - start_w) * progress);
    rect->height = start_h + (int)((WIN_H - start_h) * progress);

    maps_rect->x = rect->x;
    maps_rect->y = rect->y;
    maps_rect->width = rect->width;
    maps_rect->height = rect->height;

    icon_rect->x = rect->x;
    icon_rect->y = rect->y;
    icon_rect->width = rect->width;
    icon_rect->height = rect->height;

    if (progress >= 0.92f)
    {

        recently_viewed_rect->x = rect->x + 20;
        recently_viewed_rect->y = rect->y + rect->height - recently_viewed_rect->height - 20;
        aroma_node_set_hidden(state.map_recently_viewed_card, false);
    }

    aroma_node_invalidate(state.map_node);
    aroma_node_invalidate(state.maps_app_icon);
    aroma_node_invalidate(target);
}

bool open_maps(AromaNode *node, void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, opening_anim, NULL);
    if (!anim)
        return false;

    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    aroma_node_set_hidden(state.map_node, false);
    aroma_node_set_hidden(state.map_close_btn, false);

    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);

    return true;
}
void closing_anim(AromaNode *target, float progress, void *user_data)
{
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(state.map_node);
    if (!maps_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.maps_app_icon);
    if (!icon_rect)
        return;

    const int end_x = 260;
    const int end_y = WIN_H - 95;
    const int end_w = 48;
    const int end_h = 48;

    rect->x = 0 + (int)((end_x - 0) * progress);
    rect->y = 0 + (int)((end_y - 0) * progress);
    rect->width = WIN_W + (int)((end_w - WIN_W) * progress);
    rect->height = WIN_H + (int)((end_h - WIN_H) * progress);

    maps_rect->x = rect->x;
    maps_rect->y = rect->y;
    maps_rect->width = rect->width;
    maps_rect->height = rect->height;

    icon_rect->x = rect->x;
    icon_rect->y = rect->y;
    icon_rect->width = rect->width;
    icon_rect->height = rect->height;

    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(state.map_close_btn, true);

        aroma_node_set_hidden(state.map_node, true);
    }

    aroma_node_invalidate(state.map_node);
    aroma_node_invalidate(state.maps_app_icon);
    aroma_node_invalidate(target);
}
void close_maps(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, closing_anim, NULL);

    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);

    aroma_node_set_hidden(state.map_recently_viewed_card, true);
}

void phone_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *phone_rect = aroma_node_get_rect(state.phone_node);
    if (!phone_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.phone_app_icon);
    if (!icon_rect)
        return;


    AromaRect *phone_tabs_rect = aroma_node_get_rect(state.phone_app_tabs);
    if (!phone_tabs_rect)
        return;

    const int start_x = 330;
    const int start_y = WIN_H - 95;
    const int start_w = 48;
    const int start_h = 48;

    rect->x = start_x + (int)((0 - start_x) * progress);
    rect->y = start_y + (int)((0 - start_y) * progress);
    rect->width = start_w + (int)((WIN_W - start_w) * progress);
    rect->height = start_h + (int)((WIN_H - start_h) * progress);

    phone_rect->x = rect->x;
    phone_rect->y = rect->y;
    phone_rect->width = rect->width;
    phone_rect->height = rect->height;

    icon_rect->x = rect->x;
    icon_rect->y = rect->y;
    icon_rect->width = rect->width;
    icon_rect->height = rect->height;
 

    phone_tabs_rect->x = rect->x;
    phone_tabs_rect->y = rect->y ;
    phone_tabs_rect->width = rect->width;
    phone_tabs_rect->height = 100;

  

    aroma_node_invalidate(state.phone_node);
    aroma_node_invalidate(state.phone_app_icon);
    aroma_node_invalidate(target);
}

bool open_phone(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, phone_opening_anim, NULL);
    if (!anim)
        return false;

    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    aroma_node_set_hidden(state.phone_node, false);
    aroma_node_set_hidden(state.phone_close_btn, false);
        aroma_node_set_hidden(state.phone_app_tabs, false);

    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);

    return true;
}

void phone_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *phone_rect = aroma_node_get_rect(state.phone_node);
    if (!phone_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.phone_app_icon);
    if (!icon_rect)
        return;


    AromaRect *phone_tabs_rect = aroma_node_get_rect(state.phone_app_tabs);
    if (!phone_tabs_rect)
        return;

    const int end_x = 330;
    const int end_y = WIN_H - 95;
    const int end_w = 48;
    const int end_h = 48;

    rect->x = 0 + (int)((end_x - 0) * progress);
    rect->y = 0 + (int)((end_y - 0) * progress);
    rect->width = WIN_W + (int)((end_w - WIN_W) * progress);
    rect->height = WIN_H + (int)((end_h - WIN_H) * progress);

    phone_rect->x = rect->x;
    phone_rect->y = rect->y;
    phone_rect->width = rect->width;
    phone_rect->height = rect->height;

    icon_rect->x = rect->x;
    icon_rect->y = rect->y;
    icon_rect->width = rect->width;
    icon_rect->height = rect->height;


    phone_tabs_rect->x = rect->x;
    phone_tabs_rect->y = rect->y + 90;
    phone_tabs_rect->width = rect->width;
    phone_tabs_rect->height = 100;

    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(state.phone_close_btn, true);
        aroma_node_set_hidden(state.phone_node, true);
        
    }

    aroma_node_invalidate(state.phone_node);
    aroma_node_invalidate(state.phone_app_icon);
    aroma_node_invalidate(target);
}

void close_phone(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, phone_closing_anim, NULL);
                aroma_node_set_hidden(state.phone_app_tabs, true);

    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);

}

void music_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *music_rect = aroma_node_get_rect(state.music_node);
    if (!music_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.music_app_icon);
    if (!icon_rect)
        return;

    const int start_x = 400;
    const int start_y = WIN_H - 95;
    const int start_w = 48;
    const int start_h = 48;

    rect->x = start_x + (int)((0 - start_x) * progress);
    rect->y = start_y + (int)((0 - start_y) * progress);
    rect->width = start_w + (int)((WIN_W - start_w) * progress);
    rect->height = start_h + (int)((WIN_H - start_h) * progress);

    music_rect->x = rect->x;
    music_rect->y = rect->y;
    music_rect->width = rect->width;
    music_rect->height = rect->height;

    icon_rect->x = rect->x;
    icon_rect->y = rect->y;
    icon_rect->width = rect->width;
    icon_rect->height = rect->height;

    if (progress >= 0.92f)
    {
        aroma_node_set_hidden(state.music_content_card, false);
    }

    aroma_node_invalidate(state.music_node);
    aroma_node_invalidate(state.music_app_icon);
    aroma_node_invalidate(target);
}

bool open_music(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, music_opening_anim, NULL);
    if (!anim)
        return false;

    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    aroma_node_set_hidden(state.music_node, false);
    aroma_node_set_hidden(state.music_close_btn, false);

    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);

    return true;
}

void music_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *music_rect = aroma_node_get_rect(state.music_node);
    if (!music_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.music_app_icon);
    if (!icon_rect)
        return;

    const int end_x = 400;
    const int end_y = WIN_H - 95;
    const int end_w = 48;
    const int end_h = 48;

    rect->x = 0 + (int)((end_x - 0) * progress);
    rect->y = 0 + (int)((end_y - 0) * progress);
    rect->width = WIN_W + (int)((end_w - WIN_W) * progress);
    rect->height = WIN_H + (int)((end_h - WIN_H) * progress);

    music_rect->x = rect->x;
    music_rect->y = rect->y;
    music_rect->width = rect->width;
    music_rect->height = rect->height;

    icon_rect->x = rect->x;
    icon_rect->y = rect->y;
    icon_rect->width = rect->width;
    icon_rect->height = rect->height;

    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(state.music_close_btn, true);
        aroma_node_set_hidden(state.music_node, true);
    }

    aroma_node_invalidate(state.music_node);
    aroma_node_invalidate(state.music_app_icon);
    aroma_node_invalidate(target);
}

void close_music(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, music_closing_anim, NULL);

    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);

    aroma_node_set_hidden(state.music_content_card, true);
}

static void battery_diagnostics(void *user_data)
{
    (void)user_data;

    aroma_image_set_source(state.overlay,
#ifdef __EMSCRIPTEN__
                           "/assets/car_battery.png"
#elif defined(__arm__) || defined(__aarch64__)
                           "/usr/share/infotainment/assets/car_battery.png"
#else
                           "../assets/car_battery.png"
#endif
    );

    AromaAnimation *anim = aroma_animation_start(
        state.overlay, AROMA_ANIM_SLIDE_Y, 150, 130, 400);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);

    aroma_node_set_hidden(state.vehicle_view_lock_divider, true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_divider, true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_icon, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_header, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_desc, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_divider, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_divider, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_header, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_desc, true);
    aroma_node_set_hidden(state.vehicle_view_lock_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_label, true);
    aroma_node_set_hidden(state.vehicle_view_warning_warning_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_action, true);
    aroma_node_set_hidden(state.battery_image, false);
    aroma_node_set_hidden(state.battery_health, false);
    aroma_node_set_hidden(state.battery_percentage, false);
    aroma_animation_start(state.battery_image, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_health, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_percentage, AROMA_ANIM_FADE, 0, 1, 1000);
}

void build_vehicle_view(AromaNode *window)
{
    state.vehicle_view_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.vehicle_view_root, Z_LAYER_BACKGROUND);

    AromaNode *backroad = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/backroad_blur.png"
#elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/backroad_blur.png"
#else
        "../assets/backroad_blur.png"
#endif
        ,
        0, 0, WIN_W, WIN_H);
    aroma_node_set_z_index(backroad, Z_LAYER_BACKGROUND);

    state.car_img = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/car.png"
#elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/car.png"
#else
        "../assets/car.png"
#endif
        ,
        150, 130, 700, 405);
    aroma_node_set_z_index(state.car_img, Z_LAYER_VEHICLE_IMAGE);

    state.overlay = aroma_ui_image(state.vehicle_view_root, NULL, 150, 130, 700, 405);
    aroma_node_set_z_index(state.overlay, Z_LAYER_VEHICLE_OVERLAYS);

    state.battery_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_BATTERY_FULL,
        WIN_W - 355, 22, 40, ICON_BUTTON_OUTLINED,
        battery_diagnostics, NULL, state.icon_font);
    aroma_node_set_z_index(state.battery_button, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_large_clock = aroma_ui_label(
        state.vehicle_view_root, "12:45",
        WIN_W / 2 - 90, 35, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_large_clock_pm_am = aroma_ui_label(
        state.vehicle_view_root, "PM",
        WIN_W / 2 + 90, 60, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock_pm_am, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.gear_bg_card = aroma_ui_card(
        state.vehicle_view_root, 25, 18, 225, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_bg_card, Z_LAYER_VEHICLE_OVERLAYS + 3);

    state.gear_fg_card = aroma_ui_card(state.gear_bg_card, 5, 5, 50, 40, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_fg_card, Z_LAYER_VEHICLE_OVERLAYS + 4);
    aroma_card_set_colors(state.gear_fg_card,
                          state.theme.colors.primary, state.theme.colors.primary);

    AromaNode *lbl_p = aroma_ui_label(state.gear_bg_card, "P", 22, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_r = aroma_ui_label(state.gear_bg_card, "R", 77, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_n = aroma_ui_label(state.gear_bg_card, "N", 132, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_d = aroma_ui_label(state.gear_bg_card, "D", 187, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(lbl_p, Z_LAYER_VEHICLE_OVERLAYS + 5);
    aroma_node_set_z_index(lbl_r, Z_LAYER_VEHICLE_OVERLAYS + 5);
    aroma_node_set_z_index(lbl_n, Z_LAYER_VEHICLE_OVERLAYS + 5);
    aroma_node_set_z_index(lbl_d, Z_LAYER_VEHICLE_OVERLAYS + 5);

    state.range_card = aroma_ui_card(state.vehicle_view_root, 680, WIN_H - 110, 300, 80, CARD_TYPE_GLASS);
    aroma_node_set_z_index(state.range_card, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_card_set_colors(state.range_card, 0x80FFFFFF, 0x80FFFFFF);

    AromaNode *range_header = aroma_ui_label(state.range_card, "Battery Range", 20, 5, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(range_header, Z_LAYER_VEHICLE_OVERLAYS + 3);
    AromaNode *range_progressbar = aroma_ui_progressbar(state.range_card, 20, 40, 260, 20, 0xFF00C853, 0xFFBDBDBD);
    aroma_node_set_z_index(range_progressbar, Z_LAYER_VEHICLE_OVERLAYS + 3);

    state.vehicle_view_frunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 260, 260, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_frunk_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_lock_divider = aroma_ui_divider(
        state.vehicle_view_root, 550, 165, 40, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_lock_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_lock_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_LOCK, 563, 130, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_lock_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_header = aroma_ui_label(
        state.vehicle_view_root, "Frunk", 180, 250, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_header, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Open", 180, 270, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_desc, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_trunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 780, 220, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_trunk_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_trunk_header = aroma_ui_label(
        state.vehicle_view_root, "Trunk", 800, 215, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_header, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_trunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Closed", 800, 235, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_desc, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_charge_port_divider = aroma_ui_divider(
        state.vehicle_view_root, 810, 330, 40, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(state.vehicle_view_charge_port_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_charge_port_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_POWER, 880, 315, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_charge_port_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_warning_message_card = aroma_ui_card(
        state.vehicle_view_root, 330, WIN_H + 100, 600, 70, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.vehicle_view_warning_message_card, Z_LAYER_CARDS_BOTTOM + 50);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);

    state.vehicle_view_warning_warning_icon = aroma_ui_icon(
        state.vehicle_view_warning_message_card, AROMA_ICON_WARNING,
        65, 22, 24, 0xFFFFD600, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_warning_warning_icon, Z_LAYER_CARDS_BOTTOM + 51);

    state.vehicle_view_warning_message_label = aroma_ui_label(
        state.vehicle_view_warning_message_card,
        "Warning: The Frunk is Open. Close it before driving.",
        110, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_warning_message_label, Z_LAYER_CARDS_BOTTOM + 51);

    state.battery_image = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/charging.png"
#elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/charging.png"
#else
        "../assets/charging.png"
#endif
        ,
        WIN_W / 2 - 180, 200, 128, 128);
    aroma_node_set_z_index(state.battery_image, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_node_set_hidden(state.battery_image, true);

    state.battery_health = aroma_ui_label(
        state.vehicle_view_root, "Battery Health: Good",
        WIN_W / 2 - 20, 220, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_health, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_label_set_color(state.battery_health, 0xFF00C853);
    aroma_node_set_hidden(state.battery_health, true);

    state.battery_percentage = aroma_ui_label(
        state.vehicle_view_root, "85%",
        WIN_W / 2 - 20, 260, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_percentage, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_node_set_hidden(state.battery_percentage, true);

    media_ui.media_card = aroma_ui_card(
        state.vehicle_view_root, 50, WIN_H - 110, 380, 80, CARD_TYPE_GLASS);
    aroma_node_set_z_index(media_ui.media_card, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_card_set_colors(media_ui.media_card, 0x80FFFFFF, 0x80FFFFFF);
    aroma_node_set_hidden(media_ui.media_card, true);

    media_ui.media_title_label = aroma_ui_label(
        media_ui.media_card, "No Track", 16, 12,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(media_ui.media_title_label, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_artist_label = aroma_ui_label(
        media_ui.media_card, "No Artist", 16, 40,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(media_ui.media_artist_label, Z_LAYER_VEHICLE_OVERLAYS + 3);
    aroma_label_set_color(media_ui.media_artist_label, 0xFFAAAAAA);

    media_ui.media_prev_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_SKIP_PREVIOUS,
        230, 22, 36, ICON_BUTTON_OUTLINED,
        on_media_prev_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_prev_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_play_pause_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_PLAY_ARROW,
        274, 22, 36, ICON_BUTTON_OUTLINED,
        on_media_play_pause_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_play_pause_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_next_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_SKIP_NEXT,
        318, 22, 36, ICON_BUTTON_OUTLINED,
        on_media_next_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_next_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    state.bottom_bar = aroma_ui_card(state.vehicle_view_root, 230, WIN_H - 110, 420, 80, CARD_TYPE_GLASS);
    aroma_card_set_colors(state.bottom_bar, 0x80FFFFFF, 0x80FFFFFF);
    aroma_node_set_z_index(state.bottom_bar, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.maps_app_icon = aroma_ui_image(state.bottom_bar,
#ifdef __EMSCRIPTEN__
                                         "/assets/maps_app.png"
#elif defined(__arm__) || defined(__aarch64__)
                                         "/usr/share/infotainment/assets/maps_app.png"
#else
                                         "../assets/maps_app.png"
#endif
                                         ,
                                         30, 15, 48, 48);
    aroma_node_set_z_index(state.maps_app_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *maps_app_icon_card = aroma_ui_card(state.bottom_bar, 30, 15, 48, 48, CARD_TYPE_FILLED);
    aroma_image_set_on_click(state.maps_app_icon, open_maps, maps_app_icon_card);

    state.phone_app_icon = aroma_ui_image(state.bottom_bar,
#ifdef __EMSCRIPTEN__
                                               "/assets/phone_app.png"
#elif defined(__arm__) || defined(__aarch64__)
                                               "/usr/share/infotainment/assets/phone_app.png"
#else
                                               "../assets/phone_app.png"
#endif
                                               ,
                                               100, 15, 48, 48);
    aroma_node_set_z_index(state.phone_app_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *phone_app_icon_card = aroma_ui_card(state.bottom_bar, 100, 15, 48, 48, CARD_TYPE_OUTLINED);
    aroma_image_set_on_click(state.phone_app_icon, open_phone, phone_app_icon_card);

    state.music_app_icon = aroma_ui_image(state.bottom_bar,
#ifdef __EMSCRIPTEN__
                                               "/assets/music_app.png"
#elif defined(__arm__) || defined(__aarch64__)
                                               "/usr/share/infotainment/assets/music_app.png"
#else
                                               "../assets/music_app.png"
#endif
                                               ,
                                               170, 15, 48, 48);
    aroma_node_set_z_index(state.music_app_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *music_app_icon_card = aroma_ui_card(state.bottom_bar, 170, 15, 48, 48, CARD_TYPE_FILLED);
    aroma_image_set_on_click(state.music_app_icon, open_music, music_app_icon_card);

    AromaNode *divider_to_ac = aroma_ui_divider(state.bottom_bar, 240, 10, 60, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(divider_to_ac, Z_LAYER_VEHICLE_OVERLAYS + 2);

    AromaNode *ac_minus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_REMOVE, 260, 25, 30, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_minus, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *ac_temp_label = aroma_ui_label(state.bottom_bar, "22°C", 308, 22, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(ac_temp_label, Z_LAYER_VEHICLE_OVERLAYS + 2);
    state.ac_temp_label = ac_temp_label;
    AromaNode *ac_plus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_ADD, 370, 25, 30, ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_plus, Z_LAYER_VEHICLE_OVERLAYS + 2);
    state.map_node = aroma_ui_map(maps_app_icon_card, 0, 0, 48, 48);
    aroma_map_set_zoom(state.map_node, 15);
    aroma_map_set_center(state.map_node, 37.7749, -122.4194);
    // set markers for recently viewed destinations
    for (int i = 0; i < RECENTLY_VIEWED_COUNT; i++)
    {
        aroma_map_add_popup_marker(state.map_node, recently_viewed_destinations[i].lat, recently_viewed_destinations[i].lon, 0xFF0000FF, recently_viewed_destinations[i].name);
    }
    aroma_node_set_z_index(state.map_node, Z_LAYER_STATUS_BAR + 11);
    state.map_close_btn = aroma_ui_iconbutton(maps_app_icon_card, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED, close_maps, maps_app_icon_card, state.icon_font);
    aroma_node_set_z_index(state.map_close_btn, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_z_index(state.map_node, Z_LAYER_STATUS_BAR + 11);
    aroma_node_set_hidden(state.map_node, true);
    aroma_node_set_hidden(state.map_close_btn, true);
    state.map_recently_viewed_card = aroma_ui_card(maps_app_icon_card, 50, 50, 380, 320, CARD_TYPE_FILLED);
    aroma_node_set_hidden(state.map_recently_viewed_card, true);
    aroma_node_set_z_index(state.map_recently_viewed_card, Z_LAYER_STATUS_BAR + 11);
    aroma_card_set_colors(state.map_recently_viewed_card, 0xCCFFFFFF, 0xCCFFFFFF);
    AromaNode *map_recently_viewed_label = aroma_ui_label(state.map_recently_viewed_card, "Recently Viewed", 16, 12, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(map_recently_viewed_label, Z_LAYER_STATUS_BAR + 11);
    AromaNode *map_recently_viewed_icon = aroma_ui_icon(state.map_recently_viewed_card, AROMA_ICON_HISTORY, 340, 20, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(map_recently_viewed_icon, Z_LAYER_STATUS_BAR + 11);

    for (int i = 0; i < RECENTLY_VIEWED_COUNT; i++)
    {
        int row_y = 65 + (i * 48);

        AromaNode *row_label = aroma_ui_label(
            state.map_recently_viewed_card,
            recently_viewed_destinations[i].name,
            16, row_y, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        aroma_node_set_z_index(row_label, Z_LAYER_STATUS_BAR + 11);

        AromaNode *row_btn = aroma_ui_iconbutton(
            state.map_recently_viewed_card, AROMA_ICON_ARROW_FORWARD,
            310, row_y, 44, ICON_BUTTON_FILLED,
            on_destination_click, (void *)&recently_viewed_destinations[i],
            state.icon_font);
        aroma_node_set_z_index(row_btn, Z_LAYER_STATUS_BAR + 11);
    }
    AromaNode *rv_divider = aroma_ui_divider(state.map_recently_viewed_card, 16, 50, 348, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(rv_divider, Z_LAYER_STATUS_BAR + 11);

    state.phone_node = aroma_ui_container(
        phone_app_icon_card, 0, 0, 48, 48,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.phone_node, Z_LAYER_STATUS_BAR + 11);
    aroma_node_set_hidden(state.phone_node, true);

    state.phone_close_btn = aroma_ui_iconbutton(
        phone_app_icon_card, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
        close_phone, phone_app_icon_card, state.icon_font);
    aroma_node_set_z_index(state.phone_close_btn, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(state.phone_close_btn, true);


    AromaNode* aroma_contacts_listview = aroma_ui_listview(
        phone_app_icon_card, 16, 110, 988, 700,
NULL, NULL, state.ui_font);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);
        aroma_listview_add_item(aroma_contacts_listview, "John Doe", "555-1234", NULL);

    aroma_node_set_z_index(aroma_contacts_listview, Z_LAYER_STATUS_BAR + 12);
    state.phone_app_tabs = aroma_ui_tabs_with_icons(
        phone_app_icon_card, 0, 0, 1024, 50,
        (const char *[]){"Contacts", "Call History", "Dialer"}, (const char *[]){AROMA_ICON_CONTACTS, AROMA_ICON_CALL, AROMA_ICON_DIALER_SIP}, 3, NULL, NULL, state.settings_font, state.big_icon_font);
    
        aroma_node_set_z_index(state.phone_app_tabs, Z_LAYER_STATUS_BAR + 11);
            aroma_node_set_hidden(state.phone_app_tabs, true);
        AromaNode* content[] = {aroma_contacts_listview, NULL, NULL}; 
    aroma_tabs_set_content(state.phone_app_tabs, 0, content, 3);
    state.music_node = aroma_ui_container(
        music_app_icon_card, 0, 0, 48, 48,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.music_node, Z_LAYER_STATUS_BAR + 11);
    aroma_node_set_hidden(state.music_node, true);

    state.music_close_btn = aroma_ui_iconbutton(
        music_app_icon_card, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
        close_music, music_app_icon_card, state.icon_font);
    aroma_node_set_z_index(state.music_close_btn, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(state.music_close_btn, true);

    state.music_content_card = aroma_ui_card(music_app_icon_card, 50, 50, 380, 320, CARD_TYPE_FILLED);
    aroma_node_set_hidden(state.music_content_card, true);
    aroma_node_set_z_index(state.music_content_card, Z_LAYER_STATUS_BAR + 11);
    aroma_card_set_colors(state.music_content_card, 0xCCFFFFFF, 0xCCFFFFFF);

    AromaNode *music_content_label = aroma_ui_label(
        state.music_content_card, "Music Library", 16, 12, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(music_content_label, Z_LAYER_STATUS_BAR + 11);
    AromaNode *music_content_icon = aroma_ui_icon(
        state.music_content_card, AROMA_ICON_MUSIC_NOTE, 340, 20, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(music_content_icon, Z_LAYER_STATUS_BAR + 11);
    AromaNode *music_content_divider = aroma_ui_divider(
        state.music_content_card, 16, 50, 348, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(music_content_divider, Z_LAYER_STATUS_BAR + 11);

    aroma_animation_start(state.vehicle_view_frunk_divider, AROMA_ANIM_SCALE_Y, 0, 60, 1200);
    aroma_animation_start(state.vehicle_view_trunk_divider, AROMA_ANIM_SCALE_Y, 0, 60, 1200);
    aroma_animation_start(state.vehicle_view_lock_divider, AROMA_ANIM_SCALE_Y, 0, 60, 1200);
    aroma_animation_start(state.vehicle_view_charge_port_divider, AROMA_ANIM_SCALE_X, 0, 40, 1200);

    media_ui.monitor_running = true;
    pthread_create(&media_ui.monitor_thread, NULL, media_monitor_thread_func, NULL);
}