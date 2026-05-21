#include "widgets/aroma_gauge.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct   AromaGauge {
    AromaRect rect;
    int thickness;
    float value;
    float min;
    float max;
    uint32_t track_color;
    uint32_t indicator_color;
    bool use_theme_colors;
} AromaGauge;

AromaNode* aroma_gauge_create(AromaNode* parent, int x, int y, int radius, int thickness)
{
    if (!parent || radius <= 0 || thickness <= 0) return NULL;

    AromaGauge* gauge = (AromaGauge*)aroma_widget_alloc(sizeof(AromaGauge));
    if (!gauge) return NULL;

    AromaTheme theme = aroma_theme_get_global();
    gauge->rect.x = x - radius;
    gauge->rect.y = y - radius;
    gauge->rect.width = radius * 2;
    gauge->rect.height = radius * 2;
    gauge->thickness = thickness;
    gauge->value = 0.0f;
    gauge->min = 0.0f;
    gauge->max = 100.0f;
    
    gauge->track_color = aroma_color_blend(theme.colors.surface, theme.colors.border, 0.45f);
    gauge->indicator_color = theme.colors.primary;
    gauge->use_theme_colors = true;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, gauge);
    if (!node) {
        aroma_widget_free(gauge);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_gauge_draw);
   
    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif
   
    return node;
}

void aroma_gauge_set_value(AromaNode* gauge_node, float value, float min, float max)
{
    if (!gauge_node || !gauge_node->node_widget_ptr) return;
    AromaGauge* gauge = (AromaGauge*)gauge_node->node_widget_ptr;
    
    if (min >= max) {
        max = min + 1.0f;
    }
    
    if (value < min) value = min;
    if (value > max) value = max;
    
    gauge->value = value;
    gauge->min = min;
    gauge->max = max;
    
    aroma_node_invalidate(gauge_node);
}

float aroma_gauge_get_value(AromaNode* gauge_node)
{
    if (!gauge_node || !gauge_node->node_widget_ptr) return 0.0f;
    AromaGauge* gauge = (AromaGauge*)gauge_node->node_widget_ptr;
    return gauge->value;
}

void aroma_gauge_set_colors(AromaNode* gauge_node, uint32_t track_color, uint32_t indicator_color)
{
    if (!gauge_node || !gauge_node->node_widget_ptr) return;
    AromaGauge* gauge = (AromaGauge*)gauge_node->node_widget_ptr;
    gauge->track_color = track_color;
    gauge->use_theme_colors = false;
    gauge->indicator_color = indicator_color;
    aroma_node_invalidate(gauge_node);
}

void aroma_gauge_draw(AromaNode* gauge_node, size_t window_id)
{
    if (!gauge_node || !gauge_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(gauge_node)) return;
    
    AromaGauge* gauge = (AromaGauge*)gauge_node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->draw_arc) return;

    if (gauge->use_theme_colors) {
        AromaTheme theme = aroma_theme_get_global();
        gauge->track_color = aroma_color_blend(theme.colors.surface, theme.colors.border, 0.45f);
        gauge->indicator_color = theme.colors.primary;
    }

    int cx = gauge->rect.x + gauge->rect.width / 2;
    int cy = gauge->rect.y + gauge->rect.height / 2;
    int radius = gauge->rect.width / 2;
    
    float start_angle = (float)M_PI * 0.75f; // 135 degrees
    float end_angle = (float)M_PI * 2.25f;   // 405 degrees (or 45 degrees)
    float total_angle = end_angle - start_angle;
    
    int track_r = radius;
    int track_thickness = gauge->thickness;
    int center_r = track_r - track_thickness / 2;
    int cap_r = track_thickness / 2;

    // Draw track
    gfx->draw_arc(window_id, cx, cy, track_r, start_angle, end_angle, gauge->track_color, track_thickness);
    
    // Draw track rounded caps
    int cap1_x = cx + center_r * cosf(start_angle);
    int cap1_y = cy + center_r * sinf(start_angle);
    gfx->draw_arc(window_id, cap1_x, cap1_y, cap_r, 0.0f, (float)M_PI * 2.0f, gauge->track_color, cap_r);
    
    int cap2_x = cx + center_r * cosf(end_angle);
    int cap2_y = cy + center_r * sinf(end_angle);
    gfx->draw_arc(window_id, cap2_x, cap2_y, cap_r, 0.0f, (float)M_PI * 2.0f, gauge->track_color, cap_r);

    // Draw indicator
    float range = gauge->max - gauge->min;
    float normalized_val = (gauge->value - gauge->min) / (range != 0.0f ? range : 1.0f);
    float val_angle = start_angle + (total_angle * normalized_val);
    
    if (normalized_val > 0.0f) {
        gfx->draw_arc(window_id, cx, cy, track_r, start_angle, val_angle, gauge->indicator_color, track_thickness);
        
        // Indicator caps for a rounded modern look
        gfx->draw_arc(window_id, cap1_x, cap1_y, cap_r, 0.0f, (float)M_PI * 2.0f, gauge->indicator_color, cap_r);
        
        int ind_cap_x = cx + center_r * cosf(val_angle);
        int ind_cap_y = cy + center_r * sinf(val_angle);
        gfx->draw_arc(window_id, ind_cap_x, ind_cap_y, cap_r, 0.0f, (float)M_PI * 2.0f, gauge->indicator_color, cap_r);
    }

    // Draw car style tapered needle with fixed width calculation
    uint32_t needle_color = 0xFFF44336; // Material Red 500
    int needle_start_r = 12;
    int needle_end_r = track_r - track_thickness - 6;
    float base_width_half = 3.5f; 
    float tip_width_half = 0.5f;  

    // Build the needle dynamically layer by layer for perfect taper
    for (int r = needle_start_r; r < needle_end_r; r += 2) {
        float t = (float)(r - needle_start_r) / (float)(needle_end_r - needle_start_r);
        float current_width_half = base_width_half * (1.0f - t) + tip_width_half * t;
        float angle_hw = current_width_half / (float)r;
        gfx->draw_arc(window_id, cx, cy, r + 2, val_angle - angle_hw, val_angle + angle_hw, needle_color, 2);
    }
    
    // Needle tip smooth cap
    int tip_x = cx + needle_end_r * cosf(val_angle);
    int tip_y = cy + needle_end_r * sinf(val_angle);
    gfx->draw_arc(window_id, tip_x, tip_y, (int)(tip_width_half + 1.0f), 0.0f, (float)M_PI * 2.0f, needle_color, (int)(tip_width_half + 1.0f));

    // Modern Center cap
    gfx->draw_arc(window_id, cx, cy, 14, 0.0f, (float)M_PI * 2.0f, 0xFFE0E0E0, 14); // Outer bezel
    gfx->draw_arc(window_id, cx, cy, 10, 0.0f, (float)M_PI * 2.0f, 0xFF212121, 10); // Inner dark core
    gfx->draw_arc(window_id, cx, cy, 3, 0.0f, (float)M_PI * 2.0f, 0xFF555555, 3);   // Highlight
}

void aroma_gauge_destroy(AromaNode* gauge_node)
{
    if (!gauge_node) return;
    if (gauge_node->node_widget_ptr) {
        aroma_widget_free(gauge_node->node_widget_ptr);
        gauge_node->node_widget_ptr = NULL;
    }
    __destroy_node(gauge_node);
}