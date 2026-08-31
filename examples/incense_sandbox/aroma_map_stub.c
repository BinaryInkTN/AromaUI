#include "widgets/aroma_map.h"
#include "aroma_font.h"

AromaNode *aroma_map_create(AromaNode *parent, int x, int y, int width, int height)
{
    (void)parent; (void)x; (void)y; (void)width; (void)height;
    return NULL;
}

void aroma_map_set_center(AromaNode *node, double lat, double lon)
{
    (void)node; (void)lat; (void)lon;
}

void aroma_map_set_center_instant(AromaNode *node, double lat, double lon)
{
    (void)node; (void)lat; (void)lon;
}

void aroma_map_set_zoom(AromaNode *node, int zoom)
{
    (void)node; (void)zoom;
}

void aroma_map_set_show_attribution(AromaNode *node, bool show)
{
    (void)node; (void)show;
}

void aroma_map_add_marker(AromaNode *node, double lat, double lon, uint32_t color)
{
    (void)node; (void)lat; (void)lon; (void)color;
}

void aroma_map_add_icon_marker_with_font(AromaNode *node, double lat, double lon, uint32_t color, const char *icon_code, AromaFont *icon_font)
{
    (void)node; (void)lat; (void)lon; (void)color; (void)icon_code; (void)icon_font;
}

void aroma_map_add_popup_marker(AromaNode *node, double lat, double lon, uint32_t color, const char *popup_text)
{
    (void)node; (void)lat; (void)lon; (void)color; (void)popup_text;
}
