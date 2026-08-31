#ifndef AROMA_GAUGE_H
#define AROMA_GAUGE_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"

#ifdef __cplusplus
extern "C"
{
#endif

    AromaNode *aroma_ui_gauge(AromaNode *parent, int x, int y, int width, int height);

    void aroma_gauge_set_value(AromaNode *node, float value);

    void aroma_gauge_set_range(AromaNode *node, float min_val, float max_val);

    void aroma_gauge_set_colors(AromaNode *node, uint32_t track_color, uint32_t fill_color);

    void aroma_gauge_set_angles(AromaNode *node, float start_angle, float end_angle);

    void aroma_gauge_set_thickness(AromaNode *node, int track_thickness, int fill_thickness);

    void aroma_gauge_set_needle(AromaNode *node, bool enable, uint32_t color, int thickness);

    void aroma_gauge_set_secondary_hand(AromaNode *node, bool enable, uint32_t color, int thickness, float length_ratio);

    void aroma_gauge_set_secondary_value(AromaNode *node, float value);

    void aroma_gauge_set_extra_hand(AromaNode *node, bool enable, uint32_t color, int thickness, float length_ratio);

    void aroma_gauge_set_extra_value(AromaNode *node, float value);

    void aroma_gauge_set_ticks(AromaNode *node, bool enable, int major_count, int minor_count_per_major,
                               int major_length, int minor_length, uint32_t color, int thickness);


    void aroma_gauge_set_labels_numeric(AromaNode *node, bool enable, int start_val, int step, int count,
                                        AromaFont *major_font, uint32_t color, int radius_offset);

    void aroma_gauge_set_hub(AromaNode *node, bool enable, int radius, uint32_t color, int thickness);

    void aroma_gauge_set_inner_ring(AromaNode *node, bool enable, int radius_offset, uint32_t color, int thickness);
    void aroma_gauge_set_hub_gap(AromaNode *node, int gap);
    void aroma_gauge_set_red_zone(AromaNode *node, float threshold_val);

#ifdef __cplusplus
}
#endif
#endif