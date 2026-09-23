#include "apps/media/media_controls.h"
#include "bt_speaker_api.h"
#include "media_bt_service.h"
#include "vehicle_view.h"
#include "aroma_animation.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>

void apply_deferred_bottom_bar_position(void);
void restore_app_drawer_from_behind(void);
bool open_music(AromaNode *node, void *user_data);
void set_app_open(bool open);
extern bool app_drawer_visible;
void send_app_drawer_behind(void);
#include "app_registry.h"
bool is_any_app_open(void);

AromaNode *music_app_tabs = NULL;

/* Install-dir-joined asset path for bundled files (assets/...). */
static char s_install_dir[512] = "";

static void music_asset_path(char *out, size_t out_len, const char *file)
{
    if (s_install_dir[0])
        snprintf(out, out_len, "%s/assets/%s", s_install_dir, file);
    else
        snprintf(out, out_len, "assets/%s", file);
}

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

extern MediaPlayerUI media_ui;

bool music_app_open = false;
int music_active_tab = 0;

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
    update_media_card_display();
    update_music_play_pause_icon();
}

void on_music_next_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_next();
}

void update_music_now_playing_display(void)
{
    if (!music_now_playing_card)
        return;

    /* Owned stack in this same .so: thread-safe snapshots straight
     * from it, no host mirrors. */
    bt_media_info_t media = bt_speaker_get_media_info();
    bt_state_t current_state = bt_speaker_get_state();

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

    bt_device_info_t device = bt_speaker_get_device_info();
    bt_state_t current_state = bt_speaker_get_state();

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
        bt_stats_t stats = bt_speaker_get_stats();
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
    // See phone_opening_anim: only the app card moves; the tabs widget
    // keeps its creation geometry for draw/hit-test.
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;

    int start_y = WIN_H;
    int end_y = 0;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

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
        card_node, 0.0f, 1.0f, APP_ANIM_MS, music_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_OPEN_EASE);
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
    // See phone_opening_anim: only the app card moves.
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;

    int start_y = 0;
    int end_y = WIN_H;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

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
        card_node, 0.0f, 1.0f, APP_ANIM_MS, music_closing_anim, NULL);
    aroma_node_set_hidden(music_now_playing_card, true);
    aroma_node_set_hidden(music_device_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_CLOSE_EASE);
}

void build_music_app_ui(AromaNode *parent);

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


void build_music_app_ui(AromaNode *parent)
{
    if (!parent || music_app_tabs) return; // Already built
    
    const char *labels[] = {"Now Playing", "Devices"};
    // Header-strip height: the tabs widget draws its bar/labels from this
    // rect, so a full-screen height here paints the bar over the content.
    music_app_tabs = aroma_tabs_create(
        parent, 0, 0, WIN_W, 50,
        labels, 2);
    aroma_node_set_z_index(music_app_tabs, Z_LAYER_STATUS_BAR + 11);
    aroma_node_set_hidden(music_app_tabs, true);
    aroma_tabs_set_font(music_app_tabs, state.ui_font);
    aroma_tabs_set_on_change(music_app_tabs, on_music_tab_changed, NULL);
    aroma_tabs_setup_events(music_app_tabs, aroma_ui_request_redraw, NULL);
    
    // Now Playing tab content
    music_now_playing_card = aroma_ui_card(
        music_app_tabs, 0, 90, WIN_W, WIN_H - 90, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(music_now_playing_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(music_now_playing_card, true);
    
    char art_path[768];
    music_asset_path(art_path, sizeof(art_path), "album_cover.jpg");
    music_art_placeholder = aroma_ui_image(
        music_now_playing_card, art_path, 60, 40, 200, 200);
    aroma_node_set_z_index(music_art_placeholder, Z_LAYER_STATUS_BAR + 13);
    
    music_track_title_label = aroma_ui_label(
        music_now_playing_card, "No Track Playing",
        60, 260, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(music_track_title_label, Z_LAYER_STATUS_BAR + 13);
    
    music_track_artist_label = aroma_ui_label(
        music_now_playing_card, "No Artist",
        60, 290, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(music_track_artist_label, Z_LAYER_STATUS_BAR + 13);
    
    music_track_album_label = aroma_ui_label(
        music_now_playing_card, "No Album",
        60, 320, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(music_track_album_label, Z_LAYER_STATUS_BAR + 13);
    
    music_status_label = aroma_ui_label(
        music_now_playing_card, "Not connected",
        60, 350, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(music_status_label, Z_LAYER_STATUS_BAR + 13);
    
    music_prev_button = aroma_ui_iconbutton(
        music_now_playing_card, AROMA_ICON_SKIP_PREVIOUS,
        120, 400, 50, ICON_BUTTON_OUTLINED,
        on_music_prev_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_prev_button, Z_LAYER_STATUS_BAR + 13);
    
    music_play_pause_button = aroma_ui_iconbutton(
        music_now_playing_card, AROMA_ICON_PLAY_ARROW,
        195, 400, 50, ICON_BUTTON_OUTLINED,
        on_music_play_pause_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_play_pause_button, Z_LAYER_STATUS_BAR + 13);
    
    music_next_button = aroma_ui_iconbutton(
        music_now_playing_card, AROMA_ICON_SKIP_NEXT,
        270, 400, 50, ICON_BUTTON_OUTLINED,
        on_music_next_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_next_button, Z_LAYER_STATUS_BAR + 13);
    
    music_no_media_label = aroma_ui_label(
        music_now_playing_card, "No media playing",
        WIN_W / 2 - 100, 150, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(music_no_media_label, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_hidden(music_no_media_label, false);
    
    // Device tab content
    music_device_card = aroma_ui_card(
        music_app_tabs, 0, 90, WIN_W, WIN_H - 90, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(music_device_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(music_device_card, true);
    
    music_device_status_icon = aroma_ui_icon(
        music_device_card, AROMA_ICON_BLUETOOTH,
        60, 60, 48, 0xFF888888, state.icon_font);
    aroma_node_set_z_index(music_device_status_icon, Z_LAYER_STATUS_BAR + 13);
    
    music_device_status_label = aroma_ui_label(
        music_device_card, "No device connected",
        120, 55, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(music_device_status_label, Z_LAYER_STATUS_BAR + 13);
    
    music_device_name_label = aroma_ui_label(
        music_device_card, "",
        60, 110, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(music_device_name_label, Z_LAYER_STATUS_BAR + 13);
    
    music_device_address_label = aroma_ui_label(
        music_device_card, "",
        60, 140, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(music_device_address_label, Z_LAYER_STATUS_BAR + 13);
    
    music_device_stats_label = aroma_ui_label(
        music_device_card, "",
        60, 170, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(music_device_stats_label, Z_LAYER_STATUS_BAR + 13);
    
    music_device_no_phone_label = aroma_ui_label(
        music_device_card, "No Bluetooth phone connected",
        WIN_W / 2 - 150, WIN_H / 2 - 50, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(music_device_no_phone_label, Z_LAYER_STATUS_BAR + 13);

    // Register content nodes with tabs
    AromaNode *now_playing_content[] = {music_now_playing_card};
    AromaNode *devices_content[] = {music_device_card};
    aroma_tabs_set_content(music_app_tabs, 0, now_playing_content, 1);
    aroma_tabs_set_content(music_app_tabs, 1, devices_content, 1);

    // Below the tab header (y 0..50) so it never covers a tab.
    AromaNode *music_close_btn = aroma_ui_iconbutton(
        parent, AROMA_ICON_CLOSE, WIN_W - 68, 54, 40, ICON_BUTTON_FILLED,
        close_music, parent, state.icon_font);
    aroma_node_set_z_index(music_close_btn, Z_LAYER_STATUS_BAR + 16);
}



/* --- Full-app live refresh ----------------------------------------------
 * Polls the owned stack's thread-safe getters every frame while open. */
#include "bt_speaker_api.h"

static void music_hook_update(struct AromaNode *app_root)
{
    (void)app_root;
    if (!music_app_open)
        return;
    bt_media_info_t media = bt_speaker_get_media_info();
    static char last_key[256] = "";
    char key[256];
    snprintf(key, sizeof(key), "%.80s|%.80s|%.32s",
             media.title, media.artist, media.status);
    if (strcmp(key, last_key) != 0)
    {
        snprintf(last_key, sizeof(last_key), "%.255s", key);
        if (music_active_tab == 0)
            update_music_now_playing_display();
        else
            update_music_device_display();
    }
}

/* --- .apak plugin entry ---------------------------------------------------
 * Self-managed chrome: open_music/close_music own the drawer, z-order and
 * slide animation exactly as the former built-in app did. */
#include "aroma_package.h"

static AromaAppPlugin s_music_mirror;

static bool music_hook_init(const AromaPackageManifest *manifest,
                            const char *install_dir,
                            const AromaPackageHost *host,
                            struct AromaNode *app_root)
{
    (void)manifest;
    (void)host;
    (void)app_root;
    /* Bundled assets resolve against our own install dir (self-contained
     * package, not host asset paths). */
    if (install_dir)
        snprintf(s_install_dir, sizeof(s_install_dir), "%s", install_dir);
    else
        s_install_dir[0] = '\0';
    memset(&s_music_mirror, 0, sizeof(s_music_mirror));
    s_music_mirror.id = "com.aroma.media";
    s_music_mirror.name = "Media";
    /* Defensive reset: build_music_app_ui early-returns when
     * music_app_tabs != NULL (blank app_root on reinstall). Destroy clears
     * these, but reset here too in case a previous teardown was missed. */
    music_app_tabs = NULL;
    music_now_playing_card = NULL;
    music_art_placeholder = NULL;
    music_track_title_label = NULL;
    music_track_artist_label = NULL;
    music_track_album_label = NULL;
    music_status_label = NULL;
    music_prev_button = NULL;
    music_play_pause_button = NULL;
    music_next_button = NULL;
    music_no_media_label = NULL;
    music_open_btn = NULL;
    music_device_card = NULL;
    music_device_status_icon = NULL;
    music_device_status_label = NULL;
    music_device_name_label = NULL;
    music_device_address_label = NULL;
    music_device_stats_label = NULL;
    music_device_no_phone_label = NULL;
    music_app_open = false;
    music_active_tab = 0;
    return true;
}

static bool music_hook_build_ui(struct AromaNode *app_root)
{
    s_music_mirror.app_root = app_root;
    build_music_app_ui(app_root);
    return music_app_tabs != NULL;
}

static bool music_hook_show(struct AromaNode *app_root)
{
    return open_music(NULL, app_root);
}

static void music_hook_hide(struct AromaNode *app_root)
{
    close_music(app_root);
}

static void music_hook_destroy(void)
{
    /* Stop the owned stack so a live update can dlclose this .so without
     * stranding its D-Bus/PulseAudio threads. */
    media_bt_service()->set_enabled(false, NULL);
    /* Clear every node pointer into the dying tree so a reinstall rebuilds
     * instead of early-returning on a stale non-NULL guard (blank) or
     * touching freed nodes (crash). The tree itself is freed by the host. */
    music_app_tabs = NULL;
    music_now_playing_card = NULL;
    music_art_placeholder = NULL;
    music_track_title_label = NULL;
    music_track_artist_label = NULL;
    music_track_album_label = NULL;
    music_status_label = NULL;
    music_prev_button = NULL;
    music_play_pause_button = NULL;
    music_next_button = NULL;
    music_no_media_label = NULL;
    music_open_btn = NULL;
    music_device_card = NULL;
    music_device_status_icon = NULL;
    music_device_status_label = NULL;
    music_device_name_label = NULL;
    music_device_address_label = NULL;
    music_device_stats_label = NULL;
    music_device_no_phone_label = NULL;
    music_app_open = false;
    music_active_tab = 0;
    memset(&s_music_mirror, 0, sizeof(s_music_mirror));
}

static const AromaPackageHooks s_music_hooks = {
    .init = music_hook_init,
    .build_ui = music_hook_build_ui,
    .show = music_hook_show,
    .hide = music_hook_hide,
    .update = music_hook_update,
    .destroy = music_hook_destroy,
};

const AromaPackageHooks *aroma_package_entry(void)
{
    return &s_music_hooks;
}
