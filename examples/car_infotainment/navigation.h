#ifndef NAVIGATION_H
#define NAVIGATION_H

#include "aroma.h"
#include "navigation_geo.h"

#define POI_QUERY_MIN_INTERVAL_MS 400.0
#define POI_QUERY_MOVE_THRESHOLD_DEG 0.0008
#define POI_QUERY_ZOOM_THRESHOLD 0.25
#define SEARCH_DEBOUNCE_US 350000
#define EARTH_RADIUS 6371.0
#define MAX_SUGGESTIONS 512
#define NUM_POI_CATEGORIES 14
#define ITEMS_PER_PAGE 6
#define OFF_ROUTE_THRESHOLD_M 55.0
#define OFF_ROUTE_CONFIRM_FRAMES 20
#define RE_ROUTE_COOLDOWN_FRAMES 120

typedef struct
{
    bool active;
    double from_lat, from_lon;
    double to_lat, to_lon;
    char from_text[256];
    char to_text[256];
    double distance_km;
    int eta_minutes;
    bool navigation_active;
    bool route_ready;
    bool simulation_started;
    int frame;
    int seg_index;
    double seg_progress_m;
    double seg_length_m;
    double *path_lat;
    double *path_lon;
    int route_point_count;
    double display_heading;
    bool have_heading;
    double speed;
    double current_lat, current_lon;
    int off_route_counter;
    int reroute_cooldown_frames;
} NavigationState;

typedef enum
{
    OTA_STATE_IDLE,
    OTA_START,
    OTA_RUN,
    OTA_SUCCESS,
    OTA_FAILURE,
    OTA_DOWNLOAD,
    OTA_DONE,
    OTA_SUBPROCESS,
    OTA_PROGRESS,
} OtaProgressStatus;

typedef struct
{
    unsigned int magic;
    unsigned int status;
    unsigned int dwl_percent;
    unsigned long long dwl_bytes;
    unsigned int nsteps;
    unsigned int cur_step;
    unsigned int cur_percent;
    char cur_image[256];
    char hnd_name[64];
    int source;
    unsigned int infolen;
    char info[2048];
} SwupdateProgressMsg;

void update_pois_markers(void);
void populate_suggestion_cards(void);
void update_suggestions(const char *query);
void perform_map_search(const char *query);
void on_geocode_results(GeocodeResult *results, int count, void *user_data);
void show_route_panel(void);
void hide_route_panel(void);
void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon);
void clear_navigation(void);
void update_navigation_display(void);
bool open_maps(AromaNode *node, void *user_data);
void close_maps(void *user_data);
bool recalculate_route_from_current_position(void);

extern NavigationState map_nav;
extern AromaNode *map_search_surface;
extern AromaNode *map_search_placeholder_label;
extern AromaNode *map_search_back_btn;
extern bool map_search_expanded;
extern bool maps_screen_open;
extern AromaNode *map_from_entry;
extern AromaNode *map_to_entry;
extern AromaNode *map_swap_btn;
extern AromaNode *map_go_btn;
extern AromaNode *map_route_sheet;
extern AromaNode *map_distance_label;
extern AromaNode *map_time_label;
extern AromaNode *map_route_dest_label;
extern AromaNode *map_end_nav_btn;
extern AromaNode *nav_banner_card;
extern AromaNode *nav_turn_icon;
extern AromaNode *nav_banner_label;
extern AromaNode *nav_banner_sub;
extern AromaNode *nav_eta_label;
extern AromaNode *nav_dist_label;
extern AromaNode *nav_speed_label;
extern AromaNode *nav_turn_dist_label;
extern AromaNode *nav_bottom_card;
extern AromaNode *map_search_results_list;
extern GeocodeResult map_geocode_results[MAX_GEOCODE_RESULTS];
extern int map_geocode_result_count;
extern char last_search_query[256];
extern pthread_mutex_t search_mutex;
extern bool search_results_visible;
extern int focused_entry;
extern pthread_mutex_t debounce_mutex;
extern unsigned long search_generation;
extern char pending_search_query[256];
extern int pending_search_focused_entry;
extern AromaNode *suggestion_page;
extern AromaNode *page_label_suggestions;
extern PointOfInterest *filtered_pois;
extern int filtered_poi_count;
extern bool selecting_from;
extern bool category_enabled[NUM_POI_CATEGORIES];
extern int current_page;
extern int total_pages_suggestions;
extern double last_center_lat;
extern double last_center_lon;
extern double last_zoom;
extern int poi_update_counter;
extern bool poi_refresh_forced;
extern AromaNode *map_options_card;
extern bool map_options_visible;

#endif
