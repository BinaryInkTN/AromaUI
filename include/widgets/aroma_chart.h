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

/* Upper bound on simultaneous series. The markup `series:` property accepts
   the same number; anything beyond it is ignored. */
#define AROMA_CHART_MAX_SERIES 16

#ifdef __cplusplus
extern "C"
{
#endif

    /* What pbPlots draws for the data: a polyline, unconnected points, both,
       or grouped bars. */
    typedef enum AromaChartType
    {
        AROMA_CHART_LINE = 0,
        AROMA_CHART_SCATTER,
        AROMA_CHART_LINE_POINTS,
        AROMA_CHART_BAR
    } AromaChartType;

    /* pbPlots line styles (its `lineType` strings). */
    typedef enum AromaChartLineStyle
    {
        AROMA_CHART_SOLID = 0,
        AROMA_CHART_DASHED,
        AROMA_CHART_DOTTED,
        AROMA_CHART_DOTDASH,
        AROMA_CHART_LONGDASH,
        AROMA_CHART_TWODASH
    } AromaChartLineStyle;

    /* pbPlots point markers (its `pointType` strings). */
    typedef enum AromaChartPointType
    {
        AROMA_CHART_POINT_NONE = 0,
        AROMA_CHART_POINT_CROSSES,
        AROMA_CHART_POINT_CIRCLES,
        AROMA_CHART_POINT_DOTS,
        AROMA_CHART_POINT_TRIANGLES,
        AROMA_CHART_POINT_FILLED_TRIANGLES,
        AROMA_CHART_POINT_PIXELS,
        AROMA_CHART_POINT_DOT_LINE_TO_AXIS
    } AromaChartPointType;

    /* Charts rendered through the vendored pbPlots C library
     * (vendors/pbplots, MIT).
     *
     * pbPlots rasterises only the data series: it draws into an RGBA buffer
     * sized to the plot area * device density (aroma_font_raster_scale), so
     * the texture lands 1:1 on the backing store and stays crisp through the
     * web design-fit scaling. Its own axis text, grid and border are dropped
     * (see src/widgets/aroma_chart.c) and AromaUI draws those in design units
     * with the real font, so typography and theme colours come from here. */
    AromaNode *aroma_ui_chart(AromaNode *parent, int x, int y, int width, int height);

    /* Plot kind. Changing the type keeps the data and styles. Defaults to a
       line chart. */
    void aroma_chart_set_type(AromaNode *node, AromaChartType type);

    /* Data. Arrays are copied. `xs` may be NULL to get 0, 1, 2, ... n-1.
       `set_series` replaces every series with a single one; `add_series`
       appends. The rendered range follows the data until
       aroma_chart_set_range is called with an explicit axis. */
    void aroma_chart_set_series(AromaNode *node, const double *xs, const double *ys, size_t count);
    void aroma_chart_set_series_csv(AromaNode *node, const char *xs_csv, const char *ys_csv);
    void aroma_chart_add_series(AromaNode *node, const double *xs, const double *ys, size_t count);
    void aroma_chart_add_series_csv(AromaNode *node, const char *xs_csv, const char *ys_csv);
    void aroma_chart_clear_series(AromaNode *node);
    size_t aroma_chart_series_count(AromaNode *node);

    /* Per-series style. Indexes outside the current series list are ignored.
       Unset colours follow the theme (series 0 takes the theme primary, the
       rest use a categorical palette); unset markers default to dots for
       scatter/line+points. Bar charts require every series to have the same
       length and use the shortest one when they differ. */
    void aroma_chart_set_series_color(AromaNode *node, size_t index, uint32_t color);
    void aroma_chart_set_series_line_style(AromaNode *node, size_t index, AromaChartLineStyle style);
    void aroma_chart_set_series_point_type(AromaNode *node, size_t index, AromaChartPointType type);
    void aroma_chart_set_series_thickness(AromaNode *node, size_t index, float thickness);

    /* Explicit data ranges. auto_* recomputes from the series with headroom.
       Bar charts always include zero on the y axis. */
    void aroma_chart_set_range(AromaNode *node, bool auto_x, double x_min, double x_max,
                               bool auto_y, double y_min, double y_max);

    /* 0xAARRGGBB (alpha 0 = opaque, as elsewhere in AromaUI). Anything left
       unset follows the global theme and is re-read on every draw, so a theme
       switch re-themes the chart with the rest of the UI. `line_color` is the
       default colour for series 0. */
    void aroma_chart_set_line_color(AromaNode *node, uint32_t color);
    void aroma_chart_set_grid_color(AromaNode *node, uint32_t color);
    void aroma_chart_set_frame_color(AromaNode *node, uint32_t color);
    void aroma_chart_set_text_color(AromaNode *node, uint32_t color);
    /* Stroke width in design px (scaled to device px inside the raster). */
    void aroma_chart_set_line_thickness(AromaNode *node, float thickness);

    /* Gap between adjacent bar groups in design px. Negative keeps pbPlots'
       automatic spacing (5% of the plot width), which gets very tight for
       charts with many groups; a small positive gap keeps compact bar charts
       readable. Ignored by line and scatter types. */
    void aroma_chart_set_bar_gap(AromaNode *node, float gap);

    /* Inner margins reserved for AromaUI-drawn axis furniture, in design px. */
    void aroma_chart_set_padding(AromaNode *node, int left, int top, int right, int bottom);

    /* Comma-separated tick labels, evenly spread across each axis (so they
       line up with an explicit range). Bar charts centre each label over its
       group instead. Empty/NULL removes the labels. */
    void aroma_chart_set_tick_labels(AromaNode *node, const char *x_csv, const char *y_csv);

    void aroma_chart_set_font(AromaNode *node, AromaFont *font);
    void aroma_chart_set_furniture(AromaNode *node, bool grid, bool frame);

#ifdef __cplusplus
}
#endif

#endif
