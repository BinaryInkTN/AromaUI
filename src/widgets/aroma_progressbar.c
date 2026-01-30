#include "widgets/aroma_progressbar.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"

typedef struct AromaProgressBar {
    AromaRect rect;
    AromaProgressType type;
    float progress;
    uint32_t track_color;
    uint32_t indicator_color;
    float corner_radius;
    int fill_width;
} AromaProgressBar;

static void __progressbar_update_fill(AromaProgressBar* bar)
{
    bar->fill_width = (int)(bar->rect.width * bar->progress);
}

AromaNode* aroma_progressbar_create(AromaNode* parent, int x, int y, int width, int height, AromaProgressType type)
{
    if (!parent || width <= 0 || height <= 0) return NULL;

    AromaProgressBar* bar = (AromaProgressBar*)aroma_widget_alloc(sizeof(AromaProgressBar));
    if (!bar) return NULL;

    AromaTheme theme = aroma_theme_get_global();
    bar->rect.x = x;
    bar->rect.y = y;
    bar->rect.width = width;
    bar->rect.height = height;
    bar->type = type;
    bar->progress = 0.0f;
    bar->track_color = aroma_color_blend(theme.colors.surface, theme.colors.border, 0.45f);
    bar->indicator_color = theme.colors.primary;
    bar->corner_radius = (float)bar->rect.height / 2.0f;
    bar->fill_width = 0;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, bar);
    if (!node) {
        aroma_widget_free(bar);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_progressbar_draw);
   
    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif
   
    return node;
}

void aroma_progressbar_set_progress(AromaNode* progress_node, float progress)
{
    if (!progress_node || !progress_node->node_widget_ptr) return;
    AromaProgressBar* bar = (AromaProgressBar*)progress_node->node_widget_ptr;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    bar->progress = progress;
    __progressbar_update_fill(bar);
    aroma_node_invalidate(progress_node);
}

float aroma_progressbar_get_progress(AromaNode* progress_node)
{
    if (!progress_node || !progress_node->node_widget_ptr) return 0.0f;
    AromaProgressBar* bar = (AromaProgressBar*)progress_node->node_widget_ptr;
    return bar->progress;
}

void aroma_progressbar_set_colors(AromaNode* progress_node, uint32_t track_color, uint32_t indicator_color)
{
    if (!progress_node || !progress_node->node_widget_ptr) return;
    AromaProgressBar* bar = (AromaProgressBar*)progress_node->node_widget_ptr;
    bar->track_color = track_color;
    bar->indicator_color = indicator_color;
    aroma_node_invalidate(progress_node);
}

void aroma_progressbar_draw(AromaNode* progress_node, size_t window_id)
{
    if (!progress_node || !progress_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(progress_node)) return;
    AromaProgressBar* bar = (AromaProgressBar*)progress_node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    gfx->fill_rectangle(window_id, bar->rect.x, bar->rect.y, bar->rect.width, bar->rect.height,
                        bar->track_color, true, bar->corner_radius);

    if (bar->type == PROGRESS_TYPE_DETERMINATE) {
        if (bar->fill_width > 0) {
            gfx->fill_rectangle(window_id, bar->rect.x, bar->rect.y, bar->fill_width, bar->rect.height,
                                bar->indicator_color, true, bar->corner_radius);
        }
    } else {
        int span = bar->rect.width / 3;
        if (span < 12) span = 12;
        gfx->fill_rectangle(window_id, bar->rect.x, bar->rect.y, span, bar->rect.height,
                            bar->indicator_color, true, bar->corner_radius);
    }
}

void aroma_progressbar_destroy(AromaNode* progress_node)
{
    if (!progress_node) return;
    if (progress_node->node_widget_ptr) {
        aroma_widget_free(progress_node->node_widget_ptr);
        progress_node->node_widget_ptr = NULL;
    }
    __destroy_node(progress_node);
}
