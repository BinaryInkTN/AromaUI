/* Charts on top of the vendored pbPlots library (vendors/pbplots, MIT).
 *
 * Division of labour: pbPlots rasterises the data series into an RGBA buffer
 * sized to the plot area * device density (aroma_font_raster_scale), so the
 * texture lands exactly device-resolution and stays sharp through the docs
 * design-fit scaling, the same invariant the glyph atlas follows. Everything
 * AromaUI draws itself — grid, frame and axis text — is drawn in design units
 * with a real font, so typography and theme colours come from the toolkit.
 *
 * pbPlots can draw polylines, point clouds, both, and grouped bars; the three
 * share the same raster pipeline and differ only in which of its settings
 * objects is filled in. Line + points is two overlaid series (pbPlots never
 * draws markers and interpolation for one series).
 *
 * Dropping pbPlots' own chrome needs three tricks (all local to this file, the
 * vendored sources stay untouched):
 *   - its borders/grid are painted with a white gridColor, and every other
 *     piece of chrome (tick marks, the zero axis, the title, bar labels) in
 *     solid black or 50% grey; the rasteriser is aliased (Bresenham +
 *     DrawPixel), so those land as exactly 0, 128 or 255 in all channels and
 *     can be keyed out to alpha 0;
 *   - pinning the axes onto the range corners (xAxisAuto/yAxisAuto off) puts
 *     every tick label and axis title outside the bitmap, where SetPixel
 *     drops them, so its built-in 8px font never reaches the screen;
 *   - every series colour is nudged off those three chrome values so the key
 *     can't eat a data point.
 */

#include "widgets/aroma_chart.h"

#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"

#include "pbPlots.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define AROMA_CHART_TICK_GAP 6 /* design px between the plot edge and tick text */

typedef struct AromaChartSeries
{
    double *xs;
    double *ys;
    size_t count;

    uint32_t color;
    bool color_set;
    AromaChartLineStyle line_style;
    AromaChartPointType point_type;
    bool point_type_set;
    float thickness;
    bool thickness_set;
} AromaChartSeries;

typedef struct AromaChart
{
    AromaRect rect;

    AromaChartType type;
    AromaChartSeries series[AROMA_CHART_MAX_SERIES];
    size_t series_count;

    bool auto_x;
    bool auto_y;
    double x_min;
    double x_max;
    double y_min;
    double y_max;

    uint32_t line_color;
    uint32_t grid_color;
    uint32_t frame_color;
    uint32_t text_color;
    bool line_color_set; /* false = follow the theme at draw time */
    bool grid_color_set;
    bool frame_color_set;
    bool text_color_set;
    float line_thickness;
    float bar_gap; /* design px between bar groups; < 0 = pbPlots auto */
    int pad_left;
    int pad_top;
    int pad_right;
    int pad_bottom;
    bool show_grid;
    bool show_frame;

    char *x_ticks; /* comma-separated labels, evenly spread over the axis */
    char *y_ticks;
    AromaFont *font; /* borrowed from the loader's sized-font registry */

    unsigned version;          /* bumped by every setter */
    unsigned rendered_version; /* version the cached raster corresponds to */
    unsigned int tex;
    int tex_w;
    int tex_h;
    float tex_k;
    uint32_t tex_hash; /* series colours + plot type baked into the raster */
} AromaChart;

/* ---------------------------------------------------------------- helpers */

/* Theme colours are resolved at draw time, the way Label does it, so a chart
 * built before a theme switch re-themes with the rest of the UI; whatever the
 * caller set explicitly wins. */
static void chart_colors(const AromaChart *chart, uint32_t *line, uint32_t *grid,
                         uint32_t *frame, uint32_t *text)
{
    AromaTheme theme = aroma_theme_get_global();
    *line = chart->line_color_set ? chart->line_color : theme.colors.primary;
    *grid = chart->grid_color_set
                ? chart->grid_color
                : aroma_color_blend(theme.colors.border, theme.colors.surface, 0.5f);
    *frame = chart->frame_color_set ? chart->frame_color : theme.colors.border;
    *text = chart->text_color_set ? chart->text_color : theme.colors.text_secondary;
}

/* Categorical colours for series 1..n; series 0 uses the theme primary (or an
 * explicit line_color), which keeps the single-series look unchanged. */
static const uint32_t s_chart_palette[] = {
    0x00E8710Au, /* orange */
    0x0000A67Cu, /* teal   */
    0x00D93025u, /* red    */
    0x009338AAu, /* purple */
    0x00F9AB00u, /* amber  */
    0x000088D7u, /* sky    */
};

static uint32_t chart_series_color(const AromaChart *chart, size_t index, uint32_t primary)
{
    if (index < chart->series_count && chart->series[index].color_set)
        return chart->series[index].color;
    if (index == 0)
        return primary;
    size_t n = sizeof(s_chart_palette) / sizeof(s_chart_palette[0]);
    return s_chart_palette[(index - 1u) % n];
}

static float chart_series_thickness(const AromaChart *chart, size_t index)
{
    float t = chart->line_thickness;
    if (index < chart->series_count && chart->series[index].thickness_set)
        t = chart->series[index].thickness;
    return t > 0.0f ? t : 1.0f;
}

static bool chart_has_data(const AromaChart *chart)
{
    for (size_t i = 0; i < chart->series_count; i++)
        if (chart->series[i].count > 0)
            return true;
    return false;
}

/* Bars share one group axis, so every series has to cover the same indices;
 * pbPlots rejects mixed lengths, so trim to the shortest here instead. */
static size_t chart_bar_groups(const AromaChart *chart)
{
    if (chart->series_count == 0)
        return 0;
    size_t groups = chart->series[0].count;
    for (size_t i = 1; i < chart->series_count; i++)
        if (chart->series[i].count < groups)
            groups = chart->series[i].count;
    return groups;
}

/* Bar spacing as fractions of the plot width. pbPlots' automatic spacing is
 * 5% between groups and 0.5% between bars in a group; a custom gap keeps
 * compact, many-group charts readable. The fractions are clamped so the
 * computed bar width never collapses. */
static void chart_bar_layout(const AromaChart *chart, double width,
                             size_t groups, size_t series_count,
                             double *group_sep, double *bar_sep, double *bar_width)
{
    *group_sep = 0.05;
    *bar_sep = 0.005;

    if (chart->bar_gap >= 0.0f && width > 0.0 && groups > 0 && series_count > 0)
    {
        *group_sep = (double)chart->bar_gap / width;
        *bar_sep = chart->bar_gap > 0.0f
                       ? fmax(1.0, (double)chart->bar_gap * 0.15) / width
                       : 0.0;
    }

    double total = *group_sep * (double)(groups ? groups - 1 : 0) +
                   *bar_sep * (double)(groups * (series_count ? series_count - 1 : 0));
    if (total > 0.9)
    {
        double scale = 0.9 / total;
        *group_sep *= scale;
        *bar_sep *= scale;
        total = 0.9;
    }

    if (groups == 0 || series_count == 0)
        *bar_width = 0.0;
    else
        *bar_width = (1.0 - total) / (double)(groups * series_count);
}

static void chart_touch(AromaNode *node, AromaChart *chart)
{
    chart->version++;
    aroma_node_invalidate(node);
}

static const char *csv_skip(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')
        p++;
    return p;
}

static size_t csv_count(const char *s)
{
    size_t n = 0;
    const char *p = csv_skip(s ? s : "");
    while (*p)
    {
        char *end = NULL;
        strtod(p, &end);
        if (end == p)
            break;
        n++;
        p = csv_skip(end);
    }
    return n;
}

/* Tick labels are free text ("6a", "Mon"), so they split on commas alone —
 * unlike the series CSVs below, which are scanned with strtod. */
static const char *label_skip(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static size_t label_count(const char *s)
{
    if (!s)
        return 0;
    size_t n = 0;
    const char *p = s;
    while (*p)
    {
        const char *end = strchr(p, ',');
        const char *tok_end = end ? end : p + strlen(p);
        const char *start = label_skip(p);
        while (tok_end > start && (tok_end[-1] == ' ' || tok_end[-1] == '\t' ||
                                   tok_end[-1] == '\r' || tok_end[-1] == '\n'))
            tok_end--;
        if (tok_end > start)
            n++;
        if (!end)
            break;
        p = end + 1;
    }
    return n;
}

/* Copies tick label `i` into `buf`; returns NULL when the list runs out. */
static const char *label_at(const char *s, size_t i, char *buf, size_t buf_len)
{
    if (!s)
        return NULL;
    size_t n = 0;
    const char *p = s;
    while (*p)
    {
        const char *end = strchr(p, ',');
        const char *tok_end = end ? end : p + strlen(p);
        const char *start = label_skip(p);
        while (tok_end > start && (tok_end[-1] == ' ' || tok_end[-1] == '\t' ||
                                   tok_end[-1] == '\r' || tok_end[-1] == '\n'))
            tok_end--;
        if (tok_end > start)
        {
            if (n == i)
            {
                size_t len = (size_t)(tok_end - start);
                if (len >= buf_len)
                    len = buf_len - 1;
                memcpy(buf, start, len);
                buf[len] = '\0';
                return buf;
            }
            n++;
        }
        if (!end)
            break;
        p = end + 1;
    }
    return NULL;
}

static void csv_fill(const char *s, double *out, size_t cap)
{
    size_t n = 0;
    const char *p = csv_skip(s ? s : "");
    while (*p && n < cap)
    {
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p)
            break;
        out[n++] = v;
        p = csv_skip(end);
    }
}

/* The range actually plotted: explicit axes pass through untouched, auto axes
 * come from all series plus headroom so the trace clears the frame. Degenerate
 * ends are widened because pbPlots silently falls back to 0..10 when they match.
 * Bars always include zero so the baseline is visible. */
static void chart_resolve_range(const AromaChart *chart,
                                double *x0, double *x1, double *y0, double *y1)
{
    bool have = false;
    double dx0 = 0.0, dx1 = 0.0, dy0 = 0.0, dy1 = 0.0;
    size_t groups = chart_bar_groups(chart);

    for (size_t s = 0; s < chart->series_count; s++)
    {
        const AromaChartSeries *ser = &chart->series[s];
        size_t count = chart->type == AROMA_CHART_BAR ? groups : ser->count;
        for (size_t i = 0; i < count; i++)
        {
            double xv = chart->type == AROMA_CHART_BAR ? (double)i : ser->xs[i];
            double yv = ser->ys[i];
            if (!have)
            {
                dx0 = dx1 = xv;
                dy0 = dy1 = yv;
                have = true;
            }
            else
            {
                if (xv < dx0) dx0 = xv;
                if (xv > dx1) dx1 = xv;
                if (yv < dy0) dy0 = yv;
                if (yv > dy1) dy1 = yv;
            }
        }
    }

    if (chart->auto_x)
    {
        if (!have)
        {
            *x0 = 0.0;
            *x1 = 1.0;
        }
        else if (chart->type == AROMA_CHART_BAR)
        {
            *x0 = 0.0;
            *x1 = groups > 0 ? (double)groups : 1.0;
        }
        else
        {
            *x0 = dx0;
            *x1 = dx1;
            double span = *x1 - *x0;
            if (span > 0.0)
            {
                *x0 -= span * 0.04;
                *x1 += span * 0.04;
            }
        }
    }
    else
    {
        *x0 = chart->x_min;
        *x1 = chart->x_max;
    }

    if (chart->auto_y)
    {
        if (!have)
        {
            *y0 = 0.0;
            *y1 = 1.0;
        }
        else if (chart->type == AROMA_CHART_BAR)
        {
            *y0 = dy0 < 0.0 ? dy0 : 0.0;
            *y1 = dy1 > 0.0 ? dy1 : 0.0;
            double span = *y1 - *y0;
            if (span <= 0.0)
            {
                *y0 = 0.0;
                *y1 = 1.0;
            }
            else
            {
                if (dy1 > 0.0) *y1 += span * 0.06;
                if (dy0 < 0.0) *y0 -= span * 0.06;
            }
        }
        else
        {
            *y0 = dy0;
            *y1 = dy1;
            double span = *y1 - *y0;
            if (span > 0.0)
            {
                *y0 -= span * 0.06;
                *y1 += span * 0.06;
            }
        }
    }
    else
    {
        *y0 = chart->y_min;
        *y1 = chart->y_max;
    }

    if (!(*x1 > *x0))
    {
        *x0 -= 1.0;
        *x1 += 1.0;
    }
    if (!(*y1 > *y0))
    {
        *y0 -= 1.0;
        *y1 += 1.0;
    }
}

/* pbPlots paints its chrome with exactly three colours: white (grid/border),
 * black (titles/bar border) and 50% grey (tick labels). Move any data colour
 * off those values so the key below cannot punch holes in a series. */
static uint32_t chart_keep_stroke(uint32_t color)
{
    uint32_t r = (color >> 16) & 0xFFu;
    uint32_t g = (color >> 8) & 0xFFu;
    uint32_t b = color & 0xFFu;
    if (r == g && g == b)
    {
        if (r == 0x00u) r = 0x01u;
        else if (r == 0xFFu) r = 0xFEu;
        else if (r == 0x80u) r = 0x81u;
        return (color & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
    return color;
}

static bool chart_near_any_stroke(uint32_t r, uint32_t g, uint32_t b,
                                  const uint32_t *strokes, size_t stroke_count)
{
    for (size_t i = 0; i < stroke_count; i++)
    {
        uint32_t sr = (strokes[i] >> 16) & 0xFFu;
        uint32_t sg = (strokes[i] >> 8) & 0xFFu;
        uint32_t sb = strokes[i] & 0xFFu;
        if ((r > sr ? r - sr : sr - r) < 4u &&
            (g > sg ? g - sg : sg - g) < 4u &&
            (b > sb ? b - sb : sb - b) < 4u)
            return true;
    }
    return false;
}

/* pbPlots packs pixels as RRGGBBAA; GL_RGBA wants bytes [r,g,b,a], which on
 * little endian is AARRGGBB — swap the pairs while keying pbPlots' background
 * and chrome out. The keyed pixels are zeroed outright rather than left as
 * white-at-alpha-0: the bitmap is drawn at device density and resampled a
 * little when the design does not divide the viewport exactly, and texture
 * filtering would otherwise bleed that white into the series as a halo. */
static void chart_key_and_swizzle(uint32_t *px, size_t n,
                                  const uint32_t *strokes, size_t stroke_count)
{
    for (size_t i = 0; i < n; i++)
    {
        uint32_t v = px[i];
        uint32_t r = (v >> 24) & 0xFFu;
        uint32_t g = (v >> 16) & 0xFFu;
        uint32_t b = (v >> 8) & 0xFFu;
        uint32_t a = v & 0xFFu;
        bool chrome = (r == 0xFFu && g == 0xFFu && b == 0xFFu) ||
                      (r == 0x00u && g == 0x00u && b == 0x00u) ||
                      (r == 0x80u && g == 0x80u && b == 0x80u);
        if (chrome && !chart_near_any_stroke(r, g, b, strokes, stroke_count))
            px[i] = 0u;
        else
            px[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
}

static const wchar_t *chart_line_type_name(AromaChartLineStyle style)
{
    switch (style)
    {
    case AROMA_CHART_DASHED: return L"dashed";
    case AROMA_CHART_DOTTED: return L"dotted";
    case AROMA_CHART_DOTDASH: return L"dotdash";
    case AROMA_CHART_LONGDASH: return L"longdash";
    case AROMA_CHART_TWODASH: return L"twodash";
    case AROMA_CHART_SOLID:
    default: return L"solid";
    }
}

static const wchar_t *chart_point_type_name(AromaChartPointType type)
{
    switch (type)
    {
    case AROMA_CHART_POINT_CROSSES: return L"crosses";
    case AROMA_CHART_POINT_CIRCLES: return L"circles";
    case AROMA_CHART_POINT_DOTS: return L"dots";
    case AROMA_CHART_POINT_TRIANGLES: return L"triangles";
    case AROMA_CHART_POINT_FILLED_TRIANGLES: return L"filled triangles";
    case AROMA_CHART_POINT_PIXELS: return L"pixels";
    case AROMA_CHART_POINT_DOT_LINE_TO_AXIS: return L"dotlinetoxaxis";
    case AROMA_CHART_POINT_NONE:
    default: return L"none";
    }
}

static AromaChartPointType chart_effective_point(const AromaChart *chart, size_t index)
{
    if (chart->type == AROMA_CHART_LINE)
        return AROMA_CHART_POINT_NONE;
    if (chart->series[index].point_type_set)
        return chart->series[index].point_type;
    /* Points are the point of these types: default to a visible marker. */
    return AROMA_CHART_POINT_DOTS;
}

/* --------------------------------------------------------------- rasterize */

static uint32_t chart_raster_hash(const AromaChart *chart, const uint32_t *colors, size_t count)
{
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)chart->type) * 16777619u;
    int32_t gap = (int32_t)(chart->bar_gap * 100.0f);
    h = (h ^ (uint32_t)gap) * 16777619u;
    for (size_t i = 0; i < count; i++)
        h = (h ^ colors[i]) * 16777619u;
    return h;
}

static void chart_rasterize_xy(RGBABitmapImageReference *ref, AromaChart *chart, int bw, int bh,
                               double x0, double x1, double y0, double y1,
                               const uint32_t *colors, float k)
{
    ScatterPlotSettings *settings = GetDefaultScatterPlotSettings();
    settings->width = (double)bw;
    settings->height = (double)bh;
    settings->autoBoundaries = false;
    settings->xMin = x0;
    settings->xMax = x1;
    settings->yMin = y0;
    settings->yMax = y1;
    settings->autoPadding = false;
    settings->xPadding = 0.0;
    settings->yPadding = 0.0;
    settings->showGrid = false;
    settings->gridColor = CreateRGBColor(1.0, 1.0, 1.0); /* keyed out below */
    /* Force the axes onto the range corners: pbPlots then measures every tick
     * label outside the bitmap (x at y+h+5, y at x-textWidth-10) and SetPixel
     * discards it, so its built-in font is never rasterised here. */
    settings->xAxisAuto = false;
    settings->xAxisBottom = true;
    settings->yAxisAuto = false;
    settings->yAxisLeft = true;
    settings->title = L"";
    settings->titleLength = 0;
    settings->xLabel = L"";
    settings->xLabelLength = 0;
    settings->yLabel = L"";
    settings->yLabelLength = 0;

    ScatterPlotSeries *list[AROMA_CHART_MAX_SERIES * 2];
    size_t list_count = 0;

    for (size_t s = 0; s < chart->series_count; s++)
    {
        const AromaChartSeries *ser = &chart->series[s];
        if (ser->count == 0)
            continue;

        const wchar_t *line_name = chart_line_type_name(ser->line_style);
        double thickness = (double)chart_series_thickness(chart, s) * (double)k;
        /* pbPlots drops a line whose thickness rounds below 1px and has a
         * separate code path per integer width, so keep the pen at least 2
         * device px: at small design-fit scales that is the hairline a chart
         * wants anyway. */
        if (thickness < 2.0)
            thickness = 2.0;
        RGBA *color = CreateRGBColor((double)((colors[s] >> 16) & 0xFFu) / 255.0,
                                     (double)((colors[s] >> 8) & 0xFFu) / 255.0,
                                     (double)(colors[s] & 0xFFu) / 255.0);

        bool want_line = chart->type == AROMA_CHART_LINE ||
                         chart->type == AROMA_CHART_LINE_POINTS;
        bool want_points = chart->type == AROMA_CHART_SCATTER ||
                           chart->type == AROMA_CHART_LINE_POINTS;

        if (want_line && list_count < sizeof(list) / sizeof(list[0]))
        {
            ScatterPlotSeries *line = GetDefaultScatterPlotSeriesSettings();
            line->xs = ser->xs;
            line->xsLength = ser->count;
            line->ys = ser->ys;
            line->ysLength = ser->count;
            line->linearInterpolation = true;
            line->lineType = (wchar_t *)line_name;
            line->lineTypeLength = wcslen(line_name);
            line->pointType = L"none";
            line->pointTypeLength = 4;
            line->lineThickness = thickness;
            line->color = color;
            list[list_count++] = line;
        }

        if (want_points && list_count < sizeof(list) / sizeof(list[0]))
        {
            const wchar_t *point_name = chart_point_type_name(chart_effective_point(chart, s));
            ScatterPlotSeries *points = GetDefaultScatterPlotSeriesSettings();
            points->xs = ser->xs;
            points->xsLength = ser->count;
            points->ys = ser->ys;
            points->ysLength = ser->count;
            points->linearInterpolation = false;
            points->lineType = L"solid";
            points->lineTypeLength = 5;
            points->pointType = (wchar_t *)point_name;
            points->pointTypeLength = wcslen(point_name);
            points->lineThickness = thickness;
            points->color = color;
            list[list_count++] = points;
        }
    }

    if (list_count == 0)
        return;

    settings->scatterPlotSeries = list;
    settings->scatterPlotSeriesLength = list_count;

    StringReference *error = CreateStringReference(L"", 0);
    /* Upstream reuses its `success` flag for per-segment clipping, so a false
     * return is routine (any segment ending outside the range) and proves
     * nothing — the canvas length below is what says the render happened. */
    (void)DrawScatterPlotFromSettings(ref, settings, error);
}

static void chart_rasterize_bar(RGBABitmapImageReference *ref, AromaChart *chart, int bw, int bh,
                                int design_w, double y0, double y1,
                                const uint32_t *colors, size_t groups)
{
    BarPlotSettings *settings = GetDefaultBarPlotSettings();
    settings->width = (double)bw;
    settings->height = (double)bh;
    settings->autoBoundaries = false;
    settings->yMin = y0;
    settings->yMax = y1;
    settings->autoPadding = false;
    settings->xPadding = 0.0;
    settings->yPadding = 0.0;
    settings->showGrid = false;
    settings->gridColor = CreateRGBColor(1.0, 1.0, 1.0); /* keyed out below */
    settings->title = L"";
    settings->titleLength = 0;
    settings->yLabel = L"";
    settings->yLabelLength = 0;
    settings->autoColor = false; /* per-series colours from AromaUI */
    settings->autoSpacing = false; /* explicit spacing keeps mini charts readable */

    BarPlotSeries *list[AROMA_CHART_MAX_SERIES];
    size_t list_count = 0;
    for (size_t s = 0; s < chart->series_count && list_count < AROMA_CHART_MAX_SERIES; s++)
    {
        const AromaChartSeries *ser = &chart->series[s];
        BarPlotSeries *bar = GetDefaultBarPlotSeriesSettings();
        bar->ys = ser->ys;
        bar->ysLength = groups;
        bar->color = CreateRGBColor((double)((colors[s] >> 16) & 0xFFu) / 255.0,
                                    (double)((colors[s] >> 8) & 0xFFu) / 255.0,
                                    (double)(colors[s] & 0xFFu) / 255.0);
        list[list_count++] = bar;
    }

    if (list_count == 0)
        return;

    double group_sep = 0.05, bar_sep = 0.005, bar_width = 0.0;
    chart_bar_layout(chart, (double)design_w, groups, list_count,
                     &group_sep, &bar_sep, &bar_width);
    settings->groupSeparation = group_sep * (double)bw;
    settings->barSeparation = bar_sep * (double)bw;

    settings->barPlotSeries = list;
    settings->barPlotSeriesLength = list_count;

    StringReference *error = CreateStringReference(L"", 0);
    (void)DrawBarPlotFromSettings(ref, settings, error);
}

/* Re-renders the pbPlots bitmap when the data, the widget size, the density,
 * the type or any series colour change; otherwise leaves the cached texture
 * alone. */
static void chart_rasterize(AromaChart *chart, int plot_w, int plot_h, uint32_t line_color)
{
    float k = aroma_font_raster_scale();
    int bw = (int)lround((double)plot_w * (double)k);
    int bh = (int)lround((double)plot_h * (double)k);
    if (bw < 2 || bh < 2)
        return;

    uint32_t colors[AROMA_CHART_MAX_SERIES];
    size_t color_count = chart->series_count;
    if (color_count > AROMA_CHART_MAX_SERIES)
        color_count = AROMA_CHART_MAX_SERIES;
    for (size_t i = 0; i < color_count; i++)
        colors[i] = chart_keep_stroke(chart_series_color(chart, i, line_color));
    uint32_t hash = chart_raster_hash(chart, colors, color_count);

    if (chart->tex_w == bw && chart->tex_h == bh &&
        fabsf(chart->tex_k - k) < 0.0005f && chart->tex_hash == hash &&
        chart->rendered_version == chart->version)
        return; /* cache is warm */

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->load_image_from_rgba)
        return;

    double x0, x1, y0, y1;
    chart_resolve_range(chart, &x0, &x1, &y0, &y1);

    /* Everything pbPlots allocates lives in its arena and is swept in one go
     * below — always pair Start/Free, and copy the pixels out first. */
    StartArenaAllocator();

    RGBABitmapImageReference *ref = CreateRGBABitmapImageReference();
    if (chart->type == AROMA_CHART_BAR)
        chart_rasterize_bar(ref, chart, bw, bh, plot_w, y0, y1, colors, chart_bar_groups(chart));
    else
        chart_rasterize_xy(ref, chart, bw, bh, x0, x1, y0, y1, colors, k);

    RGBABitmapImage *image = ref ? ref->image : NULL;
    if (image && image->pixels && image->xLength > 0 && image->yLength > 0 &&
        (int)image->xLength == bw && (int)image->yLength == bh)
    {
        size_t n = (size_t)bw * (size_t)bh;
        chart_key_and_swizzle((uint32_t *)image->pixels, n, colors, color_count);

        unsigned char *rgba = (unsigned char *)image->pixels;
        if (chart->tex && (chart->tex_w != bw || chart->tex_h != bh))
        { /* update_image_rgba refuses a size change: rebuild instead */
            if (gfx->unload_image)
                gfx->unload_image(chart->tex);
            chart->tex = 0;
        }
        if (!chart->tex)
            chart->tex = gfx->load_image_from_rgba(rgba, bw, bh);
        else if (gfx->update_image_rgba)
            gfx->update_image_rgba(chart->tex, rgba, bw, bh);

        if (chart->tex)
        {
            chart->tex_w = bw;
            chart->tex_h = bh;
            chart->tex_k = k;
            chart->tex_hash = hash;
        }
    }

    FreeAllocations();

    /* Mark the version rendered either way: a failed upload would otherwise be
     * retried every frame, which is far worse than showing no trace. */
    chart->rendered_version = chart->version;
}

/* ------------------------------------------------------------------- draw */

/* Fraction of each axis covered by tick label `i` of `n`: labels are documented
 * as evenly spread, which only maps onto values when the axis is explicit. */
static double chart_tick_fraction(size_t i, size_t n)
{
    if (n <= 1)
        return 0.5;
    return (double)i / (double)(n - 1);
}

/* pbPlots' bar groups sit at index `i`; centre the matching x label over the
 * group instead of on the axis ends. Fractions are independent of pixel
 * dimensions, so they line up at every density. */
static double chart_bar_group_fraction(const AromaChart *chart, size_t i, size_t groups,
                                       size_t series_count, int plot_w)
{
    if (groups == 0)
        return 0.5;
    double ss = (double)(series_count ? series_count : 1);
    double bs = (double)groups;
    double group_sep = 0.05, bar_sep = 0.005, bar_width = 0.0;
    chart_bar_layout(chart, (double)plot_w, groups, series_count,
                     &group_sep, &bar_sep, &bar_width);
    double span = ss * bar_width + (ss - 1.0) * bar_sep;
    return ((double)i * (span + group_sep) + span / 2.0);
}

static void aroma_chart_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(node))
        return;

    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    int plot_w = chart->rect.width - chart->pad_left - chart->pad_right;
    int plot_h = chart->rect.height - chart->pad_top - chart->pad_bottom;
    if (plot_w < 4 || plot_h < 4 || !chart_has_data(chart))
        return;

    int px0 = chart->rect.x + chart->pad_left;
    int py0 = chart->rect.y + chart->pad_top;

    uint32_t line_color, grid_color, frame_color, text_color;
    chart_colors(chart, &line_color, &grid_color, &frame_color, &text_color);

    size_t groups = chart_bar_groups(chart);
    if (chart->type == AROMA_CHART_BAR && groups == 0)
        return;

    chart_rasterize(chart, plot_w, plot_h, line_color);

    const char *xt = chart->x_ticks;
    const char *yt = chart->y_ticks;
    size_t nx = label_count(xt);
    size_t ny = label_count(yt);
    if (chart->type == AROMA_CHART_BAR && nx > groups)
        nx = groups;
    char tok[64];

    /* Grid first, so the trace and the frame sit on top of it. Bar groups do
     * not line up with evenly spread vertical gridlines, so bars get the
     * horizontal rules only. */
    if (chart->show_grid && gfx->draw_line)
    {
        for (size_t i = 0; i < ny; i++)
        {
            int y = py0 + (int)lround((1.0 - chart_tick_fraction(i, ny)) * (double)(plot_h - 1));
            gfx->draw_line(window_id, px0, y, px0 + plot_w - 1, y,
                           grid_color, 1.0f, false);
        }
        if (chart->type != AROMA_CHART_BAR)
        {
            for (size_t i = 0; i < nx; i++)
            {
                int x = px0 + (int)lround(chart_tick_fraction(i, nx) * (double)(plot_w - 1));
                gfx->draw_line(window_id, x, py0, x, py0 + plot_h - 1,
                               grid_color, 1.0f, false);
            }
        }
    }

    if (chart->tex && gfx->draw_image)
        gfx->draw_image(window_id, px0, py0, plot_w, plot_h, chart->tex, 0.0f);

    if (chart->show_frame && gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, px0, py0, plot_w, plot_h,
                                   frame_color, 1, false, 0.0f);

    /* Axis text in design units with a real face — crisp at every density. */
    if (!gfx->render_text || !chart->font || (!xt && !yt))
        return;

    int line_h = aroma_font_get_line_height(chart->font);
    if (line_h < 1)
        line_h = aroma_font_get_px_size(chart->font) + 4;
    int right = chart->rect.x + chart->rect.width;

    for (size_t i = 0; i < nx; i++)
    {
        if (!label_at(xt, i, tok, sizeof(tok)))
            break;
        int w = gfx->measure_text ? (int)gfx->measure_text(window_id, chart->font, tok, 1.0f) : 0;
        if (w <= 0)
            continue;
        double frac = chart->type == AROMA_CHART_BAR
                          ? chart_bar_group_fraction(chart, i, groups, chart->series_count, plot_w)
                          : chart_tick_fraction(i, nx);
        int x = px0 + (int)lround(frac * (double)(plot_w - 1)) - w / 2;
        if (x < chart->rect.x)
            x = chart->rect.x;
        if (x + w > right)
            x = right - w;
        gfx->render_text(window_id, chart->font, tok, x,
                         py0 + plot_h + AROMA_CHART_TICK_GAP,
                         text_color, 1.0f);
    }

    for (size_t i = 0; i < ny; i++)
    {
        if (!label_at(yt, i, tok, sizeof(tok)))
            break;
        int w = gfx->measure_text ? (int)gfx->measure_text(window_id, chart->font, tok, 1.0f) : 0;
        if (w <= 0)
            continue;
        int y = py0 + (int)lround((1.0 - chart_tick_fraction(i, ny)) * (double)(plot_h - 1)) - line_h / 2;
        int x = px0 - AROMA_CHART_TICK_GAP - w;
        if (x < chart->rect.x)
            x = chart->rect.x;
        gfx->render_text(window_id, chart->font, tok, x, y, text_color, 1.0f);
    }
}

static void chart_free_series(AromaChart *chart)
{
    for (size_t i = 0; i < chart->series_count; i++)
    {
        free(chart->series[i].xs);
        free(chart->series[i].ys);
        chart->series[i].xs = NULL;
        chart->series[i].ys = NULL;
        chart->series[i].count = 0;
    }
    chart->series_count = 0;
}

static void aroma_chart_destroy(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (chart->tex)
    {
        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->unload_image)
            gfx->unload_image(chart->tex);
        chart->tex = 0;
    }
    chart_free_series(chart);
    free(chart->x_ticks);
    free(chart->y_ticks);
    aroma_widget_free(chart);
    node->node_widget_ptr = NULL;
}

/* --------------------------------------------------------------- public API */

AromaNode *aroma_ui_chart(AromaNode *parent, int x, int y, int width, int height)
{
    AromaChart *chart = (AromaChart *)aroma_widget_alloc(sizeof(AromaChart));
    if (!chart)
        return NULL;

    memset(chart, 0, sizeof(AromaChart));
    chart->rect.x = x;
    chart->rect.y = y;
    chart->rect.width = width;
    chart->rect.height = height;
    chart->type = AROMA_CHART_LINE;
    chart->auto_x = true;
    chart->auto_y = true;
    chart->x_min = 0.0;
    chart->x_max = 1.0;
    chart->y_min = 0.0;
    chart->y_max = 1.0;
    chart->line_thickness = 2.0f;
    chart->bar_gap = -1.0f;
    chart->pad_left = AROMA_CHART_PAD_LEFT;
    chart->pad_top = AROMA_CHART_PAD_TOP;
    chart->pad_right = AROMA_CHART_PAD_RIGHT;
    chart->pad_bottom = AROMA_CHART_PAD_BOTTOM;
    chart->show_grid = true;
    chart->show_frame = true;
    chart->version = 1;

    /* Colours stay unset so the chart follows the theme until told otherwise. */

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, chart);
    if (!node)
    {
        aroma_widget_free(chart);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_chart_draw);
    node->destroy_cb = aroma_chart_destroy;
    return node;
}

void aroma_chart_set_type(AromaNode *node, AromaChartType type)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (type < AROMA_CHART_LINE || type > AROMA_CHART_BAR)
        type = AROMA_CHART_LINE;
    if (chart->type == type)
        return;
    chart->type = type;
    chart_touch(node, chart);
}

void aroma_chart_set_series(AromaNode *node, const double *xs, const double *ys, size_t count)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart_free_series(chart);
    if (count > 0)
        aroma_chart_add_series(node, xs, ys, count);
    else
        chart_touch(node, chart);
}

void aroma_chart_set_series_csv(AromaNode *node, const char *xs_csv, const char *ys_csv)
{
    if (!node || !node->node_widget_ptr)
        return;

    size_t count = csv_count(ys_csv);
    if (count == 0)
    {
        aroma_chart_set_series(node, NULL, NULL, 0);
        return;
    }

    double *ys = (double *)malloc(sizeof(double) * count);
    if (!ys)
        return;
    csv_fill(ys_csv, ys, count);

    double *xs = NULL;
    if (xs_csv && csv_count(xs_csv) == count)
    {
        xs = (double *)malloc(sizeof(double) * count);
        if (xs)
            csv_fill(xs_csv, xs, count);
    }

    aroma_chart_set_series(node, xs, ys, count);
    free(xs);
    free(ys);
}

void aroma_chart_add_series(AromaNode *node, const double *xs, const double *ys, size_t count)
{
    if (!node || !node->node_widget_ptr || count == 0)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (chart->series_count >= AROMA_CHART_MAX_SERIES)
        return;

    double *nx = (double *)malloc(sizeof(double) * count);
    double *ny = (double *)malloc(sizeof(double) * count);
    if (!nx || !ny)
    {
        free(nx);
        free(ny);
        return;
    }
    if (xs)
        memcpy(nx, xs, sizeof(double) * count);
    else
        for (size_t i = 0; i < count; i++)
            nx[i] = (double)i;
    if (ys)
        memcpy(ny, ys, sizeof(double) * count);
    else
        memset(ny, 0, sizeof(double) * count);

    AromaChartSeries *ser = &chart->series[chart->series_count++];
    memset(ser, 0, sizeof(*ser));
    ser->xs = nx;
    ser->ys = ny;
    ser->count = count;
    chart_touch(node, chart);
}

void aroma_chart_add_series_csv(AromaNode *node, const char *xs_csv, const char *ys_csv)
{
    if (!node || !node->node_widget_ptr)
        return;

    size_t count = csv_count(ys_csv);
    if (count == 0)
        return;

    double *ys = (double *)malloc(sizeof(double) * count);
    if (!ys)
        return;
    csv_fill(ys_csv, ys, count);

    double *xs = NULL;
    if (xs_csv && csv_count(xs_csv) == count)
    {
        xs = (double *)malloc(sizeof(double) * count);
        if (xs)
            csv_fill(xs_csv, xs, count);
    }

    aroma_chart_add_series(node, xs, ys, count);
    free(xs);
    free(ys);
}

void aroma_chart_clear_series(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart_free_series(chart);
    chart_touch(node, chart);
}

size_t aroma_chart_series_count(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return 0;
    return ((AromaChart *)node->node_widget_ptr)->series_count;
}

void aroma_chart_set_series_color(AromaNode *node, size_t index, uint32_t color)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (index >= chart->series_count)
        return;
    chart->series[index].color = color;
    chart->series[index].color_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_series_line_style(AromaNode *node, size_t index, AromaChartLineStyle style)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (index >= chart->series_count)
        return;
    if (style < AROMA_CHART_SOLID || style > AROMA_CHART_TWODASH)
        style = AROMA_CHART_SOLID;
    chart->series[index].line_style = style;
    chart_touch(node, chart);
}

void aroma_chart_set_series_point_type(AromaNode *node, size_t index, AromaChartPointType type)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (index >= chart->series_count)
        return;
    if (type < AROMA_CHART_POINT_NONE || type > AROMA_CHART_POINT_DOT_LINE_TO_AXIS)
        type = AROMA_CHART_POINT_NONE;
    chart->series[index].point_type = type;
    chart->series[index].point_type_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_series_thickness(AromaNode *node, size_t index, float thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (index >= chart->series_count)
        return;
    chart->series[index].thickness = thickness > 0.0f ? thickness : 1.0f;
    chart->series[index].thickness_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_range(AromaNode *node, bool auto_x, double x_min, double x_max,
                           bool auto_y, double y_min, double y_max)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->auto_x = auto_x;
    chart->auto_y = auto_y;
    if (!auto_x)
    {
        chart->x_min = x_min;
        chart->x_max = x_max;
    }
    if (!auto_y)
    {
        chart->y_min = y_min;
        chart->y_max = y_max;
    }
    chart_touch(node, chart);
}

void aroma_chart_set_line_color(AromaNode *node, uint32_t color)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->line_color = color;
    chart->line_color_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_grid_color(AromaNode *node, uint32_t color)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->grid_color = color;
    chart->grid_color_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_frame_color(AromaNode *node, uint32_t color)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->frame_color = color;
    chart->frame_color_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_text_color(AromaNode *node, uint32_t color)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->text_color = color;
    chart->text_color_set = true;
    chart_touch(node, chart);
}

void aroma_chart_set_line_thickness(AromaNode *node, float thickness)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->line_thickness = thickness > 0.0f ? thickness : 1.0f;
    chart_touch(node, chart);
}

void aroma_chart_set_bar_gap(AromaNode *node, float gap)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->bar_gap = gap;
    chart_touch(node, chart);
}

void aroma_chart_set_padding(AromaNode *node, int left, int top, int right, int bottom)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->pad_left = left < 0 ? 0 : left;
    chart->pad_top = top < 0 ? 0 : top;
    chart->pad_right = right < 0 ? 0 : right;
    chart->pad_bottom = bottom < 0 ? 0 : bottom;
    chart_touch(node, chart);
}

void aroma_chart_set_tick_labels(AromaNode *node, const char *x_csv, const char *y_csv)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;

    char *nx = NULL;
    char *ny = NULL;
    if (x_csv && *x_csv)
    {
        nx = strdup(x_csv);
        if (!nx)
            return;
    }
    if (y_csv && *y_csv)
    {
        ny = strdup(y_csv);
        if (!ny)
        {
            free(nx);
            return;
        }
    }

    free(chart->x_ticks);
    free(chart->y_ticks);
    chart->x_ticks = nx;
    chart->y_ticks = ny;
    chart_touch(node, chart);
}

void aroma_chart_set_font(AromaNode *node, AromaFont *font)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    if (chart->font == font)
        return;
    chart->font = font; /* borrowed, the loader registry owns it */
    chart_touch(node, chart);
}

void aroma_chart_set_furniture(AromaNode *node, bool grid, bool frame)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;
    chart->show_grid = grid;
    chart->show_frame = frame;
    chart_touch(node, chart);
}
