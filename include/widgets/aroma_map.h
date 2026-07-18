#ifndef AROMA_MAP_H
#define AROMA_MAP_H

#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaMap {
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
    void* extra;
} AromaMap;

#define MAX_GEOCODE_RESULTS 10

typedef struct {
    double lat;
    double lon;
    char display_name[256];
    char category[64];
    char type[64];
} GeocodeResult;

AromaNode* aroma_map_create(AromaNode* parent, int x, int y, int width, int height);
void aroma_map_destroy(AromaNode* node);
void aroma_map_set_zoom(AromaNode* node, int zoom);
void aroma_map_zoom_in(AromaNode* node);
void aroma_map_zoom_out(AromaNode* node);
void aroma_map_set_center(AromaNode* node, double lat, double lon);
void aroma_map_pan_to(AromaNode* node, double lat, double lon);
void aroma_map_set_show_attribution(AromaNode* node, bool show);
void aroma_map_add_marker(AromaNode* node, double lat, double lon, uint32_t color);
void aroma_map_add_icon_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* icon_code);
void aroma_map_add_popup_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* popup_text);
void aroma_map_clear_markers(AromaNode* node);

void aroma_map_set_route(AromaNode* node, double start_lat, double start_lon, double end_lat, double end_lon, uint32_t route_color);
void aroma_map_clear_route(AromaNode* node);

void aroma_map_geocode_search(AromaNode* node, const char* query,
                               void (*callback)(GeocodeResult* results, int count, void* user_data),
                               void* user_data);

#ifdef __cplusplus
}
#endif

#endif // AROMA_MAP_H