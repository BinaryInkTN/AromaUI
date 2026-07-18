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
#include <math.h>

void populate_contact_listview(AromaNode *listview);
static void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon);
static void show_minimap(void);
static void hide_minimap(void);
static void update_minimap_display(void);
bool open_maps(AromaNode *node, void *user_data);
#define MEDIA_UPDATE_INTERVAL_US 500000
#define CONTACTS_PER_PAGE 7
#define MAX_DIALER_DIGITS 32

#define BOTTOM_BAR_X_COLLAPSED 302
#define BOTTOM_BAR_X_EXPANDED 512

#define EARTH_RADIUS 6371.0

#define GMAPS_COLOR_PRIMARY 0xFF1A73E8
#define GMAPS_COLOR_PRIMARY_DARK 0xFF1967D2
#define GMAPS_COLOR_SURFACE 0xFFFFFFFF
#define GMAPS_COLOR_SURFACE_VARIANT 0xFFF1F3F4
#define GMAPS_COLOR_ON_SURFACE 0xFF202124
#define GMAPS_COLOR_ON_SURFACE_VARIANT 0xFF5F6368
#define GMAPS_COLOR_OUTLINE 0xFFDADCE0
#define GMAPS_COLOR_DESTINATION 0xFFEA4335
#define GMAPS_COLOR_START 0xFF34A853
#define GMAPS_COLOR_SCRIM 0xDD000000

static AromaNode *minimap_card = NULL;
static AromaNode *minimap_node = NULL;
static AromaNode *minimap_close_btn = NULL;
static AromaNode *minimap_eta_label = NULL;
static AromaNode *minimap_distance_label = NULL;
static AromaNode *minimap_restore_btn = NULL;
static bool minimap_visible = false;

#define MINIMAP_WIDTH 280
#define MINIMAP_HEIGHT 220
#define MINIMAP_X (WIN_W - MINIMAP_WIDTH - 20)
#define MINIMAP_Y (WIN_H - MINIMAP_HEIGHT - 130)
#define MINIMAP_Z_INDEX (Z_LAYER_VOICE_CARD + 100)

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

static struct
{
    bool active;
    double from_lat, from_lon;
    double to_lat, to_lon;
    char from_text[256];
    char to_text[256];
    double distance_km;
    int eta_minutes;
} map_nav = {0};

static AromaNode *map_search_surface = NULL;
static AromaNode *map_search_placeholder_label = NULL;
static AromaNode *map_search_back_btn = NULL;
static bool map_search_expanded = false;

static AromaNode *map_from_entry = NULL;
static AromaNode *map_to_entry = NULL;
static AromaNode *map_swap_btn = NULL;
static AromaNode *map_go_btn = NULL;

static AromaNode *map_route_sheet = NULL;
static AromaNode *map_distance_label = NULL;
static AromaNode *map_time_label = NULL;
static AromaNode *map_route_dest_label = NULL;
static AromaNode *map_end_nav_btn = NULL;

static AromaNode *map_search_results_list = NULL;
static GeocodeResult map_geocode_results[MAX_GEOCODE_RESULTS];
static int map_geocode_result_count = 0;
static char last_search_query[256] = "";
static pthread_mutex_t search_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool search_results_visible = false;
static int focused_entry = 0;

#define SEARCH_DEBOUNCE_US 350000
static pthread_mutex_t debounce_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long search_generation = 0;
static char pending_search_query[256] = "";
static int pending_search_focused_entry = 0;

typedef struct
{
    int x, y, w, h;
} LocalOffset;

static const LocalOffset maps_icon_offset = {30, 15, 48, 48};
static const LocalOffset phone_icon_offset = {100, 15, 48, 48};
static const LocalOffset music_icon_offset = {170, 15, 48, 48};

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

static double calculate_distance_km(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                   sin(dlon / 2) * sin(dlon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS * c;
}

static int estimate_eta_minutes(double distance_km)
{
    return (int)((distance_km / 50.0) * 60.0) + 1;
}

static void format_time_string(int minutes, char *buf, size_t size)
{
    if (minutes < 60)
    {
        snprintf(buf, size, "%d min", minutes);
    }
    else
    {
        int hours = minutes / 60;
        int mins = minutes % 60;
        snprintf(buf, size, "%d hr %d min", hours, mins);
    }
}

static void format_distance_string(double km, char *buf, size_t size)
{
    if (km < 1.0)
    {
        snprintf(buf, size, "%d m", (int)(km * 1000));
    }
    else
    {
        snprintf(buf, size, "%.1f km", km);
    }
}

static void truncate_for_listview(const char *input, char *output, size_t output_size)
{
    if (!input || output_size == 0)
    {
        if (output_size > 0)
            output[0] = '\0';
        return;
    }

    size_t len = strlen(input);
    const size_t ellipsis_len = 3;
    if (len > output_size - 1 && output_size > ellipsis_len + 1)
    {
        size_t cut = output_size - 1 - ellipsis_len;
        memcpy(output, input, cut);
        memcpy(output + cut, "...", ellipsis_len);
        output[cut + ellipsis_len] = '\0';
    }
    else
    {
        strncpy(output, input, output_size - 1);
        output[output_size - 1] = '\0';
    }
}

static void filter_english_only(const char *input, char *output, size_t output_size)
{
    if (!input || !output)
    {
        if (output)
            output[0] = '\0';
        return;
    }

    size_t out_pos = 0;
    size_t in_pos = 0;
    size_t input_len = strlen(input);
    bool has_any_char = false;

    while (in_pos < input_len && out_pos < output_size - 1)
    {
        unsigned char c = input[in_pos];

        if (c < 0x80)
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
                c == ',' || c == '-' || c == '\'' || c == '&' ||
                c == '(' || c == ')' || c == '!' || c == '?')
            {
                output[out_pos++] = c;
                has_any_char = true;
            }
            else if (c == '\n' || c == '\r' || c == '\t')
            {
            }
            else
            {
                output[out_pos++] = ' ';
            }
            in_pos++;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            in_pos += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            in_pos += 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            in_pos += 4;
        }
        else
        {
            in_pos++;
        }
    }
    output[out_pos] = '\0';

    if (!has_any_char)
    {
        strcpy(output, "Unnamed");
        return;
    }

    if (out_pos > 0)
    {
        char *write_ptr = output;
        char *read_ptr = output;
        bool space_seen = false;

        while (*read_ptr == ' ')
            read_ptr++;

        while (*read_ptr)
        {
            if (*read_ptr == ' ')
            {
                if (!space_seen)
                {
                    *write_ptr++ = *read_ptr;
                    space_seen = true;
                }
            }
            else
            {
                *write_ptr++ = *read_ptr;
                space_seen = false;
            }
            read_ptr++;
        }
        *write_ptr = '\0';

        if (write_ptr > output && *(write_ptr - 1) == ' ')
        {
            *(write_ptr - 1) = '\0';
        }

        if (output[0] == '\0')
        {
            strcpy(output, "Unnamed");
        }
    }
}

static void on_geocode_results(GeocodeResult *results, int count, void *user_data)
{
    (void)user_data;

    pthread_mutex_lock(&search_mutex);

    map_geocode_result_count = count;
    if (count > MAX_GEOCODE_RESULTS)
        count = MAX_GEOCODE_RESULTS;

    for (int i = 0; i < count && i < MAX_GEOCODE_RESULTS; i++)
    {
        map_geocode_results[i] = results[i];
    }

    if (map_search_results_list)
    {
        aroma_listview_clear(map_search_results_list);

        if (count == 0)
        {
            aroma_listview_add_item_with_icon(map_search_results_list,
                                              "No results found", "Try a different search term",
                                              AROMA_ICON_SEARCH, NULL);
        }
        else
        {
            for (int i = 0; i < count; i++)
            {
                char display_name[256];
                char subtitle[256];

                truncate_for_listview(results[i].display_name, display_name, sizeof(display_name));

                if (results[i].category[0])
                {
                    snprintf(subtitle, sizeof(subtitle), "%s - %s",
                             results[i].category,
                             results[i].type[0] ? results[i].type : "place");
                }
                else
                {
                    snprintf(subtitle, sizeof(subtitle), "%s",
                             results[i].type[0] ? results[i].type : "place");
                }

                aroma_listview_add_item_with_icon(map_search_results_list,
                                                  display_name, subtitle,
                                                  AROMA_ICON_PLACE, (void *)(intptr_t)i);
            }
        }

        aroma_node_set_hidden(map_search_results_list, false);
        search_results_visible = true;
    }

    pthread_mutex_unlock(&search_mutex);
}

static void perform_map_search(const char *query)
{
    if (!query || !query[0] || strlen(query) < 2)
    {
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
        return;
    }

    if (strcmp(query, last_search_query) == 0)
    {
        return;
    }

    strncpy(last_search_query, query, sizeof(last_search_query) - 1);
    last_search_query[sizeof(last_search_query) - 1] = '\0';

    aroma_map_geocode_search(state.map_node, query, on_geocode_results, NULL);
}

typedef struct
{
    unsigned long generation;
    char query[256];
    int focused_entry;
} DebouncedSearchArgs;

static void *debounced_search_thread_func(void *arg)
{
    DebouncedSearchArgs *args = (DebouncedSearchArgs *)arg;
    usleep(SEARCH_DEBOUNCE_US);
    pthread_mutex_lock(&debounce_mutex);
    bool still_current = (args->generation == search_generation);
    pthread_mutex_unlock(&debounce_mutex);
    if (still_current)
    {
        focused_entry = args->focused_entry;
        perform_map_search(args->query);
    }
    free(args);
    return NULL;
}

static void perform_map_search_debounced(const char *query, int entry)
{
    if (!query)
        query = "";
    pthread_mutex_lock(&debounce_mutex);
    search_generation++;
    unsigned long my_generation = search_generation;
    snprintf(pending_search_query, sizeof(pending_search_query), "%s", query);
    pending_search_focused_entry = entry;
    pthread_mutex_unlock(&debounce_mutex);
    if (!query[0] || strlen(query) < 2)
    {
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
        return;
    }
    DebouncedSearchArgs *args = malloc(sizeof(DebouncedSearchArgs));
    if (!args)
    {
        focused_entry = entry;
        perform_map_search(query);
        return;
    }
    args->generation = my_generation;
    snprintf(args->query, sizeof(args->query), "%s", query);
    args->focused_entry = entry;
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, debounced_search_thread_func, args) != 0)
    {
        free(args);
        focused_entry = entry;
        perform_map_search(query);
    }
    pthread_attr_destroy(&attr);
}

static bool on_map_from_entry_change(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    perform_map_search_debounced(text, 0);
    return true;
}

static bool on_map_to_entry_change(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    perform_map_search_debounced(text, 1);
    return true;
}

static void on_search_result_click(int index, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&search_mutex);
    if (index >= 0 && index < map_geocode_result_count)
    {
        const GeocodeResult *result = &map_geocode_results[index];
        char display_name[256];
        truncate_for_listview(result->display_name, display_name, sizeof(display_name));
        if (focused_entry == 1)
        {
            aroma_textbox_set_text(map_to_entry, display_name);
            map_nav.to_lat = result->lat;
            map_nav.to_lon = result->lon;
            strncpy(map_nav.to_text, result->display_name, sizeof(map_nav.to_text) - 1);
            map_nav.to_text[sizeof(map_nav.to_text) - 1] = '\0';
        }
        else
        {
            aroma_textbox_set_text(map_from_entry, display_name);
            map_nav.from_lat = result->lat;
            map_nav.from_lon = result->lon;
            strncpy(map_nav.from_text, result->display_name, sizeof(map_nav.from_text) - 1);
            map_nav.from_text[sizeof(map_nav.from_text) - 1] = '\0';
        }
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
    }
    pthread_mutex_unlock(&search_mutex);
}

static void show_route_panel(void)
{
    if (map_route_sheet)
    {
        aroma_node_set_hidden(map_route_sheet, false);
        aroma_node_set_hidden(map_end_nav_btn, false);
    }
    if (map_search_surface)
    {
        aroma_node_set_hidden(map_search_surface, true);
        map_search_expanded = false;
    }
    if (map_search_results_list)
    {
        aroma_node_set_hidden(map_search_results_list, true);
        search_results_visible = false;
    }
}

static void hide_route_panel(void)
{
    if (map_route_sheet)
    {
        aroma_node_set_hidden(map_route_sheet, true);
    }
    if (map_end_nav_btn)
    {
        aroma_node_set_hidden(map_end_nav_btn, true);
    }
    if (map_search_surface && !map_nav.active)
    {
        aroma_node_set_hidden(map_search_surface, false);
        map_search_expanded = true;
    }
}

static void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon)
{
    map_nav.active = true;
    map_nav.from_lat = from_lat;
    map_nav.from_lon = from_lon;
    map_nav.to_lat = to_lat;
    map_nav.to_lon = to_lon;
    map_nav.distance_km = calculate_distance_km(from_lat, from_lon, to_lat, to_lon);
    map_nav.eta_minutes = estimate_eta_minutes(map_nav.distance_km);

    aroma_map_clear_markers(state.map_node);
    aroma_map_clear_route(state.map_node);

    aroma_map_add_popup_marker(state.map_node, from_lat, from_lon, GMAPS_COLOR_START, "Start");
    aroma_map_add_popup_marker(state.map_node, to_lat, to_lon, GMAPS_COLOR_DESTINATION, "Destination");
    aroma_map_set_route(state.map_node, from_lat, from_lon, to_lat, to_lon, GMAPS_COLOR_PRIMARY);

    double center_lat = (from_lat + to_lat) / 2.0;
    double center_lon = (from_lon + to_lon) / 2.0;
    aroma_map_set_center(state.map_node, center_lat, center_lon);
    double center_zoom = 15.0 - log2(map_nav.distance_km + 1.0);
    if (center_zoom < 5.0)
        center_zoom = 5.0;
    if (center_zoom > 18.0)
        center_zoom = 18.0;
    aroma_map_set_zoom(state.map_node, center_zoom);

    if (minimap_visible && minimap_node)
    {
        aroma_map_clear_markers(minimap_node);
        aroma_map_clear_route(minimap_node);
        aroma_map_add_popup_marker(minimap_node, from_lat, from_lon, GMAPS_COLOR_START, "S");
        aroma_map_add_popup_marker(minimap_node, to_lat, to_lon, GMAPS_COLOR_DESTINATION, "D");
        aroma_map_set_route(minimap_node, from_lat, from_lon, to_lat, to_lon, GMAPS_COLOR_PRIMARY);
        aroma_map_set_center(minimap_node, center_lat, center_lon);
        aroma_map_set_zoom(minimap_node, center_zoom - 2.0);
        update_minimap_display();
    }

    if (map_route_sheet)
    {
        char dist_str[32], time_str[32];
        format_distance_string(map_nav.distance_km, dist_str, sizeof(dist_str));
        format_time_string(map_nav.eta_minutes, time_str, sizeof(time_str));

        if (map_distance_label)
            aroma_label_set_text(map_distance_label, dist_str);
        if (map_time_label)
            aroma_label_set_text(map_time_label, time_str);
        if (map_route_dest_label)
        {
            char dest_display[288];
            char truncated_dest[40];
            truncate_for_listview(map_nav.to_text, truncated_dest, sizeof(truncated_dest));
            snprintf(dest_display, sizeof(dest_display), "To %s",
                     map_nav.to_text[0] ? truncated_dest : "destination");
            aroma_label_set_text(map_route_dest_label, dest_display);
        }

        show_route_panel();
    }
}

static void clear_navigation(void)
{
    map_nav.active = false;
    aroma_map_clear_route(state.map_node);
    aroma_map_clear_markers(state.map_node);
    hide_route_panel();
    aroma_map_set_center(state.map_node, 37.7749, -122.4194);
    aroma_map_set_zoom(state.map_node, 15);

    if (minimap_node)
    {
        aroma_map_clear_route(minimap_node);
        aroma_map_clear_markers(minimap_node);
    }
    hide_minimap();
}

static void on_swap_click(void *user_data)
{
    (void)user_data;
    const char *from_text = aroma_textbox_get_text(map_from_entry);
    const char *to_text = aroma_textbox_get_text(map_to_entry);
    char temp_from[256], temp_to[256];
    strncpy(temp_from, from_text ? from_text : "", sizeof(temp_from) - 1);
    temp_from[sizeof(temp_from) - 1] = '\0';
    strncpy(temp_to, to_text ? to_text : "", sizeof(temp_to) - 1);
    temp_to[sizeof(temp_to) - 1] = '\0';
    aroma_textbox_set_text(map_from_entry, temp_to);
    aroma_textbox_set_text(map_to_entry, temp_from);
    double temp_lat = map_nav.from_lat;
    double temp_lon = map_nav.from_lon;
    char temp_text[256];
    strncpy(temp_text, map_nav.from_text, sizeof(temp_text) - 1);
    temp_text[sizeof(temp_text) - 1] = '\0';
    map_nav.from_lat = map_nav.to_lat;
    map_nav.from_lon = map_nav.to_lon;
    strncpy(map_nav.from_text, map_nav.to_text, sizeof(map_nav.from_text) - 1);
    map_nav.from_text[sizeof(map_nav.from_text) - 1] = '\0';
    map_nav.to_lat = temp_lat;
    map_nav.to_lon = temp_lon;
    strncpy(map_nav.to_text, temp_text, sizeof(map_nav.to_text) - 1);
    map_nav.to_text[sizeof(map_nav.to_text) - 1] = '\0';
    if (map_nav.active)
    {
        start_navigation(map_nav.from_lat, map_nav.from_lon,
                         map_nav.to_lat, map_nav.to_lon);
    }
}

static bool on_go_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    const char *from_text = aroma_textbox_get_text(map_from_entry);
    const char *to_text = aroma_textbox_get_text(map_to_entry);
    if (map_nav.from_text[0] == '\0')
    {
        map_nav.from_lat = 37.7749;
        map_nav.from_lon = -122.4194;
        snprintf(map_nav.from_text, sizeof(map_nav.from_text), "%s",
                 (from_text && from_text[0]) ? from_text : "Current location");
    }
    if (map_nav.to_text[0] == '\0' || !to_text || !to_text[0])
    {
        if (map_search_placeholder_label)
        {
            aroma_label_set_text(map_search_placeholder_label,
                                 "Pick a destination first");
        }
        return true;
    }
    start_navigation(map_nav.from_lat, map_nav.from_lon,
                     map_nav.to_lat, map_nav.to_lon);
    return true;
}

static void on_end_nav_click(void *user_data)
{
    (void)user_data;
    clear_navigation();
}

static void on_search_pill_click(void *user_data)
{
    (void)user_data;
    map_search_expanded = !map_search_expanded;
    if (map_search_surface)
    {
        aroma_node_set_hidden(map_search_surface, !map_search_expanded);
    }
}

static void on_search_back_click(void *user_data)
{
    (void)user_data;
    map_search_expanded = false;
    if (map_search_surface)
    {
        aroma_node_set_hidden(map_search_surface, true);
    }
    if (map_search_results_list)
    {
        aroma_node_set_hidden(map_search_results_list, true);
        search_results_visible = false;
    }
}

static void on_accept_call_click(void *user_data)
{
    (void)user_data;
    if (current_call_path[0] != '\0')
    {
        bt_hfp_answer(current_call_path);
    }
    if (incoming_call_overlay)
    {
        aroma_node_set_hidden(incoming_call_accept_btn, true);
        aroma_node_set_hidden(incoming_call_reject_btn, true);
        aroma_node_set_hidden(incoming_call_end_btn, false);
        if (incoming_call_name_label)
        {
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
    if (current_call_path[0] != '\0')
    {
        bt_hfp_hangup(current_call_path);
    }
    if (incoming_call_overlay)
    {
        aroma_node_set_hidden(incoming_call_overlay, true);
    }
    call_overlay_visible = false;
    current_call_path[0] = '\0';
}

static void on_end_call_click(void *user_data)
{
    (void)user_data;
    bt_hfp_hangup_all();
    if (incoming_call_overlay)
    {
        aroma_node_set_hidden(incoming_call_overlay, true);
    }
    call_overlay_visible = false;
    current_call_path[0] = '\0';
}

static void on_floating_dialer_click(void *user_data)
{
    (void)user_data;
    if (state.phone_app_tabs)
    {
        aroma_tabs_set_selected(state.phone_app_tabs, 1);
    }
}

void show_incoming_call_screen(const char *name, const char *number, const char *call_path)
{
    if (!incoming_call_overlay)
        return;
    safe_str_copy(current_call_name, name ? name : "Unknown", sizeof(current_call_name));
    safe_str_copy(current_call_number, number ? number : "", sizeof(current_call_number));
    safe_str_copy(current_call_path, call_path ? call_path : "", sizeof(current_call_path));
    if (incoming_call_name_label)
    {
        char display[256];
        snprintf(display, sizeof(display), "Incoming Call: %s", current_call_name);
        aroma_label_set_text(incoming_call_name_label, display);
    }
    if (incoming_call_number_label)
    {
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
    while (1)
    {
        usleep(1000000);
        bt_call_info_t curr_calls[10];
        int curr_count = bt_hfp_get_active_calls(curr_calls, 10);
        for (int i = 0; i < curr_count; i++)
        {
            bool is_new = true;
            for (int j = 0; j < prev_count; j++)
            {
                if (strcmp(curr_calls[i].path, prev_calls[j].path) == 0)
                {
                    is_new = false;
                    break;
                }
            }
            if (is_new && curr_calls[i].state == BT_CALL_STATE_INCOMING)
            {
                show_incoming_call_screen(curr_calls[i].name,
                                          curr_calls[i].line_id,
                                          curr_calls[i].path);
            }
        }
        if (curr_count == 0 && call_overlay_visible)
        {
            if (incoming_call_overlay)
            {
                aroma_node_set_hidden(incoming_call_overlay, true);
            }
            call_overlay_visible = false;
        }
        memcpy(prev_calls, curr_calls, sizeof(curr_calls));
        prev_count = curr_count;
    }
    return NULL;
}

static bool on_dialer_delete_click_icon(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    size_t len = strlen(dialer_number);
    if (len > 0)
    {
        dialer_number[len - 1] = '\0';
        if (dialer_display_label)
        {
            aroma_label_set_text(dialer_display_label, dialer_number[0] ? dialer_number : "Enter number");
        }
    }
    return true;
}

static bool on_dialer_call_click_icon(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (dialer_number[0] != '\0')
    {
        bt_device_info_t device = bt_speaker_get_device_info();
        if (device.connected)
        {
            bt_hfp_dial(dialer_number);
        }
        dialer_number[0] = '\0';
        if (dialer_display_label)
        {
            aroma_label_set_text(dialer_display_label, "Enter number");
        }
    }
    return true;
}

static bool on_dialer_button_click(AromaNode *node, void *user_data)
{
    (void)node;
    const char *digit = (const char *)user_data;
    if (!digit || strlen(dialer_number) >= MAX_DIALER_DIGITS - 1)
        return true;
    strcat(dialer_number, digit);
    if (dialer_display_label)
    {
        aroma_label_set_text(dialer_display_label, dialer_number);
    }
    return true;
}

static void on_tab_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    if (pagination_card)
    {
        aroma_node_set_hidden(pagination_card, tab_index != 0);
    }
    if (dialer_card)
    {
        aroma_node_set_hidden(dialer_card, tab_index != 1);
    }
    if (state.contact_listview)
    {
        aroma_node_set_hidden(state.contact_listview, tab_index != 0);
    }
    if (tab_index == 0)
    {
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
        aroma_listview_add_item_with_icon(listview, "Connecting to phone...", "Make sure Bluetooth is enabled in settings.", AROMA_ICON_BLUETOOTH_DISABLED, NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    if (state.contact_count == 0)
    {
        aroma_listview_add_item_with_icon(listview, "No contacts found", "Connect a phone with PBAP", AROMA_ICON_PERSON, NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    ContactInfo *sorted_contacts = malloc(sizeof(ContactInfo) * state.contact_count);
    if (!sorted_contacts)
    {
        aroma_listview_add_item_with_icon(listview, "Memory error", "", AROMA_ICON_PERSON, NULL);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    int *orig_indices = malloc(sizeof(int) * state.contact_count);
    for (int i = 0; i < state.contact_count; i++)
    {
        orig_indices[i] = i;
    }
    memcpy(sorted_contacts, state.contacts, sizeof(ContactInfo) * state.contact_count);
    for (int i = 0; i < state.contact_count - 1; i++)
    {
        for (int j = i + 1; j < state.contact_count; j++)
        {
            if (compare_contacts(&sorted_contacts[i], &sorted_contacts[j]) > 0)
            {
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
        aroma_listview_add_item_with_icon(listview, display_name, display_number, AROMA_ICON_PERSON, NULL);
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
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16)
        state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static bool car_frontdoor_open(AromaNode *node, void *user_data)
{
    (void)node;
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
                bt_device_info_t device = bt_speaker_get_device_info();
                if (device.connected)
                {
                    bt_hfp_dial(number);
                }
            }
        }
    }
}

static void update_minimap_display(void)
{
    if (!minimap_visible || !minimap_eta_label || !minimap_distance_label)
        return;

    if (map_nav.active)
    {
        char dist_str[32], time_str[32];
        format_distance_string(map_nav.distance_km, dist_str, sizeof(dist_str));
        format_time_string(map_nav.eta_minutes, time_str, sizeof(time_str));

        char label_text[64];
        snprintf(label_text, sizeof(label_text), "%s  %s", time_str, dist_str);
        aroma_label_set_text(minimap_eta_label, label_text);

        if (minimap_distance_label)
        {
            char dest_truncated[40];
            truncate_for_listview(map_nav.to_text, dest_truncated, sizeof(dest_truncated));
            aroma_label_set_text(minimap_distance_label, dest_truncated);
        }
    }
    else
    {
        aroma_label_set_text(minimap_eta_label, "Searching map...");
        if (minimap_distance_label)
        {
            aroma_label_set_text(minimap_distance_label, "");
        }
    }
}

static void show_minimap(void)
{
    if (minimap_visible || !minimap_card)
        return;

    minimap_visible = true;
    aroma_node_set_hidden(minimap_card, false);
    aroma_node_set_hidden(minimap_node, false);
    aroma_node_set_z_index(minimap_card, MINIMAP_Z_INDEX);
    aroma_node_set_z_index(minimap_node, MINIMAP_Z_INDEX + 1);
    aroma_node_set_z_index(minimap_close_btn, MINIMAP_Z_INDEX + 2);
    aroma_node_set_z_index(minimap_restore_btn, MINIMAP_Z_INDEX + 2);
    aroma_node_set_z_index(minimap_eta_label, MINIMAP_Z_INDEX + 2);
    aroma_node_set_z_index(minimap_distance_label, MINIMAP_Z_INDEX + 2);

    if (map_nav.active)
    {
        aroma_map_clear_markers(minimap_node);
        aroma_map_clear_route(minimap_node);
        aroma_map_add_popup_marker(minimap_node, map_nav.from_lat, map_nav.from_lon, GMAPS_COLOR_START, "S");
        aroma_map_add_popup_marker(minimap_node, map_nav.to_lat, map_nav.to_lon, GMAPS_COLOR_DESTINATION, "D");
        aroma_map_set_route(minimap_node, map_nav.from_lat, map_nav.from_lon, map_nav.to_lat, map_nav.to_lon, GMAPS_COLOR_PRIMARY);

        double center_lat = (map_nav.from_lat + map_nav.to_lat) / 2.0;
        double center_lon = (map_nav.from_lon + map_nav.to_lon) / 2.0;
        aroma_map_set_center(minimap_node, center_lat, center_lon);
        double center_zoom = 14.0 - log2(map_nav.distance_km + 1.0);
        if (center_zoom < 5.0)
            center_zoom = 5.0;
        if (center_zoom > 16.0)
            center_zoom = 16.0;
        aroma_map_set_zoom(minimap_node, center_zoom);
    }
    else
    {
        aroma_map_set_center(minimap_node, 37.7749, -122.4194);
        aroma_map_set_zoom(minimap_node, 14);
    }

    update_minimap_display();

    AromaAnimation *anim = aroma_animation_start(
        minimap_card, AROMA_ANIM_SLIDE_Y,
        MINIMAP_Y + MINIMAP_HEIGHT, MINIMAP_Y, 300);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static void hide_minimap(void)
{
    if (!minimap_visible || !minimap_card)
        return;

    minimap_visible = false;
    aroma_node_set_hidden(minimap_card, true);
    aroma_node_set_hidden(minimap_node, true);
}

static void on_minimap_click(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (card_node)
    {
        open_maps(NULL, card_node);
        hide_minimap();
    }
}

static void on_minimap_close_click(void *user_data)
{
    (void)user_data;
    clear_navigation();
    hide_minimap();
}

static AromaRect maps_icon_anim_start;

void opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(state.map_node);
    if (!maps_rect)
        return;
    AromaRect *icon_rect = aroma_node_get_rect(state.maps_app_icon);
    if (!icon_rect)
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
        aroma_node_set_hidden(state.map_close_btn, false);
        aroma_node_set_hidden(map_search_surface, false);
    }
    aroma_node_invalidate(state.map_node);
    aroma_node_invalidate(state.maps_app_icon);
    aroma_node_invalidate(target);
}

bool open_maps(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;

    hide_minimap();

    map_geocode_result_count = 0;
    last_search_query[0] = '\0';
    search_results_visible = false;
    focused_entry = 0;
    if (!map_nav.active)
    {
        memset(&map_nav, 0, sizeof(map_nav));
        if (map_from_entry)
            aroma_textbox_set_text(map_from_entry, "");
        if (map_to_entry)
            aroma_textbox_set_text(map_to_entry, "");
    }
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
    if (map_search_surface)
    {
        map_search_expanded = true;
        aroma_node_set_hidden(map_search_surface, false);
        if (map_search_placeholder_label)
        {
            aroma_label_set_text(map_search_placeholder_label, "Search for a location");
        }
    }
    return true;
}

static AromaRect maps_icon_anim_end;

void closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
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
        aroma_node_set_hidden(map_search_surface, true);
        aroma_node_set_hidden(map_route_sheet, true);
        aroma_node_set_hidden(map_end_nav_btn, true);
        aroma_node_set_hidden(state.map_node, true);
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        restore_icon_and_card(state.maps_app_icon, target, &maps_icon_offset);
        update_media_card_display();
        map_search_expanded = false;

        if (map_nav.active)
        {
            show_minimap();
        }
        else
        {
            clear_navigation();
        }
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
    aroma_node_set_hidden(map_search_surface, true);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
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
        150, 100, 700, 405);
    aroma_node_set_z_index(state.car_img, Z_LAYER_VEHICLE_IMAGE);

    state.overlay = aroma_ui_image(state.vehicle_view_root, NULL, 150, 100, 700, 405);
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
        WIN_W / 2 + 68, 70, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
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

    state.bottom_bar = aroma_ui_card(state.vehicle_view_root, BOTTOM_BAR_X_COLLAPSED, WIN_H - 110, 360, 80, CARD_TYPE_GLASS);
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
    AromaNode *maps_app_icon_card = aroma_ui_card(state.bottom_bar, 30, 15, 48, 48, CARD_TYPE_ELEVATED);
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
    AromaNode *phone_app_icon_card = aroma_ui_card(state.bottom_bar, 100, 15, 48, 48, CARD_TYPE_ELEVATED);
    aroma_image_set_on_click(state.phone_app_icon, open_phone, phone_app_icon_card);

    AromaNode *divider_to_ac = aroma_ui_divider(state.bottom_bar, 170, 10, 60, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(divider_to_ac, Z_LAYER_VEHICLE_OVERLAYS + 2);

    AromaNode *ac_minus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_REMOVE, 190, 25, 30, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_minus, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *ac_temp_label = aroma_ui_label(state.bottom_bar, "22C", 238, 22, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(ac_temp_label, Z_LAYER_VEHICLE_OVERLAYS + 2);
    state.ac_temp_label = ac_temp_label;
    AromaNode *ac_plus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_ADD, 300, 25, 30, ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_plus, Z_LAYER_VEHICLE_OVERLAYS + 2);

    minimap_card = aroma_ui_card(
        state.vehicle_view_root, MINIMAP_X, MINIMAP_Y + MINIMAP_HEIGHT,
        MINIMAP_WIDTH, MINIMAP_HEIGHT, CARD_TYPE_ELEVATED);
    aroma_card_set_colors(minimap_card, 0xF8FFFFFF, 0xF8FFFFFF);
    aroma_node_set_z_index(minimap_card, MINIMAP_Z_INDEX);
    aroma_node_set_hidden(minimap_card, true);

    minimap_node = aroma_ui_map(
        minimap_card, 4, 4, MINIMAP_WIDTH - 8, MINIMAP_HEIGHT - 50);
    aroma_map_set_zoom(minimap_node, 14);
    aroma_map_set_center(minimap_node, 37.7749, -122.4194);
    aroma_node_set_z_index(minimap_node, MINIMAP_Z_INDEX + 1);
    aroma_node_set_hidden(minimap_node, true);

    minimap_eta_label = aroma_ui_label(
        minimap_card, "-- min  -- km", 8, MINIMAP_HEIGHT - 44,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(minimap_eta_label, MINIMAP_Z_INDEX + 2);
    aroma_label_set_color(minimap_eta_label, GMAPS_COLOR_PRIMARY);

    minimap_distance_label = aroma_ui_label(
        minimap_card, "", 8, MINIMAP_HEIGHT - 28,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(minimap_distance_label, MINIMAP_Z_INDEX + 2);
    aroma_label_set_color(minimap_distance_label, GMAPS_COLOR_ON_SURFACE_VARIANT);

    minimap_restore_btn = aroma_ui_iconbutton(
        minimap_card, AROMA_ICON_FULLSCREEN,
        MINIMAP_WIDTH - 52, MINIMAP_HEIGHT - 42, 32, ICON_BUTTON_FILLED,
        on_minimap_click, maps_app_icon_card, state.icon_font);
    aroma_iconbutton_set_colors(minimap_restore_btn, GMAPS_COLOR_PRIMARY, GMAPS_COLOR_SURFACE);
    aroma_node_set_z_index(minimap_restore_btn, MINIMAP_Z_INDEX + 2);

    minimap_close_btn = aroma_ui_iconbutton(
        minimap_card, AROMA_ICON_CLOSE,
        MINIMAP_WIDTH - 52, 4, 32, ICON_BUTTON_FILLED,
        on_minimap_close_click, NULL, state.icon_font);
    aroma_iconbutton_set_colors(minimap_close_btn, GMAPS_COLOR_DESTINATION, GMAPS_COLOR_SURFACE);
    aroma_node_set_z_index(minimap_close_btn, MINIMAP_Z_INDEX + 2);

    state.map_node = aroma_ui_map(maps_app_icon_card, 0, 0, 48, 48);
    aroma_map_set_zoom(state.map_node, 15);
    aroma_map_set_center(state.map_node, 37.7749, -122.4194);
    aroma_node_set_z_index(state.map_node, Z_LAYER_STATUS_BAR + 11);

    state.map_close_btn = aroma_ui_iconbutton(maps_app_icon_card, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED, close_maps, maps_app_icon_card, state.icon_font);
    aroma_node_set_z_index(state.map_close_btn, Z_LAYER_STATUS_BAR + 20);
    aroma_node_set_hidden(state.map_node, true);
    aroma_node_set_hidden(state.map_close_btn, true);

    map_search_surface = aroma_ui_card(maps_app_icon_card, 0, 100, 320, 800, CARD_TYPE_ELEVATED);
    aroma_card_set_colors(map_search_surface, GMAPS_COLOR_SURFACE, GMAPS_COLOR_SURFACE);
    aroma_node_set_z_index(map_search_surface, Z_LAYER_STATUS_BAR + 15);
    aroma_node_set_hidden(map_search_surface, true);

    map_search_back_btn = aroma_ui_iconbutton(
        map_search_surface, AROMA_ICON_ARROW_BACK, 8, 8, 40, ICON_BUTTON_OUTLINED,
        on_search_back_click, NULL, state.icon_font);
    aroma_iconbutton_set_colors(map_search_back_btn, GMAPS_COLOR_SURFACE, GMAPS_COLOR_ON_SURFACE_VARIANT);
    aroma_node_set_z_index(map_search_back_btn, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(map_search_back_btn, true);

    map_search_placeholder_label = aroma_ui_label(
        map_search_surface, "Search here", 52, 18, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_label_set_color(map_search_placeholder_label, GMAPS_COLOR_ON_SURFACE_VARIANT);
    aroma_node_set_z_index(map_search_placeholder_label, Z_LAYER_STATUS_BAR + 16);

    AromaNode *dir_divider = aroma_ui_divider(map_search_surface, 16, 64, 288, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(dir_divider, Z_LAYER_STATUS_BAR + 16);

    AromaNode *from_icon = aroma_ui_icon(
        map_search_surface, AROMA_ICON_RADIO_BUTTON_CHECKED, 35, 83, 18, GMAPS_COLOR_PRIMARY, state.icon_font);
    aroma_node_set_z_index(from_icon, Z_LAYER_STATUS_BAR + 16);

    map_from_entry = aroma_ui_textbox(map_search_surface, 44, 72, 232, 40, "Current location", on_map_from_entry_change, NULL, state.ui_font);
    aroma_node_set_z_index(map_from_entry, Z_LAYER_STATUS_BAR + 16);

    map_swap_btn = aroma_ui_iconbutton(
        map_search_surface, AROMA_ICON_SWAP_VERT, 284, 76, 28, ICON_BUTTON_FILLED, on_swap_click, NULL, state.icon_font);
    aroma_iconbutton_set_colors(map_swap_btn, GMAPS_COLOR_SURFACE_VARIANT, GMAPS_COLOR_ON_SURFACE_VARIANT);
    aroma_node_set_z_index(map_swap_btn, Z_LAYER_STATUS_BAR + 16);

    AromaNode *to_icon = aroma_ui_icon(
        map_search_surface, AROMA_ICON_PLACE, 35, 133, 18, GMAPS_COLOR_DESTINATION, state.icon_font);
    aroma_node_set_z_index(to_icon, Z_LAYER_STATUS_BAR + 16);

    map_to_entry = aroma_ui_textbox(map_search_surface, 44, 122, 232, 40, "Choose destination", on_map_to_entry_change, NULL, state.ui_font);
    aroma_node_set_z_index(map_to_entry, Z_LAYER_STATUS_BAR + 16);

    map_go_btn = aroma_ui_button(map_search_surface, "Directions", 16, 172, 288, 40, on_go_click, NULL, state.settings_font);
    aroma_iconbutton_set_colors(map_go_btn, GMAPS_COLOR_PRIMARY, GMAPS_COLOR_SURFACE);
    aroma_node_set_z_index(map_go_btn, Z_LAYER_STATUS_BAR + 16);

    map_search_results_list = aroma_ui_listview(
        map_search_surface, 16, 240, 288, 200,
        on_search_result_click, NULL, state.ui_font);
    aroma_listview_set_icon_font(map_search_results_list, state.big_icon_font);
    aroma_node_set_z_index(map_search_results_list, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(map_search_results_list, true);

    map_route_sheet = aroma_ui_card(maps_app_icon_card, 0, 495, 1024, 100, CARD_TYPE_ELEVATED);
    aroma_card_set_colors(map_route_sheet, GMAPS_COLOR_SURFACE, GMAPS_COLOR_SURFACE);
    aroma_node_set_z_index(map_route_sheet, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(map_route_sheet, true);

    AromaNode *dist_icon = aroma_ui_icon(
        map_route_sheet, AROMA_ICON_DIRECTIONS_CAR, 16, 18, 32, GMAPS_COLOR_PRIMARY, state.big_icon_font);
    aroma_node_set_z_index(dist_icon, Z_LAYER_STATUS_BAR + 17);

    map_time_label = aroma_ui_label(map_route_sheet, "-- min", 60, 12, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_label_set_color(map_time_label, GMAPS_COLOR_PRIMARY);
    aroma_node_set_z_index(map_time_label, Z_LAYER_STATUS_BAR + 17);

    map_distance_label = aroma_ui_label(map_route_sheet, "-- km", 60, 42, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_label_set_color(map_distance_label, GMAPS_COLOR_ON_SURFACE_VARIANT);
    aroma_node_set_z_index(map_distance_label, Z_LAYER_STATUS_BAR + 17);

    map_route_dest_label = aroma_ui_label(
        map_route_sheet, "To destination", 60, 72, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_label_set_color(map_route_dest_label, GMAPS_COLOR_ON_SURFACE);
    aroma_node_set_z_index(map_route_dest_label, Z_LAYER_STATUS_BAR + 17);

    map_end_nav_btn = aroma_ui_iconbutton(
        maps_app_icon_card, AROMA_ICON_CLOSE, WIN_W - 76, 20, 48, ICON_BUTTON_FILLED,
        on_end_nav_click, NULL, state.icon_font);
    aroma_iconbutton_set_colors(map_end_nav_btn, GMAPS_COLOR_DESTINATION, GMAPS_COLOR_SURFACE);
    aroma_node_set_z_index(map_end_nav_btn, Z_LAYER_STATUS_BAR + 17);
    aroma_node_set_hidden(map_end_nav_btn, true);

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
    aroma_listview_set_icon_font(state.contact_listview, state.big_icon_font);
    aroma_node_set_z_index(state.contact_listview, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(state.contact_listview, true);

    dialer_card = aroma_ui_card(phone_app_icon_card, 16, 110, 988, 460, CARD_TYPE_OUTLINED);
    aroma_node_set_z_index(dialer_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(dialer_card, true);

    dialer_display_label = aroma_ui_label(dialer_card, "Enter number",
                                          430, 50, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(dialer_display_label, Z_LAYER_STATUS_BAR + 13);

    AromaNode *dialer_grid = aroma_ui_container(
        dialer_card, (988 - 280) / 2, 100, 280, 290,
        AROMA_LAYOUT_MODE_GRID, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(dialer_grid, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_grid_cols(dialer_grid, 3);
    aroma_node_set_grid_rows(dialer_grid, 5);
    aroma_node_set_gap(dialer_grid, 12);

    const int btn_size = 72;
    const char *dialer_digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};

    for (int i = 0; i < 12; i++)
    {
        AromaNode *btn = aroma_ui_button(
            dialer_grid, dialer_digits[i],
            0, 0, btn_size, btn_size,
            on_dialer_button_click,
            (void *)dialer_digits[i], state.settings_font);
        aroma_node_set_z_index(btn, Z_LAYER_STATUS_BAR + 14);
        aroma_iconbutton_set_colors(btn, 0xFF424242, 0xFFFFFFFF);
    }

    AromaNode *del_btn = aroma_ui_button(
        dialer_grid, AROMA_ICON_BACKSPACE,
        0, 0, btn_size, btn_size,
        on_dialer_delete_click_icon, NULL, state.icon_font);
    aroma_node_set_z_index(del_btn, Z_LAYER_STATUS_BAR + 14);
    aroma_iconbutton_set_colors(del_btn, 0xFF424242, 0xFFFFFFFF);

    AromaNode *call_btn = aroma_ui_button(
        dialer_grid, AROMA_ICON_CALL,
        0, 0, btn_size, btn_size,
        on_dialer_call_click_icon, NULL, state.icon_font);
    aroma_node_set_z_index(call_btn, Z_LAYER_STATUS_BAR + 14);
    aroma_iconbutton_set_colors(call_btn, 0xFF4CAF50, 0xFFFFFFFF);

    pagination_card = aroma_ui_card(phone_app_icon_card, 800, 540, 200, 50, CARD_TYPE_FILLED);
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
        WIN_W / 2 - 200, 150, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(incoming_call_name_label, Z_LAYER_VOICE_CONTENT);
    aroma_label_set_color(incoming_call_name_label, 0xFFFFFFFF);

    incoming_call_number_label = aroma_ui_label(
        incoming_call_overlay, "",
        WIN_W / 2 - 150, 220, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(incoming_call_number_label, Z_LAYER_VOICE_CONTENT);
    aroma_label_set_color(incoming_call_number_label, 0xFFAAAAAA);

    incoming_call_accept_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL,
        WIN_W / 2 - 120, 320, 80, ICON_BUTTON_FILLED,
        on_accept_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_accept_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_accept_btn, 0xFF4CAF50, 0xFFFFFFFF);

    incoming_call_reject_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL_END,
        WIN_W / 2 + 40, 320, 80, ICON_BUTTON_FILLED,
        on_reject_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_reject_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_reject_btn, 0xFFF44336, 0xFFFFFFFF);

    incoming_call_end_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL_END,
        WIN_W / 2 - 40, 320, 80, ICON_BUTTON_FILLED,
        on_end_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_end_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_end_btn, 0xFFF44336, 0xFFFFFFFF);
    aroma_node_set_hidden(incoming_call_end_btn, true);

    aroma_animation_start(state.vehicle_view_frunk_divider, AROMA_ANIM_SCALE_Y, 0, 40, 1200);
    aroma_animation_start(state.vehicle_view_trunk_divider, AROMA_ANIM_SCALE_Y, 0, 40, 1200);
    aroma_animation_start(state.vehicle_view_lock_divider, AROMA_ANIM_SCALE_Y, 0, 40, 1200);
    aroma_animation_start(state.vehicle_view_charge_port_divider, AROMA_ANIM_SCALE_X, 0, 20, 1200);

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