#include "vehicle_view.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "bt_speaker_api.h"
#include "bt_speaker_hfp.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
void populate_contact_listview(AromaNode *listview);

#define MEDIA_UPDATE_INTERVAL_US 500000
#define CONTACTS_PER_PAGE 10
#define MAX_DIALER_DIGITS 32

#define BOTTOM_BAR_X_COLLAPSED 302
#define BOTTOM_BAR_X_EXPANDED 512

static bool bottom_bar_app_open = false;
static pthread_mutex_t app_open_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t contact_list_lock = PTHREAD_MUTEX_INITIALIZER;

static char dialer_number[MAX_DIALER_DIGITS] = "";
static AromaNode *dialer_display_label = NULL;
static AromaNode *dialer_card = NULL;
static int sorted_to_original[100];

static AromaNode *incoming_call_overlay = NULL;
static AromaNode *incoming_call_name_label = NULL;
static AromaNode *incoming_call_number_label = NULL;
static AromaNode *incoming_call_accept_btn = NULL;
static AromaNode *incoming_call_reject_btn = NULL;
static AromaNode *incoming_call_end_btn = NULL;
static bool call_overlay_visible = false;
static char current_call_name[128] = "";
static char current_call_number[64] = "";
static char current_call_path[256] = "";

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
    .bottom_bar_expanded = false,
    .ui_initialized = false,
    .first_media_check_done = false};

static int contact_page = 0;
static int total_pages = 0;
static AromaNode *prev_page_btn = NULL;
static AromaNode *next_page_btn = NULL;
static AromaNode *page_label = NULL;
static AromaNode *pagination_card = NULL;

typedef struct
{
    int x, y, w, h;
} LocalOffset;

static const LocalOffset maps_icon_offset = {30, 15, 48, 48};
static const LocalOffset phone_icon_offset = {100, 15, 48, 48};
static const LocalOffset music_icon_offset = {170, 15, 48, 48};

static void on_accept_call_click(void *user_data)
{
    (void)user_data;
    if (current_call_path[0] != '\0') {
        bt_hfp_answer(current_call_path);
    }
    if (incoming_call_overlay) {
        aroma_node_set_hidden(incoming_call_accept_btn, true);
        aroma_node_set_hidden(incoming_call_reject_btn, true);
        aroma_node_set_hidden(incoming_call_end_btn, false);
        if (incoming_call_name_label) {
            char display[256];
            snprintf(display, sizeof(display), "Active Call: %s", current_call_name);
            aroma_label_set_text(incoming_call_name_label, display);
        }
    }
    current_call_path[0] = '\0';
}

static void on_reject_call_click(void *user_data)
{
    (void)user_data;
    if (current_call_path[0] != '\0') {
        bt_hfp_hangup(current_call_path);
    }
    if (incoming_call_overlay) {
        aroma_node_set_hidden(incoming_call_overlay, true);
    }
    call_overlay_visible = false;
    current_call_path[0] = '\0';
}

static void on_end_call_click(void *user_data)
{
    (void)user_data;
    bt_hfp_hangup_all();
    if (incoming_call_overlay) {
        aroma_node_set_hidden(incoming_call_overlay, true);
    }
    call_overlay_visible = false;
    current_call_path[0] = '\0';
}

static void on_floating_dialer_click(void *user_data)
{
    (void)user_data;
    if (state.phone_app_tabs) {
        aroma_tabs_set_selected(state.phone_app_tabs, 1);
    }
}

void show_incoming_call_screen(const char *name, const char *number, const char *call_path)
{
    if (!incoming_call_overlay) return;
    
    safe_str_copy(current_call_name, name ? name : "Unknown", sizeof(current_call_name));
    safe_str_copy(current_call_number, number ? number : "", sizeof(current_call_number));
    safe_str_copy(current_call_path, call_path ? call_path : "", sizeof(current_call_path));
    
    if (incoming_call_name_label) {
        char display[256];
        snprintf(display, sizeof(display), "Incoming Call: %s", current_call_name);
        aroma_label_set_text(incoming_call_name_label, display);
    }
    if (incoming_call_number_label) {
        aroma_label_set_text(incoming_call_number_label, current_call_number);
    }
    
    aroma_node_set_hidden(incoming_call_accept_btn, false);
    aroma_node_set_hidden(incoming_call_reject_btn, false);
    aroma_node_set_hidden(incoming_call_end_btn, true);
    aroma_node_set_hidden(incoming_call_overlay, false);
    call_overlay_visible = true;
}

static void *call_monitor_thread_func(void *arg)
{
    (void)arg;
    
    usleep(5000000);
    
    bt_call_info_t prev_calls[10];
    int prev_count = 0;
    
    while (1) {
        usleep(1000000);
        
        bt_call_info_t curr_calls[10];
        int curr_count = bt_hfp_get_active_calls(curr_calls, 10);
        
        for (int i = 0; i < curr_count; i++) {
            bool is_new = true;
            for (int j = 0; j < prev_count; j++) {
                if (strcmp(curr_calls[i].path, prev_calls[j].path) == 0) {
                    is_new = false;
                    break;
                }
            }
            
            if (is_new && curr_calls[i].state == BT_CALL_STATE_INCOMING) {
                printf("[CALL MONITOR] Incoming call: %s (%s)\n", 
                       curr_calls[i].name, curr_calls[i].line_id);
                show_incoming_call_screen(curr_calls[i].name, 
                                         curr_calls[i].line_id, 
                                         curr_calls[i].path);
            }
        }
        
        if (curr_count == 0 && call_overlay_visible) {
            if (incoming_call_overlay) {
                aroma_node_set_hidden(incoming_call_overlay, true);
            }
            call_overlay_visible = false;
        }
        
        memcpy(prev_calls, curr_calls, sizeof(curr_calls));
        prev_count = curr_count;
    }
    
    return NULL;
}

static void on_dialer_delete_click_icon(void *user_data)
{
    (void)user_data;
    size_t len = strlen(dialer_number);
    if (len > 0) {
        dialer_number[len - 1] = '\0';
        if (dialer_display_label) {
            aroma_label_set_text(dialer_display_label, dialer_number[0] ? dialer_number : "Enter number");
        }
    }
}

static void on_dialer_call_click_icon(void *user_data)
{
    (void)user_data;
    if (dialer_number[0] != '\0') {
        printf("[PHONE UI] Dialing: %s\n", dialer_number);
        
        bt_device_info_t device = bt_speaker_get_device_info();
        if (device.connected) {
            int result = bt_hfp_dial(dialer_number);
            printf("[PHONE UI] Dial result: %d, error: %s\n", result, bt_hfp_get_last_error_message());
        }
        
        dialer_number[0] = '\0';
        if (dialer_display_label) {
            aroma_label_set_text(dialer_display_label, "Enter number");
        }
    }
}

static bool on_dialer_button_click(AromaNode *node, void *user_data)
{
    (void)node;
    const char *digit = (const char *)user_data;
    if (!digit || strlen(dialer_number) >= MAX_DIALER_DIGITS - 1) return true;
    
    strcat(dialer_number, digit);
    if (dialer_display_label) {
        aroma_label_set_text(dialer_display_label, dialer_number);
    }
    return true;
}

static void on_tab_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    
    if (pagination_card) {
        aroma_node_set_hidden(pagination_card, tab_index != 0);
    }
    
    if (dialer_card) {
        aroma_node_set_hidden(dialer_card, tab_index != 1);
    }
    
    if (state.contact_listview) {
        aroma_node_set_hidden(state.contact_listview, tab_index != 0);
    }
    
    if (tab_index == 0) {
        contact_page = 0;
        populate_contact_listview(state.contact_listview);
    }
}

static void restore_home_rect(AromaNode *node, const LocalOffset *offset)
{
    if (!node || !offset || !state.bottom_bar)
        return;

    AromaRect *bar_rect = aroma_node_get_rect(state.bottom_bar);
    AromaRect *node_rect = aroma_node_get_rect(node);
    if (!bar_rect || !node_rect)
        return;

    node_rect->x = bar_rect->x + offset->x;
    node_rect->y = bar_rect->y + offset->y;
    node_rect->width = offset->w;
    node_rect->height = offset->h;
}

static void restore_icon_and_card(AromaNode *icon, AromaNode *card, const LocalOffset *offset)
{
    restore_home_rect(icon, offset);
    restore_home_rect(card, offset);
}

static int compare_contacts(const void *a, const void *b)
{
    const ContactInfo *ca = (const ContactInfo *)a;
    const ContactInfo *cb = (const ContactInfo *)b;

    char name_a[128], name_b[128];
    safe_str_copy(name_a, ca->name, sizeof(name_a));
    safe_str_copy(name_b, cb->name, sizeof(name_b));

    if (name_a[0] == '\0')
    {
        safe_str_copy(name_a, ca->number, sizeof(name_a));
    }
    if (name_b[0] == '\0')
    {
        safe_str_copy(name_b, cb->number, sizeof(name_b));
    }

    return strcasecmp(name_a, name_b);
}

static char get_first_letter(const char *str)
{
    if (!str || !str[0])
        return '#';
    char c = toupper(str[0]);
    if (c >= 'A' && c <= 'Z')
        return c;
    return '#';
}

void populate_contact_listview(AromaNode *listview)
{
    if (!listview)
        return;

    pthread_mutex_lock(&contact_list_lock);
    
    aroma_listview_clear(listview);

    if (!state.contacts_fetched)
    {
        aroma_listview_add_item(listview, "Connecting to phone...", "", NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }

    if (state.contact_count == 0)
    {
        aroma_listview_add_item(listview, "No contacts found", "Connect a phone with PBAP", NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }

    ContactInfo *sorted_contacts = malloc(sizeof(ContactInfo) * state.contact_count);
    if (!sorted_contacts)
    {
        aroma_listview_add_item(listview, "Memory error", "", NULL);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    
    int *orig_indices = malloc(sizeof(int) * state.contact_count);
    for (int i = 0; i < state.contact_count; i++) {
        orig_indices[i] = i;
    }
    
    memcpy(sorted_contacts, state.contacts, sizeof(ContactInfo) * state.contact_count);
    
    for (int i = 0; i < state.contact_count - 1; i++) {
        for (int j = i + 1; j < state.contact_count; j++) {
            if (compare_contacts(&sorted_contacts[i], &sorted_contacts[j]) > 0) {
                ContactInfo temp = sorted_contacts[i];
                sorted_contacts[i] = sorted_contacts[j];
                sorted_contacts[j] = temp;
                int tempidx = orig_indices[i];
                orig_indices[i] = orig_indices[j];
                orig_indices[j] = tempidx;
            }
        }
    }

    total_pages = (state.contact_count + CONTACTS_PER_PAGE - 1) / CONTACTS_PER_PAGE;
    if (contact_page >= total_pages)
        contact_page = total_pages - 1;
    if (contact_page < 0)
        contact_page = 0;

    int start_idx = contact_page * CONTACTS_PER_PAGE;
    int end_idx = start_idx + CONTACTS_PER_PAGE;
    if (end_idx > state.contact_count)
        end_idx = state.contact_count;

    char current_header = 0;

    for (int i = start_idx; i < end_idx; i++)
    {
        char display_name[256];
        char display_number[64];

        sorted_to_original[i] = orig_indices[i];
        
        const char *name = sorted_contacts[i].name;
        const char *number = sorted_contacts[i].number;

        char letter = get_first_letter(name[0] ? name : number);

        if (letter != current_header)
        {
            current_header = letter;
            char header_text[4] = {letter, '\0'};
            aroma_listview_add_header(listview, header_text);
        }

        if (name[0] == '\0')
        {
            snprintf(display_name, sizeof(display_name), "%s", number);
            display_number[0] = '\0';
        }
        else
        {
            snprintf(display_name, sizeof(display_name), "%s", name);
            snprintf(display_number, sizeof(display_number), "%s", number);
        }

        aroma_listview_add_item(listview, display_name, display_number, NULL);
    }

    free(sorted_contacts);
    free(orig_indices);

    if (prev_page_btn)
    {
        aroma_node_set_hidden(prev_page_btn, contact_page == 0);
    }
    if (next_page_btn)
    {
        aroma_node_set_hidden(next_page_btn, contact_page >= total_pages - 1);
    }
    if (page_label)
    {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "%d/%d", contact_page + 1, total_pages);
        aroma_label_set_text(page_label, label_text);
    }

    if (pagination_card)
    {
        aroma_node_set_hidden(pagination_card, total_pages <= 1);
    }

    printf("[PHONE UI] Populated %d contacts (page %d/%d)\n", end_idx - start_idx, contact_page + 1, total_pages);
    
    pthread_mutex_unlock(&contact_list_lock);
}

static void on_prev_page_click(void *user_data)
{
    (void)user_data;
    if (contact_page > 0)
    {
        contact_page--;
        populate_contact_listview(state.contact_listview);
    }
}

static void on_next_page_click(void *user_data)
{
    (void)user_data;
    if (contact_page < total_pages - 1)
    {
        contact_page++;
        populate_contact_listview(state.contact_listview);
    }
}

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

static void apply_deferred_bottom_bar_position(void)
{
    if (!state.bottom_bar)
        return;

    AromaRect *rect = aroma_node_get_rect(state.bottom_bar);
    if (!rect)
        return;

    int target_x = media_ui.bottom_bar_expanded ? BOTTOM_BAR_X_EXPANDED : BOTTOM_BAR_X_COLLAPSED;
    if (rect->x == target_x)
        return;

    AromaAnimation *anim = aroma_animation_start(state.bottom_bar, AROMA_ANIM_SLIDE_X, rect->x, target_x, 1200);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static void set_bottom_bar_expanded(bool expanded)
{
    if (!state.bottom_bar)
        return;
    if (media_ui.bottom_bar_expanded == expanded)
        return;

    pthread_mutex_lock(&app_open_lock);
    bool app_open = bottom_bar_app_open;
    pthread_mutex_unlock(&app_open_lock);

    if (app_open)
    {
        media_ui.bottom_bar_expanded = expanded;
        return;
    }

    int from_x = media_ui.bottom_bar_expanded ? BOTTOM_BAR_X_EXPANDED : BOTTOM_BAR_X_COLLAPSED;
    int to_x = expanded ? BOTTOM_BAR_X_EXPANDED : BOTTOM_BAR_X_COLLAPSED;

    AromaAnimation *anim = aroma_animation_start(state.bottom_bar, AROMA_ANIM_SLIDE_X, from_x, to_x, 1200);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    media_ui.bottom_bar_expanded = expanded;
}

static bool is_any_app_open(void)
{
    pthread_mutex_lock(&app_open_lock);
    bool result = bottom_bar_app_open;
    pthread_mutex_unlock(&app_open_lock);
    return result;
}

static void set_app_open(bool open)
{
    pthread_mutex_lock(&app_open_lock);
    bottom_bar_app_open = open;
    pthread_mutex_unlock(&app_open_lock);
}

void update_media_card_display(void)
{
    if (!media_ui.ui_initialized || !media_ui.media_card)
        return;

    bt_media_info_t media = bt_speaker_get_media_info();
    bt_state_t current_state = bt_speaker_get_state();

    bool is_connected = (current_state == BT_STATE_CONNECTED ||
                         current_state == BT_STATE_PLAYING);
    bool has_media = (media.title[0] != '\0' || media.artist[0] != '\0');

    if (!has_media || !is_connected)
    {
        aroma_node_set_hidden(media_ui.media_card, true);
        media_ui.first_media_check_done = false;
        set_bottom_bar_expanded(false);
        return;
    }

    if (!media_ui.first_media_check_done)
    {
        media_ui.first_media_check_done = true;
        set_bottom_bar_expanded(true);
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

static void *media_monitor_thread_func(void *arg)
{
    (void)arg;
    
    usleep(3000000);

    while (media_ui.ui_initialized)
    {
        update_media_card_display();
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

static AromaRect maps_icon_anim_start;

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

    int start_x = maps_icon_anim_start.x;
    int start_y = maps_icon_anim_start.y;
    int start_w = maps_icon_anim_start.width;
    int start_h = maps_icon_anim_start.height;

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

    AromaRect *icon_rect = aroma_node_get_rect(state.maps_app_icon);
    if (icon_rect)
        maps_icon_anim_start = *icon_rect;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, opening_anim, NULL);
    if (!anim)
        return false;

    set_app_open(true);

    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);

    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    aroma_node_set_hidden(state.map_node, false);
    aroma_node_set_hidden(state.map_close_btn, false);

    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    aroma_node_set_z_index(state.maps_app_icon, Z_LAYER_STATUS_BAR + 10);

    return true;
}

static AromaRect maps_icon_anim_end;

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

    int end_x = maps_icon_anim_end.x;
    int end_y = maps_icon_anim_end.y;
    int end_w = maps_icon_anim_end.width;
    int end_h = maps_icon_anim_end.height;

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

    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        restore_icon_and_card(state.maps_app_icon, target, &maps_icon_offset);
        update_media_card_display();
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

    AromaRect *bar_rect = aroma_node_get_rect(state.bottom_bar);
    if (bar_rect)
    {
        maps_icon_anim_end.x = bar_rect->x + maps_icon_offset.x;
        maps_icon_anim_end.y = bar_rect->y + maps_icon_offset.y;
        maps_icon_anim_end.width = maps_icon_offset.w;
        maps_icon_anim_end.height = maps_icon_offset.h;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, closing_anim, NULL);

    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);

    aroma_node_set_hidden(state.map_recently_viewed_card, true);
}

static AromaRect phone_icon_anim_start;

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

    int start_x = phone_icon_anim_start.x;
    int start_y = phone_icon_anim_start.y;
    int start_w = phone_icon_anim_start.width;
    int start_h = phone_icon_anim_start.height;

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
    phone_tabs_rect->y = rect->y;
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

    AromaRect *icon_rect = aroma_node_get_rect(state.phone_app_icon);
    if (icon_rect)
        phone_icon_anim_start = *icon_rect;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, phone_opening_anim, NULL);
    if (!anim)
        return false;

    set_app_open(true);

    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);

    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    aroma_node_set_hidden(state.phone_node, false);
    aroma_node_set_hidden(state.phone_close_btn, false);
    aroma_node_set_hidden(state.phone_app_tabs, false);
    aroma_node_set_hidden(state.contact_listview, false);
    if (pagination_card)
        aroma_node_set_hidden(pagination_card, false);
    if (dialer_card)
        aroma_node_set_hidden(dialer_card, true);

    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    aroma_node_set_z_index(state.phone_app_icon, Z_LAYER_STATUS_BAR + 10);

    contact_page = 0;
    populate_contact_listview(state.contact_listview);

    return true;
}

static AromaRect phone_icon_anim_end;

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

    int end_x = phone_icon_anim_end.x;
    int end_y = phone_icon_anim_end.y;
    int end_w = phone_icon_anim_end.width;
    int end_h = phone_icon_anim_end.height;

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

    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        restore_icon_and_card(state.phone_app_icon, target, &phone_icon_offset);
        update_media_card_display();
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

    AromaRect *bar_rect = aroma_node_get_rect(state.bottom_bar);
    if (bar_rect)
    {
        phone_icon_anim_end.x = bar_rect->x + phone_icon_offset.x;
        phone_icon_anim_end.y = bar_rect->y + phone_icon_offset.y;
        phone_icon_anim_end.width = phone_icon_offset.w;
        phone_icon_anim_end.height = phone_icon_offset.h;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, phone_closing_anim, NULL);
    aroma_node_set_hidden(state.phone_app_tabs, true);
    aroma_node_set_hidden(state.contact_listview, true);
    if (pagination_card)
        aroma_node_set_hidden(pagination_card, true);
    if (dialer_card)
        aroma_node_set_hidden(dialer_card, true);

    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
}

static AromaRect music_icon_anim_start;

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

    int start_x = music_icon_anim_start.x;
    int start_y = music_icon_anim_start.y;
    int start_w = music_icon_anim_start.width;
    int start_h = music_icon_anim_start.height;

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

    AromaRect *icon_rect = aroma_node_get_rect(state.music_app_icon);
    if (icon_rect)
        music_icon_anim_start = *icon_rect;

    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 600, music_opening_anim, NULL);
    if (!anim)
        return false;

    set_app_open(true);

    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);

    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);

    aroma_node_set_hidden(state.music_node, false);
    aroma_node_set_hidden(state.music_close_btn, false);

    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    aroma_node_set_z_index(state.music_app_icon, Z_LAYER_STATUS_BAR + 10);

    return true;
}

static AromaRect music_icon_anim_end;

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

    int end_x = music_icon_anim_end.x;
    int end_y = music_icon_anim_end.y;
    int end_w = music_icon_anim_end.width;
    int end_h = music_icon_anim_end.height;

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

    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        restore_icon_and_card(state.music_app_icon, target, &music_icon_offset);
        update_media_card_display();
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

    AromaRect *bar_rect = aroma_node_get_rect(state.bottom_bar);
    if (bar_rect)
    {
        music_icon_anim_end.x = bar_rect->x + music_icon_offset.x;
        music_icon_anim_end.y = bar_rect->y + music_icon_offset.y;
        music_icon_anim_end.width = music_icon_offset.w;
        music_icon_anim_end.height = music_icon_offset.h;
    }

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

static void on_contact_click(int index, void *user_data)
{
    (void)user_data;
    int start_idx = contact_page * CONTACTS_PER_PAGE;
    int actual_index = start_idx + index;
    
    if (actual_index >= 0 && actual_index < state.contact_count)
    {
        int orig_idx = sorted_to_original[actual_index];
        if (orig_idx >= 0 && orig_idx < state.contact_count)
        {
            char number[64];
            safe_str_copy(number, state.contacts[orig_idx].number, sizeof(number));

            if (number[0] != '\0')
            {
                printf("[PHONE UI] Dialing from contacts: %s (%s)\n", 
                       state.contacts[orig_idx].name, number);
                
                bt_device_info_t device = bt_speaker_get_device_info();
                if (device.connected) {
                    bt_hfp_dial(number);
                }
            }
        }
    }
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

    state.bottom_bar = aroma_ui_card(state.vehicle_view_root, BOTTOM_BAR_X_COLLAPSED, WIN_H - 110, 420, 80, CARD_TYPE_GLASS);
    aroma_card_set_colors(state.bottom_bar, 0x80FFFFFF, 0x80FFFFFF);
    aroma_node_set_z_index(state.bottom_bar, Z_LAYER_VEHICLE_OVERLAYS + 2);
    media_ui.bottom_bar_expanded = false;

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
    aroma_node_set_z_index(state.phone_close_btn, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(state.phone_close_btn, true);

    state.contact_listview = aroma_ui_listview(
        phone_app_icon_card, 16, 110, 988, 380,
        on_contact_click, NULL, state.ui_font);

    aroma_node_set_z_index(state.contact_listview, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(state.contact_listview, true);
    dialer_card = aroma_ui_card(phone_app_icon_card, 16, 110, 988, 380, CARD_TYPE_GLASS);
    aroma_node_set_z_index(dialer_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(dialer_card, true);

    dialer_display_label = aroma_ui_label(dialer_card, "Enter number", 
        (988 - 400)/2, 20, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(dialer_display_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(dialer_display_label, 0xFFFFFFFF);

    AromaNode *dialer_grid = aroma_ui_container(
        dialer_card, (988 - 280)/2, 70, 280, 290,
        AROMA_LAYOUT_MODE_GRID, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(dialer_grid, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_grid_cols(dialer_grid, 3);
    aroma_node_set_grid_rows(dialer_grid, 5);
    aroma_node_set_gap(dialer_grid, 12);

    const int btn_size = 72;

    const char *dialer_digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    const char *dialer_icons[] = {
        NULL, NULL, NULL,
        NULL, NULL, NULL,
        NULL, NULL, NULL,
        NULL, NULL, NULL
    };

    for (int i = 0; i < 12; i++) {
        AromaNode *btn = aroma_ui_iconbutton(
            dialer_grid, dialer_digits[i],
            0, 0, btn_size, ICON_BUTTON_FILLED,
            (void (*)(void *))on_dialer_button_click,
            (void *)dialer_digits[i], state.settings_font);
        aroma_node_set_z_index(btn, Z_LAYER_STATUS_BAR + 14);
        aroma_iconbutton_set_colors(btn, 0xFF424242, 0xFFFFFFFF);
    }

    AromaNode *del_btn = aroma_ui_iconbutton(
        dialer_grid, AROMA_ICON_BACKSPACE,
        0, 0, btn_size, ICON_BUTTON_FILLED,
        on_dialer_delete_click_icon, NULL, state.icon_font);
    aroma_node_set_z_index(del_btn, Z_LAYER_STATUS_BAR + 14);
    aroma_iconbutton_set_colors(del_btn, 0xFF424242, 0xFFFFFFFF);

    AromaNode *call_btn = aroma_ui_iconbutton(
        dialer_grid, AROMA_ICON_CALL,
        0, 0, btn_size, ICON_BUTTON_FILLED,
        on_dialer_call_click_icon, NULL, state.icon_font);
    aroma_node_set_z_index(call_btn, Z_LAYER_STATUS_BAR + 14);
    aroma_iconbutton_set_colors(call_btn, 0xFF4CAF50, 0xFFFFFFFF);
    AromaNode *floating_dialer_btn = aroma_ui_iconbutton(
        phone_app_icon_card, AROMA_ICON_DIALER_SIP,
        910, 480, 56, ICON_BUTTON_FILLED,
        on_floating_dialer_click, NULL, state.icon_font);
    aroma_node_set_z_index(floating_dialer_btn, Z_LAYER_STATUS_BAR + 16);
    aroma_iconbutton_set_colors(floating_dialer_btn, 0xFF2196F3, 0xFFFFFFFF);

    pagination_card = aroma_ui_card(phone_app_icon_card, 16, 500, 400, 50, CARD_TYPE_GLASS);
    aroma_node_set_z_index(pagination_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(pagination_card, true);

    prev_page_btn = aroma_ui_iconbutton(
        pagination_card, AROMA_ICON_ARROW_BACK,
        16, 8, 34, ICON_BUTTON_OUTLINED,
        on_prev_page_click, NULL, state.icon_font);
    aroma_node_set_z_index(prev_page_btn, Z_LAYER_STATUS_BAR + 13);

    page_label = aroma_ui_label(
        pagination_card, "1/1",
        70, 12, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(page_label, Z_LAYER_STATUS_BAR + 13);

    next_page_btn = aroma_ui_iconbutton(
        pagination_card, AROMA_ICON_ARROW_FORWARD,
        130, 8, 34, ICON_BUTTON_OUTLINED,
        on_next_page_click, NULL, state.icon_font);
    aroma_node_set_z_index(next_page_btn, Z_LAYER_STATUS_BAR + 13);

    state.phone_app_tabs = aroma_ui_tabs_with_icons(
        phone_app_icon_card, 0, 0, 1024, 50,
        (const char *[]){"Contacts", "Dialer"},
        (const char *[]){AROMA_ICON_CONTACTS, AROMA_ICON_DIALER_SIP},
        2, on_tab_changed, NULL, state.settings_font, state.big_icon_font);

    aroma_node_set_z_index(state.phone_app_tabs, Z_LAYER_STATUS_BAR + 15);
    aroma_node_set_hidden(state.phone_app_tabs, true);

    AromaNode *content[] = {state.contact_listview, dialer_card};
    aroma_tabs_set_content(state.phone_app_tabs, 0, content, 2);

    incoming_call_overlay = aroma_ui_card(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_FILLED);
    aroma_node_set_z_index(incoming_call_overlay, Z_LAYER_VOICE_CARD);
    aroma_card_set_colors(incoming_call_overlay, 0xDD000000, 0xDD000000);
    aroma_node_set_hidden(incoming_call_overlay, true);
    
    incoming_call_name_label = aroma_ui_label(
        incoming_call_overlay, "Incoming Call", 
        WIN_W/2 - 200, 150, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(incoming_call_name_label, Z_LAYER_VOICE_CONTENT);
    aroma_label_set_color(incoming_call_name_label, 0xFFFFFFFF);
    
    incoming_call_number_label = aroma_ui_label(
        incoming_call_overlay, "", 
        WIN_W/2 - 150, 220, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(incoming_call_number_label, Z_LAYER_VOICE_CONTENT);
    aroma_label_set_color(incoming_call_number_label, 0xFFAAAAAA);
    
    incoming_call_accept_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL,
        WIN_W/2 - 120, 320, 80, ICON_BUTTON_FILLED,
        on_accept_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_accept_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_accept_btn, 0xFF4CAF50, 0xFFFFFFFF);
    
    incoming_call_reject_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL_END,
        WIN_W/2 + 40, 320, 80, ICON_BUTTON_FILLED,
        on_reject_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_reject_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_reject_btn, 0xFFF44336, 0xFFFFFFFF);
    
    incoming_call_end_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL_END,
        WIN_W/2 - 40, 320, 80, ICON_BUTTON_FILLED,
        on_end_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_end_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_end_btn, 0xFFF44336, 0xFFFFFFFF);
    aroma_node_set_hidden(incoming_call_end_btn, true);

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

    media_ui.ui_initialized = true;
    
    pthread_t media_thread;
    pthread_attr_t media_attr;
    pthread_attr_init(&media_attr);
    pthread_attr_setdetachstate(&media_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&media_thread, &media_attr, media_monitor_thread_func, NULL);
    pthread_attr_destroy(&media_attr);
    
    pthread_t call_thread;
    pthread_attr_t call_attr;
    pthread_attr_init(&call_attr);
    pthread_attr_setdetachstate(&call_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&call_thread, &call_attr, call_monitor_thread_func, NULL);
    pthread_attr_destroy(&call_attr);
}