#include "widgets/aroma_map.h"
#include <stdint.h>
#include <stdbool.h>

AromaNode* aroma_map_create(AromaNode* parent, int x, int y, int width, int height) { (void)parent; (void)x; (void)y; (void)width; (void)height; return NULL; }
void aroma_map_destroy(AromaNode* node) { (void)node; }
void aroma_map_set_zoom(AromaNode* node, int zoom) { (void)node; (void)zoom; }
void aroma_map_zoom_in(AromaNode* node) { (void)node; }
void aroma_map_zoom_out(AromaNode* node) { (void)node; }
void aroma_map_set_center(AromaNode* node, double lat, double lon) { (void)node; (void)lat; (void)lon; }
void aroma_map_pan_to(AromaNode* node, double lat, double lon) { (void)node; (void)lat; (void)lon; }
void aroma_map_set_show_attribution(AromaNode* node, bool show) { (void)node; (void)show; }
void aroma_map_add_marker(AromaNode* node, double lat, double lon, uint32_t color) { (void)node; (void)lat; (void)lon; (void)color; }
void aroma_map_add_icon_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* icon_code) { (void)node; (void)lat; (void)lon; (void)color; (void)icon_code; }
void aroma_map_add_popup_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* popup_text) { (void)node; (void)lat; (void)lon; (void)color; (void)popup_text; }
void aroma_map_clear_markers(AromaNode* node) { (void)node; }
void aroma_map_set_route(AromaNode* node, double start_lat, double start_lon, double end_lat, double end_lon, uint32_t route_color) { (void)node; (void)start_lat; (void)start_lon; (void)end_lat; (void)end_lon; (void)route_color; }
void aroma_map_clear_route(AromaNode* node) { (void)node; }
