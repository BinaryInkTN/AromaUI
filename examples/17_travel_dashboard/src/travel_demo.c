

#include "travel_demo.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aroma_material_icons.h"
#include "aroma_node.h"
#include "aroma_timer.h"
#include "aroma_ui.h"
#include "widgets/aroma_iconbutton.h"
#include "widgets/aroma_label.h"
#include "widgets/aroma_map.h"
#include "widgets/aroma_snackbar.h"

#define MAP_CENTER_LAT 34.0367
#define MAP_CENTER_LON -118.7798
#define MAP_ZOOM 14
#define MAP_ROUTE_COLOR 0xFF1A73E8u
#define MAP_ROUTE_WIDTH 18

static const double k_route_lats[] = {34.0570, 34.0259};
static const double k_route_lons[] = {-118.7798, -118.7798};

#define TRACK_COUNT 3

typedef struct TravelTrack
{
    const char *title;
    const char *artist;
} TravelTrack;

static const TravelTrack k_tracks[TRACK_COUNT] = {
    {"All This Dust", "Canasta"},
    {"Slow Motion", "Canasta"},
    {"Golden Hour", "Canasta"},
};

static IncenseRegistry *s_registry;
static bool s_message_playing;
static bool s_music_playing;
static int s_track;
static AromaTimer *s_reply_timer;

static const char *const k_snacks[] = {
    "travel_snack_msg_play", "travel_snack_msg_pause",
    "travel_snack_reply", "travel_snack_music_play",
    "travel_snack_music_pause", "travel_snack_prev",
    "travel_snack_next", "travel_snack_more",
    "travel_snack_apps", "travel_snack_bell", "travel_snack_mic",
    "travel_snack_banner", "travel_snack_eta_hidden",
    "travel_snack_arrow",
};



static AromaNode *travel_node(const char *id)
{
    return (s_registry && id) ? IncenseFindWidgetQuiet(s_registry, id) : NULL;
}

static void travel_show(const char *id, bool visible)
{
    AromaNode *n = travel_node(id);
    if (n)
        aroma_node_set_hidden(n, !visible);
}

static void travel_text(const char *id, const char *text)
{
    AromaNode *n = travel_node(id);
    if (n)
        aroma_label_set_text(n, text ? text : "");
}

static void travel_set_iconbutton(const char *id, const char *glyph)
{
    AromaNode *n = travel_node(id);
    if (n)
        aroma_iconbutton_set_icon(n, glyph);
}

static void travel_dismiss_snacks(void)
{
    for (size_t i = 0; i < sizeof(k_snacks) / sizeof(k_snacks[0]); i++)
    {
        AromaNode *n = travel_node(k_snacks[i]);
        if (n)
            aroma_snackbar_dismiss(n);
    }
}

static void travel_toast(const char *id)
{
    travel_dismiss_snacks();
    AromaNode *n = travel_node(id);
    if (n)
        aroma_snackbar_show(n);
}

static void travel_reg_void(const char *name, void (*fn)(void *), void *ud)
{
    IncenseRegisterCallback(name, INCENSE_CALLBACK_VOID_PTR, (void *)fn, ud);
}



static void travel_set_message_playing(bool playing)
{
    s_message_playing = playing;
    travel_set_iconbutton("travel_msg_play_icon",
                          playing ? AROMA_ICON_PAUSE : AROMA_ICON_PLAY_ARROW);
    travel_text("travel_msg_sub", playing ? "Playing message..." : "New message");
}

static void travel_set_reply_sent(bool sent)
{
    travel_text("travel_msg_sub", sent ? "Reply sent" : "New message");
    travel_set_iconbutton("travel_msg_reply_icon",
                          sent ? AROMA_ICON_CHECK : AROMA_ICON_REPLY);
}

static void travel_set_music_playing(bool playing)
{
    s_music_playing = playing;
    travel_set_iconbutton("travel_music_toggle_icon",
                          playing ? AROMA_ICON_PAUSE : AROMA_ICON_PLAY_ARROW);
    travel_set_iconbutton("travel_album_badge_icon",
                          playing ? AROMA_ICON_PAUSE : AROMA_ICON_PLAY_ARROW);
}

static void travel_set_track(int index)
{
    s_track = (index % TRACK_COUNT + TRACK_COUNT) % TRACK_COUNT;
    travel_text("travel_music_title", k_tracks[s_track].title);
    travel_text("travel_music_artist", k_tracks[s_track].artist);
}

static void travel_set_eta_visible(bool visible)
{
    travel_show("travel_eta", visible);
}

static void travel_reply_reset(void *user_data)
{
    (void)user_data;
    s_reply_timer = NULL;
    if (!s_registry)
        return;
    travel_set_reply_sent(false);
    aroma_ui_request_redraw(NULL);
}

static void travel_cancel_reply_timer(void)
{
    if (s_reply_timer)
    {
        aroma_timer_cancel(s_reply_timer);
        s_reply_timer = NULL;
    }
}

static void travel_setup_map(void)
{
    AromaNode *map = travel_node("travel_map_widget");
    if (!map)
        return;
    aroma_map_set_animations_enabled(map, false);
    aroma_map_set_zoom(map, MAP_ZOOM);
    aroma_map_set_center_instant(map, MAP_CENTER_LAT, MAP_CENTER_LON);
    aroma_map_set_route_width(map, MAP_ROUTE_WIDTH);
    aroma_map_set_route_points(map, k_route_lats, k_route_lons, 2, MAP_ROUTE_COLOR);
}



static void travel_cb_msg_play(void *userdata)
{
    (void)userdata;
    travel_cancel_reply_timer();
    travel_set_message_playing(!s_message_playing);
    travel_toast(s_message_playing ? "travel_snack_msg_play"
                                   : "travel_snack_msg_pause");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_msg_reply(void *userdata)
{
    (void)userdata;
    travel_set_reply_sent(true);
    travel_toast("travel_snack_reply");
    travel_cancel_reply_timer();
    s_reply_timer = aroma_timer_create(2200, false, travel_reply_reset, NULL);
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_music_toggle(void *userdata)
{
    (void)userdata;
    travel_set_music_playing(!s_music_playing);
    travel_toast(s_music_playing ? "travel_snack_music_play"
                                 : "travel_snack_music_pause");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_album_toggle(void *userdata)
{
    (void)userdata;
    if (!s_music_playing)
    {
        travel_set_music_playing(true);
        travel_toast("travel_snack_music_play");
    }
    else
    {
        travel_set_music_playing(false);
        travel_toast("travel_snack_music_pause");
    }
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_music_prev(void *userdata)
{
    (void)userdata;
    travel_set_track(s_track - 1);
    travel_set_music_playing(true);
    travel_toast("travel_snack_prev");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_music_next(void *userdata)
{
    (void)userdata;
    travel_set_track(s_track + 1);
    travel_set_music_playing(true);
    travel_toast("travel_snack_next");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_music_more(void *userdata)
{
    (void)userdata;
    travel_toast("travel_snack_more");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_apps(void *userdata)
{
    (void)userdata;
    travel_toast("travel_snack_apps");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_bell(void *userdata)
{
    (void)userdata;
    travel_toast("travel_snack_bell");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_mic(void *userdata)
{
    (void)userdata;
    travel_toast("travel_snack_mic");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_banner(void *userdata)
{
    (void)userdata;
    travel_set_eta_visible(true);
    travel_toast("travel_snack_banner");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_eta_close(void *userdata)
{
    (void)userdata;
    travel_set_eta_visible(false);
    travel_toast("travel_snack_eta_hidden");
    aroma_ui_request_redraw(NULL);
}

static void travel_cb_arrow(void *userdata)
{
    (void)userdata;
    AromaNode *map = travel_node("travel_map_widget");
    if (map)
    {
        aroma_map_set_zoom(map, MAP_ZOOM);
        aroma_map_set_center_instant(map, MAP_CENTER_LAT, MAP_CENTER_LON);
    }
    travel_toast("travel_snack_arrow");
    aroma_ui_request_redraw(NULL);
}



void travel_register_callbacks(void)
{
    travel_reg_void("travel_msg_play", travel_cb_msg_play, NULL);
    travel_reg_void("travel_msg_reply", travel_cb_msg_reply, NULL);
    travel_reg_void("travel_album_toggle", travel_cb_album_toggle, NULL);
    travel_reg_void("travel_music_toggle", travel_cb_music_toggle, NULL);
    travel_reg_void("travel_music_prev", travel_cb_music_prev, NULL);
    travel_reg_void("travel_music_next", travel_cb_music_next, NULL);
    travel_reg_void("travel_music_more", travel_cb_music_more, NULL);
    travel_reg_void("travel_apps", travel_cb_apps, NULL);
    travel_reg_void("travel_bell", travel_cb_bell, NULL);
    travel_reg_void("travel_mic", travel_cb_mic, NULL);
    travel_reg_void("travel_banner", travel_cb_banner, NULL);
    travel_reg_void("travel_eta_close", travel_cb_eta_close, NULL);
    travel_reg_void("travel_arrow", travel_cb_arrow, NULL);
}

void travel_after_mount(IncenseRegistry *registry)
{
    s_registry = registry;
    travel_cancel_reply_timer();
    s_message_playing = false;
    s_music_playing = true;
    s_track = 0;

    if (!travel_node("travel_root"))
        return;

    travel_dismiss_snacks();
    travel_set_message_playing(false);
    travel_set_reply_sent(false);
    travel_set_music_playing(true);
    travel_set_track(0);
    travel_set_eta_visible(true);
    travel_setup_map();
    aroma_ui_request_redraw(NULL);
}
