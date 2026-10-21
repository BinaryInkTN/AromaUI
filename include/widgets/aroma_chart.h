#ifndef AROMA_CHART_H
#define AROMA_CHART_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"

/* Box the plot sits in; exposed so markup can override the inner margins
   without the loader having to restate them. */
#ifndef AROMA_CHART_PAD_LEFT
#define AROMA_CHART_PAD_LEFT 34
#define AROMA_CHART_PAD_TOP 8
#define AROMA_CHART_PAD_RIGHT 10
#define AROMA_CHART_PAD_BOTTOM 22
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /* Line chart rendered through the vendored pbPlots C library
     * (vendors/pbplots, MIT).
     *
     * pbPlots rasterises only the data series: it draws into an RGBA buffer
     * sized to the plot area * device density (aroma_font_raster_scale), so
     * the texture lands 1:1 on the backing store and stays crisp through the
     * web design-fit scaling. Its own axis text, grid and border are dropped
     * (see src/widgets/aroma_chart.c) and AromaUI draws those in design units
     * with the real font, so typography and theme colours come from here. */
    AromaNode *aroma_ui_chart(AromaNode *parent, int x, int y, int width, int height);

    /* Data. Arrays are copied. `xs` may be NULL to get 0, 1, 2, ... n-1.
       The rendered range follows the data until aroma_chart_set_range is
       called with an explicit axis. */
    void aroma_chart_set_series(AromaNode *node, const double *xs, const double *ys, size_t count);
    void aroma_chart_set_series_csv(AromaNode *node, const char *xs_csv, const char *ys_csv);

    /* Explicit data ranges. auto_* recomputes from the series with headroom. */
    void aroma_chart_set_range(AromaNode *node, bool auto_x, double x_min, double x_max,
                               bool auto_y, double y_min, double y_max);

    /* 0xAARRGGBB (alpha 0 = opaque, as elsewhere in AromaUI). Anything left
       unset follows the global theme and is re-read on every draw, so a theme
       switch re-themes the chart with the rest of the UI. */
    void aroma_chart_set_line_color(AromaNode *node, uint32_t color);
    void aroma_chart_set_grid_color(AromaNode *node, uint32_t color);
    void aroma_chart_set_frame_color(AromaNode *node, uint32_t color);
    void aroma_chart_set_text_color(AromaNode *node, uint32_t color);
    /* Stroke width in design px (scaled to device px inside the raster). */
    void aroma_chart_set_line_thickness(AromaNode *node, float thickness);

    /* Inner margins reserved for AromaUI-drawn axis furniture, in design px. */
    void aroma_chart_set_padding(AromaNode *node, int left, int top, int right, int bottom);

    /* Comma-separated tick labels, evenly spread across each axis (so they
       line up with an explicit range). Empty/NULL removes the labels. */
    void aroma_chart_set_tick_labels(AromaNode *node, const char *x_csv, const char *y_csv);

    void aroma_chart_set_font(AromaNode *node, AromaFont *font);
    void aroma_chart_set_furniture(AromaNode *node, bool grid, bool frame);

#ifdef __cplusplus
}
#endif

#endif
