/* Line chart on top of the vendored pbPlots library (vendors/pbplots, MIT).
 *
 * Division of labour: pbPlots rasterises the data series into an RGBA buffer
 * sized to the plot area * device density (aroma_font_raster_scale), so the
 * texture lands exactly device-resolution and stays sharp through the docs
 * design-fit scaling, the same invariant the glyph atlas follows. Everything
 * AromaUI draws itself — grid, frame and axis text — is drawn in design units
 * with a real font, so typography and theme colours come from the toolkit.
 *
 * Dropping pbPlots' own chrome needs three tricks (all local to this file, the
 * vendored sources stay untouched):
 *   - its borders/grid are painted with a white gridColor, and every other
 *     piece of chrome (tick marks, the zero axis, the title) in solid black or
 *     white; the rasteriser is aliased (Bresenham + DrawPixel), so those land
 *     as exactly 0 or 255 in all channels and can be keyed out to alpha 0;
 *   - pinning the axes onto the range corners (xAxisAuto/yAxisAuto off) puts
 *     every tick label and axis title outside the bitmap, where SetPixel
 *     drops them, so its built-in 8px font never reaches the screen;
 *   - the stroke colour is nudged off pure black/white so the key can't eat it.
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

#define AROMA_CHART_TICK_GAP 6 /* design px between the plot edge and tick text */

typedef struct AromaChart
{
    AromaRect rect;

    double *xs;
    double *ys;
    size_t count;

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
    int pad_left;
    int pad_top;
    int pad_right;
    int pad_bottom;
    bool show_grid;
    bool show_frame;

    char *x_ticks; /* comma-separated labels, evenly spread over the axis */
    char *y_ticks;
    AromaFont *font; /* borrowed from the loader's sized-font registry */

    unsigned version;         /* bumped by every setter */
    unsigned rendered_version; /* version the cached raster corresponds to */
    unsigned int tex;
    int tex_w;
    int tex_h;
    float tex_k;
    uint32_t tex_line; /* stroke colour baked into the cached raster */
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

/* Reads entry `i` of a numeric CSV into `buf`; returns NULL when the list runs out. */
static const char *csv_at(const char *s, size_t i, char *buf, size_t buf_len)
{
    const char *p = csv_skip(s ? s : "");
    size_t n = 0;
    while (*p)
    {
        char *end = NULL;
        strtod(p, &end);
        if (end == p)
            break;
        if (n == i)
        {
            size_t len = (size_t)(end - p);
            if (len >= buf_len)
                len = buf_len - 1;
            memcpy(buf, p, len);
            buf[len] = '\0';
            return buf;
        }
        n++;
        p = csv_skip(end);
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
 * come from the data plus headroom so the trace clears the frame. Degenerate
 * ends are widened because pbPlots silently falls back to 0..10 when they match. */
static void chart_resolve_range(const AromaChart *chart,
                                double *x0, double *x1, double *y0, double *y1)
{
    if (chart->auto_x && chart->count > 0)
    {
        *x0 = *x1 = chart->xs[0];
        for (size_t i = 1; i < chart->count; i++)
        {
            if (chart->xs[i] < *x0) *x0 = chart->xs[i];
            if (chart->xs[i] > *x1) *x1 = chart->xs[i];
        }
        double span = *x1 - *x0;
        if (span > 0.0)
        {
            *x0 -= span * 0.04;
            *x1 += span * 0.04;
        }
    }
    else
    {
        *x0 = chart->x_min;
        *x1 = chart->x_max;
    }

    if (chart->auto_y && chart->count > 0)
    {
        *y0 = *y1 = chart->ys[0];
        for (size_t i = 1; i < chart->count; i++)
        {
            if (chart->ys[i] < *y0) *y0 = chart->ys[i];
            if (chart->ys[i] > *y1) *y1 = chart->ys[i];
        }
        double span = *y1 - *y0;
        if (span > 0.0)
        {
            *y0 -= span * 0.06;
            *y1 += span * 0.06;
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

/* The stroke must not be pure black or white: those are exactly the two
 * colours keyed out of the raster below. */
static uint32_t chart_keep_stroke(uint32_t color)
{
    uint32_t r = (color >> 16) & 0xFFu;
    uint32_t g = (color >> 8) & 0xFFu;
    uint32_t b = color & 0xFFu;
    if (r == 0x00 && g == 0x00 && b == 0x00)
        return (color & 0xFF000000u) | 0x00010101u;
    if (r == 0xFF && g == 0xFF && b == 0xFF)
        return (color & 0xFF000000u) | 0x00FEFEFEu;
    return color;
}

/* pbPlots packs pixels as RRGGBBAA; GL_RGBA wants bytes [r,g,b,a], which on
 * little endian is AARRGGBB — swap the pairs while keying pbPlots' background
 * and chrome (solid white and solid black) out. The keyed pixels are zeroed
 * outright rather than left as white-at-alpha-0: the bitmap is drawn at device
 * density and resampled a little when the design does not divide the viewport
 * exactly, and texture filtering would otherwise bleed that white into the
 * series as a halo. */
static void chart_key_and_swizzle(uint32_t *px, size_t n, uint32_t stroke)
{
    uint32_t sr = (stroke >> 16) & 0xFFu;
    uint32_t sg = (stroke >> 8) & 0xFFu;
    uint32_t sb = stroke & 0xFFu;
    /* pbPlots paints its built-in axis labels and tick marks in neutral grey.
       We render our own tick labels as real text, so leaving those glyph
       fragments clipped at the bitmap edge looks like random debris.  Key
       neutral grey out as well, unless the series itself is grey. */
    bool stroke_is_grey = (sr == sg && sg == sb);
    for (size_t i = 0; i < n; i++)
    {
        uint32_t v = px[i];
        uint32_t r = (v >> 24) & 0xFFu;
        uint32_t g = (v >> 16) & 0xFFu;
        uint32_t b = (v >> 8) & 0xFFu;
        uint32_t a = v & 0xFFu;
        uint32_t maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
        uint32_t minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
        bool neutral = (maxc - minc) <= 2u;
        bool close_to_stroke = !stroke_is_grey &&
            (r > sr ? r - sr : sr - r) < 6u &&
            (g > sg ? g - sg : sg - g) < 6u &&
            (b > sb ? b - sb : sb - b) < 6u;
        if ((r == 0xFFu && g == 0xFFu && b == 0xFFu) ||
            (r == 0x00u && g == 0x00u && b == 0x00u) ||
            (neutral && !stroke_is_grey && !close_to_stroke))
            px[i] = 0u;
        else
            px[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
}

/* --------------------------------------------------------------- rasterize */

/* Re-renders the pbPlots bitmap when the data, the widget size, the density or
 * the stroke colour change; otherwise leaves the cached texture alone. */
static void chart_rasterize(AromaChart *chart, int plot_w, int plot_h, uint32_t line_color)
{
    float k = aroma_font_raster_scale();
    int bw = (int)lround((double)plot_w * (double)k);
    int bh = (int)lround((double)plot_h * (double)k);
    if (bw < 2 || bh < 2)
        return;

    if (chart->tex_w == bw && chart->tex_h == bh &&
        fabsf(chart->tex_k - k) < 0.0005f && chart->tex_line == line_color &&
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

    uint32_t stroke = chart_keep_stroke(line_color);
    ScatterPlotSeries *series = GetDefaultScatterPlotSeriesSettings();
    series->xs = chart->xs;
    series->xsLength = chart->count;
    series->ys = chart->ys;
    series->ysLength = chart->count;
    series->linearInterpolation = true;
    series->lineType = L"solid";
    series->lineTypeLength = 5;
    series->pointType = L"none";
    series->pointTypeLength = 4;
    series->lineThickness = (double)chart->line_thickness * (double)k;
    /* pbPlots drops a line whose thickness rounds below 1px and has a separate
     * code path per integer width, so keep the pen at least 2 device px: at
     * small design-fit scales that is the hairline a chart wants anyway. */
    if (series->lineThickness < 2.0)
        series->lineThickness = 2.0;
    series->color = CreateRGBColor((double)((stroke >> 16) & 0xFFu) / 255.0,
                                   (double)((stroke >> 8) & 0xFFu) / 255.0,
                                   (double)(stroke & 0xFFu) / 255.0);
    ScatterPlotSeries *series_list[1];
    series_list[0] = series;
    settings->scatterPlotSeries = series_list;
    settings->scatterPlotSeriesLength = 1;

    StringReference *error = CreateStringReference(L"", 0);
    /* Upstream reuses its `success` flag for per-segment clipping, so a false
     * return is routine (any segment ending outside the range) and proves
     * nothing — the canvas length below is what says the render happened. */
    (void)DrawScatterPlotFromSettings(ref, settings, error);

    RGBABitmapImage *image = ref ? ref->image : NULL;
    if (image && image->pixels && image->xLength > 0 && image->yLength > 0 &&
        (int)image->xLength == bw && (int)image->yLength == bh)
    {
        size_t n = (size_t)bw * (size_t)bh;
        chart_key_and_swizzle((uint32_t *)image->pixels, n, stroke);

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
            chart->tex_line = line_color;
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
    if (plot_w < 4 || plot_h < 4 || chart->count == 0)
        return;

    int px0 = chart->rect.x + chart->pad_left;
    int py0 = chart->rect.y + chart->pad_top;

    uint32_t line_color, grid_color, frame_color, text_color;
    chart_colors(chart, &line_color, &grid_color, &frame_color, &text_color);

    chart_rasterize(chart, plot_w, plot_h, line_color);

    const char *xt = chart->x_ticks;
    const char *yt = chart->y_ticks;
    size_t nx = label_count(xt);
    size_t ny = label_count(yt);
    char tok[64];

    /* Grid first, so the trace and the frame sit on top of it. */
    if (chart->show_grid && gfx->draw_line)
    {
        for (size_t i = 0; i < ny; i++)
        {
            int y = py0 + (int)lround((1.0 - chart_tick_fraction(i, ny)) * (double)(plot_h - 1));
            gfx->draw_line(window_id, px0, y, px0 + plot_w - 1, y,
                           grid_color, 1.0f, false);
        }
        for (size_t i = 0; i < nx; i++)
        {
            int x = px0 + (int)lround(chart_tick_fraction(i, nx) * (double)(plot_w - 1));
            gfx->draw_line(window_id, x, py0, x, py0 + plot_h - 1,
                           grid_color, 1.0f, false);
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
        int x = px0 + (int)lround(chart_tick_fraction(i, nx) * (double)(plot_w - 1)) - w / 2;
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
    free(chart->xs);
    free(chart->ys);
    free(chart->x_ticks);
    free(chart->y_ticks);
    aroma_widget_free(chart);
    node->node_widget_ptr = NULL;
    __destroy_node(node);
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
    chart->auto_x = true;
    chart->auto_y = true;
    chart->x_min = 0.0;
    chart->x_max = 1.0;
    chart->y_min = 0.0;
    chart->y_max = 1.0;
    chart->line_thickness = 2.0f;
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

void aroma_chart_set_series(AromaNode *node, const double *xs, const double *ys, size_t count)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaChart *chart = (AromaChart *)node->node_widget_ptr;

    double *nx = NULL;
    double *ny = NULL;
    if (count > 0)
    {
        nx = (double *)malloc(sizeof(double) * count);
        ny = (double *)malloc(sizeof(double) * count);
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
    }

    free(chart->xs);
    free(chart->ys);
    chart->xs = nx;
    chart->ys = ny;
    chart->count = count;
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
