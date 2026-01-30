/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "widgets/aroma_tooltip.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_TOOLTIP_TEXT_MAX 128

typedef struct AromaTooltip {
    AromaRect rect;
    char text[AROMA_TOOLTIP_TEXT_MAX];
    AromaTooltipPosition position;
    bool visible;
    AromaFont* font;
    float corner_radius;
    float text_scale;
    uint32_t bg_color;
} AromaTooltip;

AromaNode* aroma_tooltip_create(AromaNode* parent, const char* text, int x, int y, AromaTooltipPosition position)
{
    if (!parent || !text) return NULL;
    AromaTooltip* tip = (AromaTooltip*)aroma_widget_alloc(sizeof(AromaTooltip));
    if (!tip) return NULL;

    memset(tip, 0, sizeof(AromaTooltip));
    tip->rect.x = x;
    tip->rect.y = y;
    tip->rect.width = 140;
    tip->rect.height = 32;
    tip->position = position;
    tip->visible = false;
    strncpy(tip->text, text, AROMA_TOOLTIP_TEXT_MAX - 1);
    AromaTheme theme = aroma_theme_get_global();
    tip->corner_radius = 6.0f;
    tip->text_scale = 1.0f;
    tip->bg_color = aroma_color_adjust(theme.colors.text_primary, -0.6f);

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, tip);
    if (!node) {
        aroma_widget_free(tip);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_tooltip_draw);

    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

void aroma_tooltip_set_text(AromaNode* tooltip_node, const char* text)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr || !text) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    strncpy(tip->text, text, AROMA_TOOLTIP_TEXT_MAX - 1);
    aroma_node_invalidate(tooltip_node);
}

void aroma_tooltip_show(AromaNode* tooltip_node, int delay_ms)
{
    (void)delay_ms;
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    tip->visible = true;
    aroma_node_invalidate(tooltip_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_tooltip_hide(AromaNode* tooltip_node)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    tip->visible = false;
    aroma_node_invalidate(tooltip_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_tooltip_set_font(AromaNode* tooltip_node, AromaFont* font)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    tip->font = font;
}

void aroma_tooltip_draw(AromaNode* tooltip_node, size_t window_id)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    if (!tip->visible) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    if (aroma_node_is_hidden(tooltip_node)) return;
    AromaTheme theme = aroma_theme_get_global();

    gfx->fill_rectangle(window_id, tip->rect.x, tip->rect.y, tip->rect.width, tip->rect.height,
                        tip->bg_color, true, tip->corner_radius);

    if (tip->font && gfx->render_text) {
        gfx->render_text(window_id, tip->font, tip->text, tip->rect.x + 8, tip->rect.y + 20, theme.colors.surface, tip->text_scale);
    }
}

void aroma_tooltip_destroy(AromaNode* tooltip_node)
{
    if (!tooltip_node) return;
    if (tooltip_node->node_widget_ptr) {
        aroma_widget_free(tooltip_node->node_widget_ptr);
        tooltip_node->node_widget_ptr = NULL;
    }
    __destroy_node(tooltip_node);
}