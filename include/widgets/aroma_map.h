#ifndef AROMA_MAP_H
#define AROMA_MAP_H

#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_common.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct AromaMap
    {
        AromaRect rect;
        double center_lat;
        double center_lon;
        double zoom;
        bool is_dragging;
        int last_mouse_x;
        int last_mouse_y;
        double offset_x;
        double offset_y;
        bool show_osm_attribution;
        void *extra;
    } AromaMap;

#define MAX_GEOCODE_RESULTS 10

    typedef struct
    {
        double lat;
        double lon;
        char display_name[256];
        char category[64];
        char type[64];
    } GeocodeResult;

    // OPTIMIZED: Removed node_id (never read after loading)
    // Was 24 bytes, now 16 bytes (saves 8 bytes per node)
    typedef struct
    {
        double lat;
        double lon;
    } OSRMNode;

    // OPTIMIZED: Removed edge_id, speed, priority (never read after loading)
    // Was 40 bytes, now 32 bytes (saves 8 bytes per edge)
    typedef struct
    {
        uint32_t from_node;
        uint32_t to_node;
        double weight;
        double distance;
        bool is_roundabout;
    } OSRMEdge;

    typedef struct
    {
        OSRMNode *nodes;
        uint32_t node_count;
        OSRMEdge *edges;
        uint32_t edge_count;
        uint32_t *adjacency_offset;
        uint32_t *adjacency_list;
        double min_lat;
        double min_lon;
        double max_lat;
        double max_lon;
        bool is_loaded;
    } OSRMGraph;

    typedef enum
    {
        MANEUVER_NONE = 0,
        MANEUVER_TURN_LEFT,
        MANEUVER_TURN_RIGHT,
        MANEUVER_SLIGHT_LEFT,
        MANEUVER_SLIGHT_RIGHT,
        MANEUVER_STRAIGHT,
        MANEUVER_UTURN,
        MANEUVER_ROUNDABOUT,
        MANEUVER_ARRIVE
    } ManeuverType;

    typedef struct
    {
        ManeuverType type;
        double lat;
        double lon;
        double distance_to_turn;
        double distance_remaining;
        double time_remaining;
        char road_name[128];
        int speed_limit;
        int roundabout_exit;
    } TurnInstruction;

    typedef struct
    {
        double lat;
        double lon;
        double heading;
        double speed_kmh;
        bool has_fix;
    } GPSPosition;

    typedef struct
    {
        double total_distance_km;
        double total_time_seconds;
        double distance_to_next_turn;
        double time_to_next_turn;
        double distance_to_destination;
        double time_to_destination;
        int next_turn_index;
        int current_speed_limit;
        double last_distance_to_turn;
    } RouteProgress;

    typedef enum
    {
        POI_CATEGORY_GAS_STATION = 0,
        POI_CATEGORY_RESTAURANT,
        POI_CATEGORY_CAFE,
        POI_CATEGORY_FAST_FOOD,
        POI_CATEGORY_SHOP,
        POI_CATEGORY_SUPERMARKET,
        POI_CATEGORY_CONVENIENCE,
        POI_CATEGORY_HOTEL,
        POI_CATEGORY_BANK,
        POI_CATEGORY_ATM,
        POI_CATEGORY_PHARMACY,
        POI_CATEGORY_HOSPITAL,
        POI_CATEGORY_CLINIC,
        POI_CATEGORY_SCHOOL,
        POI_CATEGORY_UNIVERSITY,
        POI_CATEGORY_PARKING,
        POI_CATEGORY_CHARGING_STATION,
        POI_CATEGORY_CAR_REPAIR,
        POI_CATEGORY_CAR_WASH,
        POI_CATEGORY_OTHER_BUSINESS,
        POI_CATEGORY_OTHER_POIS,
        POI_CATEGORY_COUNT
    } POICategory;

    typedef struct
    {
        int64_t osm_id;
        POICategory category;
        double lat;
        double lon;
        char name[128];
        char name_original[128];
        char name_en[128];
        char name_fr[128];
        char street[128];
        char street_original[128];
        char street_en[128];
        char street_fr[128];
        char area[128];
        char area_en[128];
        char area_fr[128];
        char area_type[32];
        char city[128];
        char city_en[128];
        char city_fr[128];
        char address[256];
        char address_fr[256];
        char phone[32];
        char website[128];
        char opening_hours[64];
        char operator_name[64];
        char brand[64];
        char *fuel_types;
        bool has_fuel_types;
    } PointOfInterest;

    typedef void (*POIDrawCallback)(AromaNode *node, size_t window_id, PointOfInterest *poi, int draw_x, int draw_y, void *user_data);
    typedef bool (*POIHitTestCallback)(PointOfInterest *poi, int draw_x, int draw_y, int click_x, int click_y, void *user_data);

    AromaNode *aroma_map_create(AromaNode *parent, int x, int y, int width, int height);
    void aroma_map_destroy(AromaNode *node);
    void aroma_map_set_zoom(AromaNode *node, int zoom);
    void aroma_map_zoom_in(AromaNode *node);
    void aroma_map_zoom_out(AromaNode *node);
    void aroma_map_set_center(AromaNode *node, double lat, double lon);
    void aroma_map_pan_to(AromaNode *node, double lat, double lon);
    void aroma_map_set_center_instant(AromaNode *node, double lat, double lon);
    void aroma_map_set_show_attribution(AromaNode *node, bool show);
    void aroma_map_set_mbtiles(AromaNode *node, const char *filepath);
    void aroma_map_add_marker(AromaNode *node, double lat, double lon, uint32_t color);
    void aroma_map_add_icon_marker_with_font(AromaNode *node, double lat, double lon, uint32_t color, const char *icon_code, AromaFont *icon_font);
    void aroma_map_add_icon_popup_marker(AromaNode *node, double lat, double lon, uint32_t color,
                                         const char *icon_code, AromaFont *icon_font, const char *popup_text);
    void aroma_map_add_popup_marker(AromaNode *node, double lat, double lon, uint32_t color, const char *popup_text);
    void aroma_map_clear_markers(AromaNode *node);
    void aroma_map_set_route(AromaNode *node, double start_lat, double start_lon, double end_lat, double end_lon, uint32_t route_color);
    void aroma_map_clear_route(AromaNode *node);
    void aroma_map_geocode_search(AromaNode *node, const char *query, void (*callback)(GeocodeResult *results, int count, void *user_data), void *user_data);
    void aroma_map_set_animations_enabled(AromaNode *node, bool enabled);
    bool aroma_map_get_animations_enabled(AromaNode *node);
    bool aroma_map_load_osrm_data(AromaNode *node, const char *binary_file);
    void aroma_map_unload_osrm_data(AromaNode *node);
    void aroma_map_set_route_offline(AromaNode *node, double start_lat, double start_lon, double end_lat, double end_lon, uint32_t route_color);
    bool aroma_map_is_osrm_loaded(AromaNode *node);
    uint32_t aroma_map_get_osrm_node_count(AromaNode *node);
    uint32_t aroma_map_get_osrm_edge_count(AromaNode *node);
    void aroma_map_set_gps_position(AromaNode *node, double lat, double lon, double heading, double speed_kmh);
    void aroma_map_get_route_progress(AromaNode *node, RouteProgress *progress);
    void aroma_map_get_next_turn(AromaNode *node, TurnInstruction *turn);
    int aroma_map_get_route_points(AromaNode *node, double **lats, double **lons);
    bool aroma_map_load_poi_database(AromaNode *node, const char *db_path);
    void aroma_map_unload_poi_database(AromaNode *node);
    void aroma_map_set_pois_visible(AromaNode *node, bool visible);
    void aroma_map_set_poi_categories_visible(AromaNode *node, POICategory *categories, int count);
    PointOfInterest *aroma_map_query_pois_in_viewport(AromaNode *node, double min_lat, double max_lat, double min_lon, double max_lon, int *count);
    PointOfInterest *aroma_map_query_pois_by_name(AromaNode *node, const char *name_query, int limit, int *count);
    const char *aroma_map_get_poi_category_name(POICategory category);
    uint32_t aroma_map_get_poi_category_color(POICategory category);
    void aroma_map_set_poi_draw_callback(AromaNode *node, POIDrawCallback callback, void *user_data);
    void aroma_map_set_poi_hit_test_callback(AromaNode *node, POIHitTestCallback callback, void *user_data);
    double aroma_map_get_zoom(AromaNode *node);
    bool aroma_map_is_poi_loading(AromaNode *node);
    
#ifdef __cplusplus
}
#endif

#endif