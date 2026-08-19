#include "widgets/aroma_gauge.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <math.h>
#include <stdio.h>

#ifndef AROMA_GAUGE_TICK_DOWN
#define AROMA_GAUGE_TICK_DOWN 0
#define AROMA_GAUGE_TICK_UP 1
#define AROMA_GAUGE_TICK_STRAIGHT 2
#endif

typedef struct AromaGauge
{
    AromaRect rect;
    float value;
    float min_val;
    float max_val;
    uint32_t track_color;
    uint32_t fill_color;
    float start_angle;
    float end_angle;
    int track_thickness;
    int fill_thickness;
    bool has_needle;
    uint32_t needle_color;
    int needle_thickness;

    bool has_ticks;
    int major_tick_count;
    int minor_tick_count;
    int major_tick_length;
    int minor_tick_length;
    uint32_t tick_color;
    int tick_thickness;
    int tick_direction;

    bool has_labels;
    int label_start_val;
    int label_step;
    int label_count;
    AromaFont *label_font;
    uint32_t label_color;
    int label_radius_offset;

    bool has_hub;
    int hub_radius;
    uint32_t hub_color;
    int hub_thickness;
    int hub_gap;

    float red_zone_val;

    bool has_inner_ring;
    int inner_ring_offset;
    uint32_t inner_ring_color;
    int inner_ring_thickness;
} AromaGauge;

static void aroma_gauge_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(node))
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->draw_arc)
        return;

    int cx = gauge->rect.x + gauge->rect.width / 2;
    int cy = gauge->rect.y + gauge->rect.height / 2;
    int radius = (gauge->rect.width < gauge->rect.height ? gauge->rect.width : gauge->rect.height) / 2;

    int base_radius = radius - 50;
    int track_inner_edge = base_radius - (gauge->track_thickness / 2);
    int inner_ring_radius = gauge->has_inner_ring ? base_radius - gauge->inner_ring_offset : 0;

    if (gfx->draw_arc && gauge->track_thickness > 0)
    {
        gfx->draw_arc(window_id, cx, cy, base_radius, gauge->start_angle, gauge->end_angle, gauge->track_color, gauge->track_thickness);
    }

    if (gauge->red_zone_val > 0.0f && gfx->draw_arc)
    {
        float red_start_normalized = (gauge->red_zone_val - gauge->min_val) / (gauge->max_val - gauge->min_val);
        if (red_start_normalized < 0.0f) red_start_normalized = 0.0f;
        if (red_start_normalized > 1.0f) red_start_normalized = 1.0f;
        float red_start_angle = gauge->start_angle + red_start_normalized * (gauge->end_angle - gauge->start_angle);
        
        gfx->draw_arc(window_id, cx, cy, base_radius, red_start_angle, gauge->end_angle, 0xFFFF1111, gauge->track_thickness + 2);
    }

    if (gauge->has_inner_ring && gfx->draw_arc)
    {
        int inner_r = base_radius - gauge->inner_ring_offset;
        if (inner_r > 0)
        {
            gfx->draw_arc(window_id, cx, cy, inner_r, gauge->start_angle, gauge->end_angle, gauge->inner_ring_color, gauge->inner_ring_thickness);
        }
    }

    if (gauge->has_ticks && gfx->draw_line && gauge->major_tick_count > 1)
    {
        int total_intervals = (gauge->major_tick_count - 1) * (gauge->minor_tick_count + 1);
        float angle_step = (gauge->end_angle - gauge->start_angle) / total_intervals;

        for (int i = 0; i <= total_intervals; i++)
        {
            float angle = gauge->start_angle + i * angle_step;
            bool is_major = (i % (gauge->minor_tick_count + 1) == 0);
            int tick_len = is_major ? gauge->major_tick_length : gauge->minor_tick_length;

            int outer_r, inner_r;

            if (gauge->tick_direction == AROMA_GAUGE_TICK_UP)
            {
                inner_r = track_inner_edge;
                outer_r = track_inner_edge + tick_len;
            }
            else if (gauge->tick_direction == AROMA_GAUGE_TICK_STRAIGHT)
            {
                inner_r = track_inner_edge - (tick_len / 2);
                outer_r = track_inner_edge + (tick_len / 2);
            }
            else
            {
                outer_r = track_inner_edge;
                inner_r = track_inner_edge - tick_len;
            }

            int min_allowed_r = inner_ring_radius > 0 ? inner_ring_radius + gauge->inner_ring_thickness : 0;
            if (inner_r < min_allowed_r)
                inner_r = min_allowed_r;
            if (outer_r < min_allowed_r)
                continue;

            int outer_x = cx + (int)(cos(angle) * outer_r);
            int outer_y = cy + (int)(sin(angle) * outer_r);
            int inner_x = cx + (int)(cos(angle) * inner_r);
            int inner_y = cy + (int)(sin(angle) * inner_r);

            int thick = is_major ? gauge->tick_thickness : (gauge->tick_thickness > 1 ? gauge->tick_thickness - 1 : 1);

            gfx->draw_line(window_id, inner_x, inner_y, outer_x, outer_y, gauge->tick_color, thick, false);
        }
    }

    if (gauge->has_labels && gfx->render_text && gauge->label_font && gauge->label_count > 1)
    {
        float angle_step = (gauge->end_angle - gauge->start_angle) / (gauge->label_count - 1);
        int total_intervals = gauge->has_ticks ? ((gauge->major_tick_count - 1) * (gauge->minor_tick_count + 1)) : 0;

        float text_height = gfx->measure_text(window_id, gauge->label_font, "8", 1.0f) * 1.5f;

        for (int i = 0; i < gauge->label_count; i++)
        {
            float angle = gauge->start_angle + i * angle_step;
            int val = gauge->label_start_val + i * gauge->label_step;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", val);

            AromaFont *current_font = gauge->label_font;

            int tick_len = gauge->major_tick_length;
            if (gauge->has_ticks && total_intervals > 0)
            {
                int tick_index = i * total_intervals / (gauge->label_count - 1);
                bool sits_on_major_tick = (tick_index % (gauge->minor_tick_count + 1) == 0);
                tick_len = sits_on_major_tick ? gauge->major_tick_length : gauge->minor_tick_length;
            }
            else if (!gauge->has_ticks)
            {
                tick_len = 0;
            }

            int label_radius;

            if (gauge->tick_direction == AROMA_GAUGE_TICK_UP)
            {
                label_radius = track_inner_edge + tick_len + gauge->label_radius_offset;
            }
            else if (gauge->tick_direction == AROMA_GAUGE_TICK_STRAIGHT)
            {
                label_radius = track_inner_edge - (tick_len / 2) - gauge->label_radius_offset;
            }
            else
            {
                label_radius = track_inner_edge - tick_len - gauge->label_radius_offset;
            }

            float text_width = gfx->measure_text(window_id, current_font, buf, 1.0f);

            int anchor_x = cx + (int)(cos(angle) * label_radius);
            int anchor_y = cy + (int)(sin(angle) * label_radius);

            int lx = anchor_x - (int)((text_width / 2.0f) * (1.0f + cos(angle)));
            int ly = anchor_y - (int)((text_height / 2.0f) * (1.0f + sin(angle))) + (int)(text_height / 4.0f);

            uint32_t c = gauge->label_color;
            if (gauge->red_zone_val != 0.0f && val >= gauge->red_zone_val)
            {
                c = 0xFFFF1111;
            }

            gfx->render_text(window_id, current_font, buf, lx, ly, c, 1.0f);
        }
    }

    if (gfx->draw_arc && gauge->fill_thickness > 0)
    {
        float normalized = (gauge->value - gauge->min_val) / (gauge->max_val - gauge->min_val);
        if (normalized < 0.0f)
            normalized = 0.0f;
        if (normalized > 1.0f)
            normalized = 1.0f;
        float fill_end = gauge->start_angle + normalized * (gauge->end_angle - gauge->start_angle);
        gfx->draw_arc(window_id, cx, cy, base_radius, gauge->start_angle, fill_end, gauge->fill_color, gauge->fill_thickness);
    }

    if (gauge->has_needle && gfx->draw_line)
    {
        float normalized = (gauge->value - gauge->min_val) / (gauge->max_val - gauge->min_val);
        if (normalized < 0.0f)
            normalized = 0.0f;
        if (normalized > 1.0f)
            normalized = 1.0f;
        float fill_end = gauge->start_angle + normalized * (gauge->end_angle - gauge->start_angle);

        int needle_start_r = inner_ring_radius > 0 ? inner_ring_radius : 0;

        int tip_x = cx + (int)(cos(fill_end) * (base_radius));
        int tip_y = cy + (int)(sin(fill_end) * (base_radius));
        int base_x = cx + (int)(cos(fill_end) * needle_start_r);
        int base_y = cy + (int)(sin(fill_end) * needle_start_r);

        gfx->draw_line(window_id, base_x, base_y, tip_x, tip_y, gauge->needle_color, gauge->needle_thickness, true);
    }
}

static void aroma_gauge_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr)
    {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
    __destroy_node(node);
}

AromaNode *aroma_ui_gauge(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
        return NULL;

    AromaGauge *gauge = (AromaGauge *)aroma_widget_alloc(sizeof(AromaGauge));
    if (!gauge)
        return NULL;

    AromaTheme theme = aroma_theme_get_global();
    gauge->rect.x = x;
    gauge->rect.y = y;
    gauge->rect.width = width;
    gauge->rect.height = height;

    gauge->value = 0.0f;
    gauge->min_val = 0.0f;
    gauge->max_val = 1.0f;

    gauge->track_color = aroma_color_blend(theme.colors.surface, theme.colors.border, 0.45f);
    gauge->fill_color = theme.colors.primary;

    gauge->start_angle = 2.35619f;
    gauge->end_angle = 7.06858f;

    gauge->track_thickness = 10;
    gauge->fill_thickness = 10;
    gauge->has_needle = false;
    gauge->needle_color = 0xFFFFFFFF;
    gauge->needle_thickness = 4;

    gauge->has_ticks = false;
    gauge->tick_direction = AROMA_GAUGE_TICK_DOWN;

    gauge->has_labels = false;
    gauge->has_hub = false;
    gauge->has_inner_ring = false;

    gauge->hub_gap = 6;
    gauge->red_zone_val = 0.0f;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, gauge);
    if (!node)
    {
        aroma_widget_free(gauge);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_gauge_draw);
    node->destroy_cb = aroma_gauge_destroy;

    return node;
}

void aroma_gauge_set_value(AromaNode *node, float value)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->value = value;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_range(AromaNode *node, float min_val, float max_val)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->min_val = min_val;
    gauge->max_val = max_val;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_colors(AromaNode *node, uint32_t track_color, uint32_t fill_color)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->track_color = track_color;
    gauge->fill_color = fill_color;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_angles(AromaNode *node, float start_angle, float end_angle)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->start_angle = start_angle;
    gauge->end_angle = end_angle;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_thickness(AromaNode *node, int track_thickness, int fill_thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->track_thickness = track_thickness;
    gauge->fill_thickness = fill_thickness;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_needle(AromaNode *node, bool enable, uint32_t color, int thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->has_needle = enable;
    gauge->needle_color = color;
    gauge->needle_thickness = thickness;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_ticks(AromaNode *node, bool enable, int major_count, int minor_count,
                           int major_length, int minor_length, uint32_t color, int thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->has_ticks = enable;
    gauge->major_tick_count = major_count;
    gauge->minor_tick_count = minor_count;
    gauge->major_tick_length = major_length;
    gauge->minor_tick_length = minor_length;
    gauge->tick_color = color;
    gauge->tick_thickness = thickness;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_tick_direction(AromaNode *node, int direction)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->tick_direction = direction;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_labels_numeric(AromaNode *node, bool enable, int start_val, int step, int count,
                                    AromaFont *font, uint32_t color, int radius_offset)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->has_labels = enable;
    gauge->label_start_val = start_val;
    gauge->label_step = step;
    gauge->label_count = count;
    gauge->label_font = font;
    gauge->label_color = color;
    gauge->label_radius_offset = radius_offset;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_hub(AromaNode *node, bool enable, int radius, uint32_t color, int thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->has_hub = enable;
    gauge->hub_radius = radius;
    gauge->hub_color = color;
    gauge->hub_thickness = thickness;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_hub_gap(AromaNode *node, int gap)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->hub_gap = gap;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_inner_ring(AromaNode *node, bool enable, int radius_offset, uint32_t color, int thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->has_inner_ring = enable;
    gauge->inner_ring_offset = radius_offset;
    gauge->inner_ring_color = color;
    gauge->inner_ring_thickness = thickness;
    aroma_node_invalidate(node);
}

void aroma_gauge_set_red_zone(AromaNode *node, float threshold_val)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaGauge *gauge = (AromaGauge *)node->node_widget_ptr;
    gauge->red_zone_val = threshold_val;
    aroma_node_invalidate(node);
}