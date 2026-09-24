#include "aroma_animation.h"
#include "navigation.h"
#include "navigation_geo.h"
#include "vehicle_view.h"
#include "app_registry.h"
#include "app_state.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void update_suggestions(const char *query);
static void on_geocode_results(GeocodeResult *results, int count, void *user_data);
static void perform_map_search(const char *query);
static void update_pois_markers(void);

static void update_navigation_display(void);
static void clear_navigation(void);
static void show_route_panel(void);
static bool recalculate_route_from_current_position(void);

typedef struct {
    NavigationState *state;
    int slot_index;
} SuggestionSlotContext;


static int estimate_eta_minutes(double distance_km)
{
    return (int)((distance_km / 50.0) * 60.0) + 1;
}




static AromaNode *s_map_node = NULL;
static AromaNode *s_map_close_btn = NULL;
static AromaFont *s_ui_font = NULL;
static AromaFont *s_icon_font = NULL;
static AromaFont *s_settings_font = NULL;


static char s_install_dir[512] = "";

static void nav_asset_path(char *out, size_t out_len, const char *file)
{
    if (s_install_dir[0])
        snprintf(out, out_len, "%s/assets/%s", s_install_dir, file);
    else
        snprintf(out, out_len, "assets/%s", file);
}
static bool search_results_visible = false;
static AromaNode *map_route_sheet = NULL;
static AromaNode *map_end_nav_btn = NULL;
static AromaNode *map_distance_label = NULL;
static AromaNode *map_time_label = NULL;
static AromaNode *map_route_dest_label = NULL;
static bool map_search_expanded = false;
static AromaNode *map_search_results_list = NULL;
static int focused_entry = 0;
static bool poi_refresh_forced = false;
static bool map_options_visible = false;



static bool poi_query_in_flight = false;
static double last_poi_query_time_ms = 0;



static const POICategory nav_poi_categories[NUM_POI_CATEGORIES] = {
    POI_CATEGORY_GAS_STATION,
    POI_CATEGORY_RESTAURANT,
    POI_CATEGORY_CAFE,
    POI_CATEGORY_FAST_FOOD,
    POI_CATEGORY_SHOP,
    POI_CATEGORY_SUPERMARKET,
    POI_CATEGORY_HOTEL,
    POI_CATEGORY_BANK,
    POI_CATEGORY_ATM,
    POI_CATEGORY_PHARMACY,
    POI_CATEGORY_HOSPITAL,
    POI_CATEGORY_PARKING,
    POI_CATEGORY_CHARGING_STATION,
    POI_CATEGORY_OTHER_BUSINESS,
};

static int filtered_poi_count = 0;
static PointOfInterest *filtered_pois = NULL;
static bool selecting_from = false;
static pthread_mutex_t search_mutex = PTHREAD_MUTEX_INITIALIZER;

static AromaNode *map_options_card = NULL;
static NavigationState map_nav = {0};
static AromaNode *map_search_surface = NULL;
static AromaNode *map_search_placeholder_label = NULL;
static AromaNode *map_search_back_btn = NULL;
static bool maps_screen_open = false;
static AromaNode *map_from_entry = NULL;
static AromaNode *map_to_entry = NULL;
static AromaNode *map_go_btn = NULL;
static AromaNode *nav_banner_card = NULL;
static AromaNode *nav_turn_icon = NULL;
static AromaNode *nav_banner_label = NULL;
static AromaNode *nav_banner_sub = NULL;
static AromaNode *nav_eta_label = NULL;
static AromaNode *nav_dist_label = NULL;
static AromaNode *nav_speed_label = NULL;
static AromaNode *nav_turn_dist_label = NULL;
static AromaNode *nav_bottom_card = NULL;
static GeocodeResult map_geocode_results[MAX_GEOCODE_RESULTS];
static int map_geocode_result_count = 0;
static char last_search_query[256] = "";
static AromaNode *suggestion_cards[ITEMS_PER_PAGE];
static AromaNode *suggestion_name_labels[ITEMS_PER_PAGE];
static AromaNode *suggestion_desc_labels[ITEMS_PER_PAGE];
static AromaNode *suggestion_pick_buttons[ITEMS_PER_PAGE];
static AromaNode *suggestion_page;
static AromaNode *page_label_suggestions;
static bool category_enabled[NUM_POI_CATEGORIES];
static int current_page = 0;
static int total_pages_suggestions = 1;
static double last_center_lat = 0.0, last_center_lon = 0.0, last_zoom = 0.0;
static int poi_update_counter = 0;
static SuggestionSlotContext suggestion_slot_contexts[ITEMS_PER_PAGE];

static void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon);
bool open_maps(AromaNode *node, void *user_data);


#define GMAPS_COLOR_PRIMARY 0xFF1A73E8
#define GMAPS_COLOR_SURFACE 0xFFFFFFFF
#define GMAPS_COLOR_ON_SURFACE 0xFF202124
#define GMAPS_COLOR_ON_SURFACE_VARIANT 0xFF5F6368
#define GMAPS_COLOR_DESTINATION 0xFFEA4335
#define GMAPS_COLOR_START 0xFF34A853




static bool on_satellite_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    if (aroma_switch_get_state(switch_node))
    {
        char tiles[768];
        nav_asset_path(tiles, sizeof(tiles), "ariana_sat.mbtiles");
        aroma_map_set_mbtiles(s_map_node, tiles);
    }
    else
    {
        char tiles[768];
        nav_asset_path(tiles, sizeof(tiles), "ariana_3d.mbtiles");
        aroma_map_set_mbtiles(s_map_node, tiles);
    }
    aroma_node_invalidate(s_map_node);
    return true;
}

static void on_map_options_click(void *user_data)
{
    (void)user_data;
    map_options_visible = !map_options_visible;
    if (map_options_card)
    {
        aroma_node_set_hidden(map_options_card, !map_options_visible);
    }
}

static void on_map_options_close_click(void *user_data)
{
    (void)user_data;
    map_options_visible = false;
    if (map_options_card)
    {
        aroma_node_set_hidden(map_options_card, true);
    }
}

void on_preset_item_click(int index, void *user_data)
{
    (void)user_data;
    if (index < 0 || index >= NUM_POI_CATEGORIES)
        return;
}

static bool on_pois_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    bool enabled = aroma_switch_get_state(switch_node);

    static bool saved_categories[NUM_POI_CATEGORIES] = {false};
    static bool categories_saved = false;

    if (!enabled)
    {
        for (int i = 0; i < NUM_POI_CATEGORIES; i++)
        {
            saved_categories[i] = category_enabled[i];
        }
        categories_saved = true;

        for (int i = 0; i < NUM_POI_CATEGORIES; i++)
        {
            category_enabled[i] = false;
        }
    }
    else
    {
        if (categories_saved)
        {
            for (int i = 0; i < NUM_POI_CATEGORIES; i++)
            {
                category_enabled[i] = saved_categories[i];
            }
        }
        else
        {
            for (int i = 0; i < NUM_POI_CATEGORIES; i++)
            {
                category_enabled[i] = true;
            }
        }
    }

    poi_refresh_forced = true;
    last_center_lat = 0.0;
    last_center_lon = 0.0;
    last_zoom = 0.0;

    if (maps_screen_open && s_map_node && !map_nav.navigation_active)
    {
        update_pois_markers();
    }

    return true;
}

static void populate_suggestion_cards(void)
{
    int start = current_page * ITEMS_PER_PAGE;
    int end = start + ITEMS_PER_PAGE;
    if (end > filtered_poi_count)
        end = filtered_poi_count;

    for (int slot = 0; slot < ITEMS_PER_PAGE; slot++)
    {
        int i = start + slot;
        if (i >= end)
        {
            aroma_node_set_hidden(suggestion_cards[slot], true);
            continue;
        }

        PointOfInterest *poi = &filtered_pois[i];
        const char *name = poi->name[0] ? poi->name : "Unnamed";
        const char *street = poi->street[0] ? poi->street : NULL;
        const char *area = poi->area[0] ? poi->area : NULL;

        char description[256];
        if (street && area && street[0] && area[0])
            snprintf(description, sizeof(description), "%s, %s", street, area);
        else if (street && street[0])
            snprintf(description, sizeof(description), "%s", street);
        else if (area && area[0])
            snprintf(description, sizeof(description), "%s", area);
        else if (poi->address[0])
            snprintf(description, sizeof(description), "%s", poi->address);
        else
            snprintf(description, sizeof(description), "%.6f, %.6f", poi->lat, poi->lon);

        aroma_label_set_text(suggestion_name_labels[slot], name);
        aroma_label_set_text(suggestion_desc_labels[slot], description);
        aroma_node_set_hidden(suggestion_cards[slot], false);
    }

    char page_text[64];
    snprintf(page_text, sizeof(page_text), "Page %d/%d", current_page + 1, total_pages_suggestions);
    aroma_label_set_text(page_label_suggestions, page_text);
}

static bool on_suggestion_pick(AromaNode *btn, void *user_data)
{
    (void)btn;
    SuggestionSlotContext *ctx = (SuggestionSlotContext *)user_data;
    if (!ctx)
        return true;

    int actual_index = current_page * ITEMS_PER_PAGE + ctx->slot_index;
    if (actual_index >= filtered_poi_count)
        return true;

    PointOfInterest *poi = &filtered_pois[actual_index];
    const char *name = poi->name[0] ? poi->name : "Unnamed";

    if (selecting_from)
    {
        aroma_textbox_set_text(map_from_entry, name);
        map_nav.from_lat = poi->lat;
        map_nav.from_lon = poi->lon;
        strncpy(map_nav.from_text, name, sizeof(map_nav.from_text) - 1);
        map_nav.from_text[sizeof(map_nav.from_text) - 1] = '\0';
    }
    else
    {
        aroma_textbox_set_text(map_to_entry, name);
        map_nav.to_lat = poi->lat;
        map_nav.to_lon = poi->lon;
        strncpy(map_nav.to_text, name, sizeof(map_nav.to_text) - 1);
        map_nav.to_text[sizeof(map_nav.to_text) - 1] = '\0';
    }

    aroma_node_set_hidden(suggestion_page, true);
    return true;
}

static bool on_from_text_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    selecting_from = true;
    update_suggestions(text);
    return true;
}

static bool on_to_text_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    selecting_from = false;
    update_suggestions(text);
    return true;
}

static bool on_prev_page(AromaNode *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    if (current_page <= 0)
        return true;
    current_page--;
    populate_suggestion_cards();
    return true;
}

static bool on_next_page(AromaNode *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    if (current_page >= total_pages_suggestions - 1)
        return true;
    current_page++;
    populate_suggestion_cards();
    return true;
}

static bool on_close_suggestions(AromaNode *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    aroma_node_set_hidden(suggestion_page, true);
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

static void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon)
{
    map_nav.active = true;
    map_nav.from_lat = from_lat;
    map_nav.from_lon = from_lon;
    map_nav.to_lat = to_lat;
    map_nav.to_lon = to_lon;
    map_nav.distance_km = calculate_distance_km(from_lat, from_lon, to_lat, to_lon);
    map_nav.eta_minutes = estimate_eta_minutes(map_nav.distance_km);

    bool osrm_loaded = aroma_map_is_osrm_loaded(s_map_node);

    aroma_map_clear_markers(s_map_node);
    aroma_map_clear_route(s_map_node);

    if (osrm_loaded)
    {
        aroma_map_set_route_offline(s_map_node, from_lat, from_lon, to_lat, to_lon, GMAPS_COLOR_PRIMARY);
        double *route_lats = NULL;
        double *route_lons = NULL;
        map_nav.route_point_count = aroma_map_get_route_points(s_map_node, &route_lats, &route_lons);

        if (map_nav.route_point_count > 1)
        {
            map_nav.route_ready = true;
            map_nav.simulation_started = true;
            map_nav.navigation_active = true;
            map_nav.frame = 0;
            map_nav.seg_index = 0;
            map_nav.seg_progress_m = 0.0;
            map_nav.have_heading = false;
            map_nav.speed = 0.0;
            map_nav.current_lat = from_lat;
            map_nav.current_lon = from_lon;
            map_nav.off_route_counter = 0;
            map_nav.reroute_cooldown_frames = 0;

            if (map_nav.path_lat)
            {
                free(map_nav.path_lat);
                map_nav.path_lat = NULL;
            }
            if (map_nav.path_lon)
            {
                free(map_nav.path_lon);
                map_nav.path_lon = NULL;
            }

            map_nav.path_lat = malloc(sizeof(double) * map_nav.route_point_count);
            map_nav.path_lon = malloc(sizeof(double) * map_nav.route_point_count);
            for (int i = 0; i < map_nav.route_point_count; i++)
            {
                map_nav.path_lat[i] = nav_mercator_to_lat(route_lats[i]);
                map_nav.path_lon[i] = nav_mercator_to_lon(route_lons[i]);
            }
            map_nav.seg_length_m = nav_haversine_m(map_nav.path_lat[0], map_nav.path_lon[0],
                                                   map_nav.path_lat[1], map_nav.path_lon[1]);

            aroma_map_add_popup_marker(s_map_node, to_lat, to_lon, GMAPS_COLOR_DESTINATION, "Destination");
            aroma_map_add_marker(s_map_node, map_nav.current_lat, map_nav.current_lon, GMAPS_COLOR_PRIMARY);

            if (nav_banner_card)
            {
                aroma_node_set_hidden(nav_banner_card, false);
                aroma_node_set_hidden(nav_bottom_card, false);
            }

            aroma_map_set_center_instant(s_map_node, map_nav.current_lat, map_nav.current_lon);
            aroma_map_set_zoom(s_map_node, 18);

            show_route_panel();
            return;
        }
    }

    aroma_map_set_route(s_map_node, from_lat, from_lon, to_lat, to_lon, GMAPS_COLOR_PRIMARY);
    aroma_map_add_popup_marker(s_map_node, from_lat, from_lon, GMAPS_COLOR_START, "Start");
    aroma_map_add_popup_marker(s_map_node, to_lat, to_lon, GMAPS_COLOR_DESTINATION, "Destination");
    map_nav.route_ready = false;
    map_nav.navigation_active = false;
    map_nav.simulation_started = false;

    aroma_map_set_zoom(s_map_node, 18);

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
        map_nav.from_lat = 36.8625f;
        map_nav.from_lon = 10.1956f;
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

bool open_maps(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;

    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

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
    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    maps_screen_open = true;
    poi_refresh_forced = true;
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_OPEN_EASE);
    aroma_node_set_hidden(s_map_node, false);
    aroma_node_set_hidden(s_map_close_btn, false);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    aroma_map_set_zoom(s_map_node, 18);
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

void closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(s_map_node);
    if (!maps_rect)
        return;

    int start_y = 0;
    int end_y = WIN_H;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

    maps_rect->x = rect->x;
    maps_rect->y = rect->y;
    maps_rect->width = rect->width;
    maps_rect->height = rect->height;

    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(s_map_close_btn, true);
        aroma_node_set_hidden(map_search_surface, true);
        aroma_node_set_hidden(map_route_sheet, true);
        aroma_node_set_hidden(map_end_nav_btn, true);
        aroma_node_set_hidden(map_options_card, true);
        aroma_node_set_hidden(s_map_node, true);
        aroma_node_set_hidden(target, true);
        maps_screen_open = false;
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
        if (suggestion_page)
        {
            aroma_node_set_hidden(suggestion_page, true);
        }
        if (nav_banner_card)
        {
            aroma_node_set_hidden(nav_banner_card, true);
            aroma_node_set_hidden(nav_bottom_card, true);
        }
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        map_search_expanded = false;
        map_options_visible = false;
        restore_app_drawer_from_behind();

        if (!map_nav.active)
        {
            clear_navigation();
        }
    }
    aroma_node_invalidate(s_map_node);
    aroma_node_invalidate(target);
}

void close_maps(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, closing_anim, NULL);
    aroma_node_set_hidden(map_search_surface, true);
    aroma_animation_set_easing(anim, APP_ANIM_CLOSE_EASE);
}

static bool nav_app_init(AromaAppPlugin *app) {
    return true;
}


static void nav_app_destroy(AromaAppPlugin *app) {
}

static bool nav_app_show(AromaAppPlugin *app, AromaNode *parent) {
    return open_maps(NULL, app->app_root);
}

static void nav_app_hide(AromaAppPlugin *app) {
    close_maps(app->app_root);
}

void nav_app_update(AromaAppPlugin *app) {
    if (maps_screen_open && s_map_node && !map_nav.navigation_active)
    {
        update_pois_markers();
    }

    if (map_nav.simulation_started && map_nav.route_ready && map_nav.route_point_count > 1)
    {
        map_nav.frame++;

        if (map_nav.seg_index < map_nav.route_point_count - 1)
        {
            double dist_remaining_estimate = 0.0;
            if (map_nav.route_point_count > 1)
            {
                for (int i = map_nav.seg_index; i < map_nav.route_point_count - 1; i++)
                    dist_remaining_estimate += nav_haversine_m(map_nav.path_lat[i], map_nav.path_lon[i],
                                                               map_nav.path_lat[i + 1], map_nav.path_lon[i + 1]);
                dist_remaining_estimate -= map_nav.seg_progress_m;
            }

            double target_speed = 35.0;
            if (map_nav.frame < 90)
                target_speed = 35.0 * (map_nav.frame / 90.0);
            if (dist_remaining_estimate < 60.0)
                target_speed = fmin(target_speed, 35.0 * (dist_remaining_estimate / 60.0));
            if (target_speed < 5.0 && dist_remaining_estimate > 2.0)
                target_speed = 5.0;
            map_nav.speed += (target_speed - map_nav.speed) * 0.05;
            if (map_nav.speed < 0.0)
                map_nav.speed = 0.0;

            double meters_per_frame = (map_nav.speed * 1000.0 / 3600.0) / 60.0;
            map_nav.seg_progress_m += meters_per_frame;

            while (map_nav.seg_progress_m >= map_nav.seg_length_m && map_nav.seg_index < map_nav.route_point_count - 2)
            {
                map_nav.seg_progress_m -= map_nav.seg_length_m;
                map_nav.seg_index++;
                map_nav.seg_length_m = nav_haversine_m(map_nav.path_lat[map_nav.seg_index], map_nav.path_lon[map_nav.seg_index],
                                                       map_nav.path_lat[map_nav.seg_index + 1], map_nav.path_lon[map_nav.seg_index + 1]);
            }

            double t = (map_nav.seg_length_m > 0.0001) ? (map_nav.seg_progress_m / map_nav.seg_length_m) : 0.0;
            if (t > 1.0)
                t = 1.0;

            double a_lat = map_nav.path_lat[map_nav.seg_index], a_lon = map_nav.path_lon[map_nav.seg_index];
            double b_lat = map_nav.path_lat[map_nav.seg_index + 1], b_lon = map_nav.path_lon[map_nav.seg_index + 1];

            map_nav.current_lat = a_lat + (b_lat - a_lat) * t;
            map_nav.current_lon = a_lon + (b_lon - a_lon) * t;

            double raw_bearing = nav_bearing_deg(a_lat, a_lon, b_lat, b_lon);
            if (!map_nav.have_heading)
            {
                map_nav.display_heading = raw_bearing;
                map_nav.have_heading = true;
            }
            else
                map_nav.display_heading += nav_shortest_angle_diff(map_nav.display_heading, raw_bearing) * 0.15;
            map_nav.display_heading = fmod(map_nav.display_heading + 360.0, 360.0);

            aroma_map_set_gps_position(s_map_node, map_nav.current_lat, map_nav.current_lon,
                                       map_nav.display_heading, map_nav.speed);
            aroma_map_set_center(s_map_node, map_nav.current_lat, map_nav.current_lon);

            if (map_nav.reroute_cooldown_frames > 0)
                map_nav.reroute_cooldown_frames--;

            double off_route_distance = nav_min_distance_to_route_m(map_nav.path_lat, map_nav.path_lon,
                                                                    map_nav.route_point_count,
                                                                    map_nav.current_lat, map_nav.current_lon);
            if (off_route_distance > OFF_ROUTE_THRESHOLD_M)
                map_nav.off_route_counter++;
            else
                map_nav.off_route_counter = 0;

            if (map_nav.off_route_counter >= OFF_ROUTE_CONFIRM_FRAMES && map_nav.reroute_cooldown_frames == 0)
            {
                bool rerouted = recalculate_route_from_current_position();
                if (rerouted)
                {
                    aroma_label_set_text(nav_banner_label, "Route recalculated");
                    aroma_label_set_text(nav_banner_sub, "You are back on route");
                }
                else
                {
                    aroma_label_set_text(nav_banner_label, "Recalculate failed");
                    aroma_label_set_text(nav_banner_sub, "Keep driving to recover signal");
                    map_nav.reroute_cooldown_frames = RE_ROUTE_COOLDOWN_FRAMES;
                    map_nav.off_route_counter = 0;
                }
            }

            bool reached_end = (map_nav.seg_index >= map_nav.route_point_count - 2 && t >= 1.0);

            if (map_nav.frame % 15 == 0 || reached_end)
            {
                aroma_map_clear_markers(s_map_node);
                aroma_map_add_popup_marker(s_map_node, map_nav.to_lat, map_nav.to_lon, GMAPS_COLOR_DESTINATION, "Destination");
                aroma_map_add_marker(s_map_node, map_nav.current_lat, map_nav.current_lon, GMAPS_COLOR_PRIMARY);
            }

            if (map_nav.frame % 15 == 0)
            {
                update_navigation_display();
            }

            if (reached_end)
            {
                aroma_label_set_text(nav_banner_label, "Arrived");
                aroma_label_set_text(nav_banner_sub, "Destination reached");
                aroma_icon_set_text(nav_turn_icon, AROMA_ICON_PLACE, s_icon_font);
                map_nav.simulation_started = false;
                map_nav.navigation_active = false;
                map_nav.active = false;
                aroma_map_clear_markers(s_map_node);
                aroma_node_set_hidden(nav_banner_card, true);
                aroma_node_set_hidden(nav_bottom_card, true);
                aroma_node_set_hidden(map_search_surface, false);
                map_search_expanded = true;
                aroma_node_set_hidden(map_route_sheet, false);
                aroma_node_set_hidden(map_end_nav_btn, false);
            }
        }
    }


}

static bool nav_app_build_ui(AromaAppPlugin *app, AromaNode *parent) {
    (void)app;
s_map_node = aroma_ui_map(parent, 0, 0, 48, 48);
    {
        char mbtiles[768], routing[768], pois[768];
        nav_asset_path(mbtiles, sizeof(mbtiles), "ariana_3d.mbtiles");
        nav_asset_path(routing, sizeof(routing), "routing_data.bin");
        nav_asset_path(pois, sizeof(pois), "tunisia_pois.db");
        aroma_map_set_mbtiles(s_map_node, mbtiles);
        aroma_map_load_osrm_data(s_map_node, routing);
        aroma_map_load_poi_database(s_map_node, pois);
    }
    aroma_map_set_center(s_map_node, 36.8625, 10.1956);
    aroma_map_set_animations_enabled(s_map_node, false);

    aroma_node_set_z_index(s_map_node, Z_LAYER_STATUS_BAR + 11);
    s_map_close_btn = aroma_ui_iconbutton(parent, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED, close_maps, parent, s_icon_font);
    aroma_node_set_z_index(s_map_close_btn, Z_LAYER_STATUS_BAR + 20);
    aroma_node_set_hidden(s_map_node, true);
    aroma_node_set_hidden(s_map_close_btn, true);

    for (int i = 0; i < NUM_POI_CATEGORIES; i++)
        category_enabled[i] = false;

    category_enabled[0] = true;
    category_enabled[5] = true;
    category_enabled[10] = true;
    category_enabled[11] = true;
    category_enabled[12] = true;

    last_center_lat = 0.0;
    last_center_lon = 0.0;
    last_zoom = 0.0;
    poi_update_counter = 0;
    current_page = 0;
    total_pages_suggestions = 1;
    poi_refresh_forced = true;

    AromaNode *map_options_btn = aroma_ui_iconbutton(
        parent, AROMA_ICON_MORE_VERT, WIN_W - 70, 20, 48, ICON_BUTTON_FILLED,
        on_map_options_click, NULL, s_icon_font);
    aroma_node_set_z_index(map_options_btn, Z_LAYER_STATUS_BAR + 20);

    map_options_card = aroma_ui_card(
        parent, WIN_W - 320, 80, 300, 200, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(map_options_card, Z_LAYER_STATUS_BAR + 21);
    aroma_node_set_hidden(map_options_card, true);

    AromaNode *map_options_close_btn = aroma_ui_iconbutton(
        map_options_card, AROMA_ICON_CLOSE, 260, 8, 32, ICON_BUTTON_OUTLINED,
        on_map_options_close_click, NULL, s_icon_font);
    aroma_node_set_z_index(map_options_close_btn, Z_LAYER_STATUS_BAR + 22);

    AromaNode *map_options_title = aroma_ui_label(
        map_options_card, "Map Options", 16, 16, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    aroma_node_set_z_index(map_options_title, Z_LAYER_STATUS_BAR + 22);

    AromaNode *satellite_switch = aroma_ui_switch(
        map_options_card, 210, 60, 60, 30,
        false, on_satellite_switch_changed, NULL);
    aroma_node_set_z_index(satellite_switch, Z_LAYER_STATUS_BAR + 22);

    AromaNode *satellite_label = aroma_ui_label(
        map_options_card, "Satellite View", 16, 65, LABEL_STYLE_LABEL_SMALL, s_ui_font);
    aroma_node_set_z_index(satellite_label, Z_LAYER_STATUS_BAR + 22);

    AromaNode *pois_switch = aroma_ui_switch(
        map_options_card, 210, 110, 60, 30,
        true, on_pois_switch_changed, NULL);
    aroma_node_set_z_index(pois_switch, Z_LAYER_STATUS_BAR + 22);
    AromaNode *pois_label = aroma_ui_label(
        map_options_card, "Show POIs", 16, 115, LABEL_STYLE_LABEL_SMALL, s_ui_font);
    aroma_node_set_z_index(pois_label, Z_LAYER_STATUS_BAR + 22);

    map_search_surface = aroma_ui_card(parent, 0, 80, 340, 520, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(map_search_surface, Z_LAYER_STATUS_BAR + 15);
    aroma_node_set_hidden(map_search_surface, true);

    map_search_placeholder_label = aroma_ui_label(
        map_search_surface, "Search here", 60, 18, LABEL_STYLE_LABEL_MEDIUM, s_ui_font);
    aroma_node_set_z_index(map_search_placeholder_label, Z_LAYER_STATUS_BAR + 16);

    map_search_back_btn = aroma_ui_iconbutton(
        map_search_surface, AROMA_ICON_ARROW_BACK, 8, 8, 40, ICON_BUTTON_OUTLINED,
        on_search_back_click, NULL, s_icon_font);
    aroma_iconbutton_set_colors(map_search_back_btn, GMAPS_COLOR_SURFACE, GMAPS_COLOR_ON_SURFACE_VARIANT);
    aroma_node_set_z_index(map_search_back_btn, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(map_search_back_btn, true);

    AromaNode *dir_divider = aroma_ui_divider(map_search_surface, 16, 64, 308, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(dir_divider, Z_LAYER_STATUS_BAR + 16);

    map_from_entry = aroma_ui_textbox(map_search_surface, 15, 72, 280, 40, "Search from...", on_from_text_changed, NULL, s_ui_font);
    aroma_textbox_enable_virtual_keyboard(map_from_entry, true);
    aroma_node_set_z_index(map_from_entry, Z_LAYER_STATUS_BAR + 16);

    map_to_entry = aroma_ui_textbox(map_search_surface, 15, 122, 280, 40, "Search to...", on_to_text_changed, NULL, s_ui_font);
    aroma_textbox_enable_virtual_keyboard(map_to_entry, true);
    aroma_node_set_z_index(map_to_entry, Z_LAYER_STATUS_BAR + 16);

    map_go_btn = aroma_ui_button(map_search_surface, "Directions", 16, 172, 308, 40, on_go_click, NULL, s_settings_font);
    aroma_node_set_z_index(map_go_btn, Z_LAYER_STATUS_BAR + 16);

    AromaNode *preset_title = aroma_ui_label(map_search_surface, "Presets", 16, 232, LABEL_STYLE_LABEL_LARGE, s_settings_font);
    aroma_node_set_z_index(preset_title, Z_LAYER_STATUS_BAR + 16);

    AromaNode *preset_listview = aroma_listview_create(map_search_surface, 16, 260, 308, 240);
    aroma_listview_add_item_with_icon(preset_listview, "Home", "", AROMA_ICON_HOME, NULL);
    aroma_listview_add_item_with_icon(preset_listview, "Parad'Ice", "", AROMA_ICON_LOCAL_DINING, NULL);
    aroma_listview_add_item_with_icon(preset_listview, "ISI", "", AROMA_ICON_BOOK, NULL);
    aroma_listview_add_item_with_icon(preset_listview, "Agile", "", AROMA_ICON_LOCAL_GAS_STATION, NULL);
    aroma_listview_set_font(preset_listview, s_ui_font);
    aroma_listview_set_icon_font(preset_listview, s_icon_font);
    aroma_node_set_z_index(preset_listview, Z_LAYER_STATUS_BAR + 16);
    aroma_listview_set_callback(preset_listview, on_preset_item_click, NULL);

    suggestion_page = aroma_ui_card(parent, 320, 80, 704, 520, CARD_TYPE_ELEVATED);
    aroma_node_set_hidden(suggestion_page, true);
    aroma_node_set_z_index(suggestion_page, Z_LAYER_STATUS_BAR + 40);

    AromaNode *suggestion_title = aroma_ui_label(suggestion_page, "Select Location", 280, 10, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    aroma_node_set_z_index(suggestion_title, Z_LAYER_STATUS_BAR + 41);

    for (int slot = 0; slot < ITEMS_PER_PAGE; slot++)
    {
        int card_y = 50 + slot * 55;

        AromaNode *card = aroma_ui_card(suggestion_page, 16, card_y, 672, 50, CARD_TYPE_ELEVATED);
        aroma_node_set_z_index(card, Z_LAYER_STATUS_BAR + 41);

        AromaNode *name_label = aroma_ui_label(card, "", 10, 5, LABEL_STYLE_LABEL_MEDIUM, s_ui_font);
        AromaNode *desc_label = aroma_ui_label(card, "", 10, 25, LABEL_STYLE_LABEL_SMALL, s_ui_font);
        aroma_label_set_color(desc_label, GMAPS_COLOR_ON_SURFACE_VARIANT);
        aroma_node_set_z_index(name_label, Z_LAYER_STATUS_BAR + 42);
        aroma_node_set_z_index(desc_label, Z_LAYER_STATUS_BAR + 42);

        suggestion_slot_contexts[slot].state = &map_nav;
        suggestion_slot_contexts[slot].slot_index = slot;

        AromaNode *pick_btn = aroma_ui_button_with_icon(card, "Pick", 520, 8, 90, 34,
                                                        on_suggestion_pick, &suggestion_slot_contexts[slot], s_ui_font, AROMA_ICON_CHECK, s_icon_font);
        aroma_node_set_z_index(pick_btn, Z_LAYER_STATUS_BAR + 42);

        aroma_node_set_hidden(card, true);

        suggestion_cards[slot] = card;
        suggestion_name_labels[slot] = name_label;
        suggestion_desc_labels[slot] = desc_label;
        suggestion_pick_buttons[slot] = pick_btn;
    }

    page_label_suggestions = aroma_ui_label(suggestion_page, "Page 1/1", 320, 490, LABEL_STYLE_LABEL_MEDIUM, s_ui_font);
    aroma_node_set_z_index(page_label_suggestions, Z_LAYER_STATUS_BAR + 41);

    AromaNode *prev_btn = aroma_ui_button_with_icon(suggestion_page, "Prev", 110, 400, 80, 50, on_prev_page, NULL, s_ui_font, AROMA_ICON_ARROW_LEFT, s_icon_font);
    AromaNode *next_btn = aroma_ui_button_with_icon(suggestion_page, "Next", 290, 400, 80, 50, on_next_page, NULL, s_ui_font, AROMA_ICON_ARROW_RIGHT, s_icon_font);
    AromaNode *close_btn = aroma_ui_button_with_icon(suggestion_page, "Close", 470, 400, 80, 50, on_close_suggestions, NULL, s_ui_font, AROMA_ICON_CLOSE, s_icon_font);
    aroma_node_set_z_index(prev_btn, Z_LAYER_STATUS_BAR + 41);
    aroma_node_set_z_index(next_btn, Z_LAYER_STATUS_BAR + 41);
    aroma_node_set_z_index(close_btn, Z_LAYER_STATUS_BAR + 41);

    nav_banner_card = aroma_ui_card(parent, 80, 10, 864, 80, CARD_TYPE_ELEVATED);
    nav_turn_icon = aroma_ui_icon(nav_banner_card, AROMA_ICON_ARROW_UPWARD, 30, 20, 40, GMAPS_COLOR_PRIMARY, s_icon_font);
    nav_banner_label = aroma_ui_label(nav_banner_card, "Starting navigation...", 65, 10, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    nav_banner_sub = aroma_ui_label(nav_banner_card, "", 65, 50, LABEL_STYLE_LABEL_SMALL, s_ui_font);
    aroma_node_set_hidden(nav_banner_card, true);
    aroma_node_set_z_index(nav_banner_card, Z_LAYER_STATUS_BAR + 30);
    aroma_node_set_z_index(nav_turn_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_banner_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_banner_sub, Z_LAYER_STATUS_BAR + 31);
    nav_bottom_card = aroma_ui_card(parent, 80, WIN_H - 90, 864, 80, CARD_TYPE_ELEVATED);
    AromaNode *eta_icon = aroma_ui_icon(nav_bottom_card, AROMA_ICON_SCHEDULE, 45, 32, 28, 0xFF00C853, s_icon_font);
    nav_eta_label = aroma_ui_label(nav_bottom_card, "-- min", 80, 32, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    AromaNode *dist_icon2 = aroma_ui_icon(nav_bottom_card, AROMA_ICON_PLACE, 230, 32, 28, 0xFFFF6D00, s_icon_font);
    nav_dist_label = aroma_ui_label(nav_bottom_card, "-- km", 265, 32, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    AromaNode *speed_icon = aroma_ui_icon(nav_bottom_card, AROMA_ICON_GRAPHIC_EQ, 420, 32, 28, 0xFF2979FF, s_icon_font);
    nav_speed_label = aroma_ui_label(nav_bottom_card, "-- km/h", 455, 32, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    AromaNode *turn_dist_icon = aroma_ui_icon(nav_bottom_card, AROMA_ICON_NAVIGATION, 595, 32, 28, 0xFFD50000, s_icon_font);
    nav_turn_dist_label = aroma_ui_label(nav_bottom_card, "--", 630, 32, LABEL_STYLE_LABEL_MEDIUM, s_settings_font);
    aroma_node_set_hidden(nav_bottom_card, true);
    aroma_node_set_z_index(nav_bottom_card, Z_LAYER_STATUS_BAR + 30);
    aroma_node_set_z_index(eta_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_eta_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(dist_icon2, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_dist_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(speed_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_speed_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(turn_dist_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_turn_dist_label, Z_LAYER_STATUS_BAR + 31);

    return true;
}


static void update_pois_markers(void)
{
    if (!s_map_node)
        return;

    if (map_nav.navigation_active)
        return;

    AromaMap *map_widget = (AromaMap *)s_map_node->node_widget_ptr;
    if (!map_widget)
        return;

    double center_lat = map_widget->center_lat;
    double center_lon = map_widget->center_lon;
    double zoom = aroma_map_get_zoom(s_map_node);

    if (poi_query_in_flight)
        return;

    double now_ms = monotonic_ms();
    bool time_elapsed = (now_ms - last_poi_query_time_ms) >= POI_QUERY_MIN_INTERVAL_MS;
    bool moved_enough =
        fabs(center_lat - last_center_lat) > POI_QUERY_MOVE_THRESHOLD_DEG ||
        fabs(center_lon - last_center_lon) > POI_QUERY_MOVE_THRESHOLD_DEG ||
        fabs(zoom - last_zoom) > POI_QUERY_ZOOM_THRESHOLD;

    if (!poi_refresh_forced && !moved_enough)
        return;

    if (!time_elapsed && !poi_refresh_forced)
        return;

    poi_query_in_flight = true;
    poi_refresh_forced = false;
    last_poi_query_time_ms = now_ms;

    poi_update_counter++;

    last_center_lat = center_lat;
    last_center_lon = center_lon;
    last_zoom = zoom;

    AromaRect *map_rect = aroma_node_get_rect(s_map_node);
    double half_w = map_rect ? map_rect->width / 2.0 : 512.0;
    double half_h = map_rect ? map_rect->height / 2.0 : 300.0;

    double lat_rad = center_lat * M_PI / 180.0;
    double z_factor = pow(2.0, zoom) * 256.0;

    double px_x = (center_lon + 180.0) / 360.0 * z_factor;
    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor;

    double margin = 1.5;
    double min_px_x = px_x - half_w * margin;
    double max_px_x = px_x + half_w * margin;
    double min_px_y = px_y - half_h * margin;
    double max_px_y = px_y + half_h * margin;

    double view_min_lon = (min_px_x / z_factor) * 360.0 - 180.0;
    double view_max_lon = (max_px_x / z_factor) * 360.0 - 180.0;

    double n_min = M_PI - 2.0 * M_PI * (min_px_y / z_factor);
    double n_max = M_PI - 2.0 * M_PI * (max_px_y / z_factor);

    double view_max_lat = 180.0 / M_PI * (2.0 * atan(exp(n_min)) - M_PI / 2.0);
    double view_min_lat = 180.0 / M_PI * (2.0 * atan(exp(n_max)) - M_PI / 2.0);

    if (view_max_lat < view_min_lat)
    {
        double tmp = view_max_lat;
        view_max_lat = view_min_lat;
        view_min_lat = tmp;
    }
    if (view_max_lon < view_min_lon)
    {
        double tmp = view_max_lon;
        view_max_lon = view_min_lon;
        view_min_lon = tmp;
    }

    view_min_lat = fmax(view_min_lat, -85.0);
    view_max_lat = fmin(view_max_lat, 85.0);
    view_min_lon = fmax(view_min_lon, -180.0);
    view_max_lon = fmin(view_max_lon, 180.0);

    int enabled_category_count = 0;
    for (int j = 0; j < NUM_POI_CATEGORIES; j++)
    {
        if (category_enabled[j])
            enabled_category_count++;
    }

    if (enabled_category_count == 0)
    {
        aroma_map_clear_markers(s_map_node);
        aroma_node_invalidate(s_map_node);
        poi_query_in_flight = false;
        return;
    }

    int result_count = 0;
    PointOfInterest *pois = aroma_map_query_pois_in_viewport(
        s_map_node, view_min_lat, view_max_lat, view_min_lon, view_max_lon, &result_count);

    if (!pois || result_count == 0)
    {
        poi_query_in_flight = false;
        return;
    }

    typedef struct
    {
        double lat;
        double lon;
        uint32_t color;
        const char *icon_code;
    } MarkerCandidate;

    MarkerCandidate candidates[200];
    int candidate_count = 0;

    int max_markers = 200;

    for (int i = 0; i < result_count && candidate_count < max_markers; i++)
    {
        PointOfInterest *poi = &pois[i];

        bool cat_enabled = false;
        for (int j = 0; j < NUM_POI_CATEGORIES; j++)
        {
            if (poi->category == nav_poi_categories[j] && category_enabled[j])
            {
                cat_enabled = true;
                break;
            }
        }

        if (!cat_enabled)
            continue;
        if (!poi->name[0])
            continue;

        uint32_t color = 0xFF999999;
        const char *icon_code = AROMA_ICON_PLACE;

        switch (poi->category)
        {
        case POI_CATEGORY_GAS_STATION:
            color = 0xFFFF6600;
            icon_code = AROMA_ICON_LOCAL_GAS_STATION;
            break;
        case POI_CATEGORY_RESTAURANT:
        case POI_CATEGORY_FAST_FOOD:
            color = 0xFFFF3333;
            icon_code = AROMA_ICON_RESTAURANT;
            break;
        case POI_CATEGORY_CAFE:
            color = 0xFF8B4513;
            icon_code = AROMA_ICON_LOCAL_CAFE;
            break;
        case POI_CATEGORY_PARKING:
            color = 0xFF3366FF;
            icon_code = AROMA_ICON_LOCAL_PARKING;
            break;
        case POI_CATEGORY_CHARGING_STATION:
            color = 0xFF00FF00;
            icon_code = AROMA_ICON_EV_STATION;
            break;
        case POI_CATEGORY_HOSPITAL:
            color = 0xFFFF0000;
            icon_code = AROMA_ICON_LOCAL_HOSPITAL;
            break;
        case POI_CATEGORY_PHARMACY:
            color = 0xFF009933;
            icon_code = AROMA_ICON_LOCAL_PHARMACY;
            break;
        case POI_CATEGORY_HOTEL:
            color = 0xFF0066CC;
            icon_code = AROMA_ICON_LOCAL_HOTEL;
            break;
        case POI_CATEGORY_ATM:
            color = 0xFF006699;
            icon_code = AROMA_ICON_LOCAL_ATM;
            break;
        case POI_CATEGORY_BANK:
            color = 0xFF003366;
            icon_code = AROMA_ICON_ACCOUNT_BALANCE;
            break;
        case POI_CATEGORY_SHOP:
            color = 0xFF9933FF;
            icon_code = AROMA_ICON_SHOP;
            break;
        case POI_CATEGORY_SUPERMARKET:
            color = 0xFF00CC00;
            icon_code = AROMA_ICON_LOCAL_GROCERY_STORE;
            break;
        default:
            color = 0xFF999999;
            icon_code = AROMA_ICON_PLACE;
            break;
        }

        candidates[candidate_count].lat = poi->lat;
        candidates[candidate_count].lon = poi->lon;
        candidates[candidate_count].color = color;
        candidates[candidate_count].icon_code = icon_code;
        candidate_count++;
    }

    if (candidate_count == 0)
    {
        poi_query_in_flight = false;
        return;
    }

    aroma_map_clear_markers(s_map_node);
    for (int i = 0; i < candidate_count; i++)
    {
        aroma_map_add_icon_marker_with_font(s_map_node,
                                            candidates[i].lat,
                                            candidates[i].lon,
                                            candidates[i].color,
                                            candidates[i].icon_code,
                                            s_icon_font);
    }

    aroma_node_invalidate(s_map_node);

    poi_query_in_flight = false;
}

static void update_suggestions(const char *query)
{
    if (filtered_pois)
    {
        free(filtered_pois);
        filtered_pois = NULL;
    }
    filtered_poi_count = 0;
    current_page = 0;

    if (!query || query[0] == '\0')
    {
        aroma_node_set_hidden(suggestion_page, true);
        return;
    }

    int result_count = 0;
    PointOfInterest *results = aroma_map_query_pois_by_name(s_map_node, query, MAX_SUGGESTIONS, &result_count);

    if (result_count > 0 && results)
    {
        filtered_pois = malloc(result_count * sizeof(PointOfInterest));
        if (filtered_pois)
        {
            memcpy(filtered_pois, results, result_count * sizeof(PointOfInterest));
            filtered_poi_count = result_count;
        }
    }

    if (filtered_poi_count > 0)
    {
        total_pages_suggestions = (filtered_poi_count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        populate_suggestion_cards();
        aroma_node_invalidate(suggestion_page);
        aroma_node_set_hidden(suggestion_page, false);
    }
    else
    {
        aroma_node_set_hidden(suggestion_page, true);
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

    aroma_map_geocode_search(s_map_node, query, on_geocode_results, NULL);
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

static bool recalculate_route_from_current_position(void)
{
    if (!s_map_node)
        return false;

    if (!aroma_map_is_osrm_loaded(s_map_node))
        return false;

    aroma_map_set_route_offline(s_map_node, map_nav.current_lat, map_nav.current_lon,
                                map_nav.to_lat, map_nav.to_lon, GMAPS_COLOR_PRIMARY);

    double *route_lats = NULL;
    double *route_lons = NULL;
    int new_count = aroma_map_get_route_points(s_map_node, &route_lats, &route_lons);
    if (new_count <= 1 || !route_lats || !route_lons)
        return false;

    double *new_path_lat = malloc(sizeof(double) * new_count);
    double *new_path_lon = malloc(sizeof(double) * new_count);
    if (!new_path_lat || !new_path_lon)
    {
        if (new_path_lat)
            free(new_path_lat);
        if (new_path_lon)
            free(new_path_lon);
        return false;
    }

    for (int i = 0; i < new_count; i++)
    {
        new_path_lat[i] = nav_mercator_to_lat(route_lats[i]);
        new_path_lon[i] = nav_mercator_to_lon(route_lons[i]);
    }

    if (map_nav.path_lat)
        free(map_nav.path_lat);
    if (map_nav.path_lon)
        free(map_nav.path_lon);
    map_nav.path_lat = new_path_lat;
    map_nav.path_lon = new_path_lon;
    map_nav.route_point_count = new_count;
    map_nav.seg_index = 0;
    map_nav.seg_progress_m = 0.0;
    map_nav.seg_length_m = nav_haversine_m(map_nav.path_lat[0], map_nav.path_lon[0],
                                           map_nav.path_lat[1], map_nav.path_lon[1]);
    map_nav.off_route_counter = 0;
    map_nav.reroute_cooldown_frames = RE_ROUTE_COOLDOWN_FRAMES;
    return true;
}

static void update_navigation_display(void)
{
    if (!map_nav.navigation_active || !map_nav.route_ready)
        return;

    RouteProgress progress;
    aroma_map_get_route_progress(s_map_node, &progress);
    TurnInstruction turn;
    aroma_map_get_next_turn(s_map_node, &turn);

    const char *icon_code = AROMA_ICON_ARROW_UPWARD;
    char banner_text[128];
    char banner_sub_text[128];
    char turn_dist_str[32];

    if (progress.distance_to_next_turn < 100 && turn.type != MANEUVER_ARRIVE && turn.type != MANEUVER_NONE)
    {
        if (turn.type == MANEUVER_ROUNDABOUT)
        {
            strcpy(banner_text, "Roundabout");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "Take exit %d", turn.roundabout_exit);
            icon_code = AROMA_ICON_REFRESH;
        }
        else if (turn.type == MANEUVER_TURN_LEFT)
        {
            strcpy(banner_text, "Turn left");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_ARROW_BACK;
        }
        else if (turn.type == MANEUVER_TURN_RIGHT)
        {
            strcpy(banner_text, "Turn right");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_ARROW_FORWARD;
        }
        else if (turn.type == MANEUVER_UTURN)
        {
            strcpy(banner_text, "Make U-turn");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_REFRESH;
        }
        else
        {
            strcpy(banner_text, "Continue");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_ARROW_UPWARD;
        }
        snprintf(turn_dist_str, sizeof(turn_dist_str), "%.0f m", progress.distance_to_next_turn);
    }
    else if (progress.distance_to_next_turn < 20 || turn.type == MANEUVER_ARRIVE)
    {
        strcpy(banner_text, "Arriving");
        strcpy(banner_sub_text, "At destination");
        strcpy(turn_dist_str, "Now");
        icon_code = AROMA_ICON_PLACE;
    }
    else
    {
        strcpy(banner_text, "Continue straight");
        strcpy(banner_sub_text, "");
        strcpy(turn_dist_str, "--");
        icon_code = AROMA_ICON_ARROW_UPWARD;
    }

    aroma_icon_set_text(nav_turn_icon, icon_code, s_icon_font);
    aroma_label_set_text(nav_banner_label, banner_text);
    aroma_label_set_text(nav_banner_sub, banner_sub_text);

    char eta_str[32], dist_str[32], speed_str[32];
    snprintf(eta_str, sizeof(eta_str), "%.0f min", progress.time_to_destination / 60.0);
    snprintf(dist_str, sizeof(dist_str), "%.1f km", progress.distance_to_destination / 1000.0);
    snprintf(speed_str, sizeof(speed_str), "%.0f km/h", map_nav.speed);
    aroma_label_set_text(nav_eta_label, eta_str);
    aroma_label_set_text(nav_dist_label, dist_str);
    aroma_label_set_text(nav_speed_label, speed_str);
    aroma_label_set_text(nav_turn_dist_label, turn_dist_str);
}

static void clear_navigation(void)
{
    map_nav.active = false;
    map_nav.navigation_active = false;
    map_nav.simulation_started = false;
    map_nav.route_ready = false;
    if (map_nav.path_lat)
    {
        free(map_nav.path_lat);
        map_nav.path_lat = NULL;
    }
    if (map_nav.path_lon)
    {
        free(map_nav.path_lon);
        map_nav.path_lon = NULL;
    }
    map_nav.route_point_count = 0;
    aroma_map_clear_route(s_map_node);
    aroma_map_clear_markers(s_map_node);
    hide_route_panel();
    aroma_map_set_center(s_map_node, 36.8625f, 10.1956f);
    aroma_map_set_zoom(s_map_node, 18);

    if (nav_banner_card)
    {
        aroma_node_set_hidden(nav_banner_card, true);
        aroma_node_set_hidden(nav_bottom_card, true);
    }
}



double calculate_distance_km(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                   sin(dlon / 2) * sin(dlon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS * c;
}



void opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(s_map_node);
    if (!maps_rect)
        return;

    int start_y = WIN_H;
    int end_y = 0;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

    maps_rect->x = rect->x;
    maps_rect->y = rect->y;
    maps_rect->width = rect->width;
    maps_rect->height = rect->height;

    if (progress >= 0.92f)
    {
        aroma_node_set_hidden(s_map_close_btn, false);
        aroma_node_set_hidden(map_search_surface, false);
    }
    aroma_node_invalidate(s_map_node);
    aroma_node_invalidate(target);
}





#include "aroma_package.h"

static AromaAppPlugin s_nav_mirror;

static bool nav_hook_init(const AromaPackageManifest *manifest,
                          const char *install_dir,
                          const AromaPackageHost *host,
                          struct AromaNode *app_root)
{
    (void)manifest;
    (void)app_root;
    if (host)
    {
        s_ui_font = host->ui_font;
        s_icon_font = host->icon_font;
        s_settings_font = host->settings_font ? host->settings_font
                                              : host->ui_font;
    }

    if (install_dir)
        snprintf(s_install_dir, sizeof(s_install_dir), "%s", install_dir);
    else
        s_install_dir[0] = '\0';
    memset(&s_nav_mirror, 0, sizeof(s_nav_mirror));
    s_nav_mirror.id = "com.aroma.nav";
    s_nav_mirror.name = "Navigation";


    maps_screen_open = false;
    poi_query_in_flight = false;
    selecting_from = false;
    search_results_visible = false;
    map_search_expanded = false;
    map_options_visible = false;
    return nav_app_init(&s_nav_mirror);
}

static bool nav_hook_build_ui(struct AromaNode *app_root)
{
    s_nav_mirror.app_root = app_root;
    return nav_app_build_ui(&s_nav_mirror, app_root);
}

static bool nav_hook_show(struct AromaNode *app_root)
{
    s_nav_mirror.app_root = app_root;
    return nav_app_show(&s_nav_mirror, app_root);
}

static void nav_hook_hide(struct AromaNode *app_root)
{
    s_nav_mirror.app_root = app_root;
    nav_app_hide(&s_nav_mirror);
}

static void nav_hook_update(struct AromaNode *app_root)
{
    (void)app_root;
    nav_app_update(&s_nav_mirror);
}

static void nav_hook_destroy(void)
{




    if (filtered_pois)
    {
        free(filtered_pois);
        filtered_pois = NULL;
    }
    filtered_poi_count = 0;
    if (map_nav.path_lat)
    {
        free(map_nav.path_lat);
        map_nav.path_lat = NULL;
    }
    if (map_nav.path_lon)
    {
        free(map_nav.path_lon);
        map_nav.path_lon = NULL;
    }
    memset(&map_nav, 0, sizeof(map_nav));
    s_map_node = NULL;
    s_map_close_btn = NULL;
    map_route_sheet = NULL;
    map_end_nav_btn = NULL;
    map_distance_label = NULL;
    map_time_label = NULL;
    map_route_dest_label = NULL;
    map_search_expanded = false;
    map_search_results_list = NULL;
    search_results_visible = false;
    focused_entry = 0;
    poi_refresh_forced = false;
    map_options_visible = false;
    poi_query_in_flight = false;
    selecting_from = false;
    map_options_card = NULL;
    map_search_surface = NULL;
    map_search_placeholder_label = NULL;
    map_search_back_btn = NULL;
    maps_screen_open = false;
    map_from_entry = NULL;
    map_to_entry = NULL;
    map_go_btn = NULL;
    nav_banner_card = NULL;
    nav_turn_icon = NULL;
    nav_banner_label = NULL;
    nav_banner_sub = NULL;
    nav_eta_label = NULL;
    nav_dist_label = NULL;
    nav_speed_label = NULL;
    nav_turn_dist_label = NULL;
    nav_bottom_card = NULL;
    map_geocode_result_count = 0;
    last_search_query[0] = '\0';
    for (int i = 0; i < ITEMS_PER_PAGE; i++)
    {
        suggestion_cards[i] = NULL;
        suggestion_name_labels[i] = NULL;
        suggestion_desc_labels[i] = NULL;
        suggestion_pick_buttons[i] = NULL;
    }
    suggestion_page = NULL;
    page_label_suggestions = NULL;
    current_page = 0;
    total_pages_suggestions = 1;
    last_center_lat = 0.0;
    last_center_lon = 0.0;
    last_zoom = 0.0;
    poi_update_counter = 0;
    memset(&s_nav_mirror, 0, sizeof(s_nav_mirror));
}

static const AromaPackageHooks s_nav_hooks = {
    .init = nav_hook_init,
    .build_ui = nav_hook_build_ui,
    .show = nav_hook_show,
    .hide = nav_hook_hide,
    .update = nav_hook_update,
    .destroy = nav_hook_destroy,
};

const AromaPackageHooks *aroma_package_entry(void)
{
    return &s_nav_hooks;
}


