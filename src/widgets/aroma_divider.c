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

#include "widgets/aroma_divider.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_node.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

typedef struct AromaDivider {
    AromaRect rect;
    AromaDividerOrientation orientation;
    uint32_t color;
    int thickness;
} AromaDivider;

AromaNode* aroma_divider_create(
    AromaNode* parent,
    int x,
    int y,
    int length,
    AromaDividerOrientation orientation)
{
    if (!parent || length <= 0) return NULL;

    AromaDivider* divider = aroma_widget_alloc(sizeof(AromaDivider));
    if (!divider) return NULL;

    memset(divider, 0, sizeof(*divider));

    AromaTheme theme = aroma_theme_get_global();

    divider->orientation = orientation;
    divider->thickness = 1;
    divider->color = theme.colors.border;

    divider->rect.x = x;
    divider->rect.y = y;
    divider->rect.width  =
        (orientation == DIVIDER_ORIENTATION_HORIZONTAL) ? length : divider->thickness;
    divider->rect.height =
        (orientation == DIVIDER_ORIENTATION_HORIZONTAL) ? divider->thickness : length;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, divider);
    if (!node) {
        aroma_widget_free(divider);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_divider_draw);
    aroma_node_invalidate(node);

    return node;
}

void aroma_divider_set_color(AromaNode* divider_node, uint32_t color)
{
    if (!divider_node || !divider_node->node_widget_ptr) return;

    AromaDivider* divider = divider_node->node_widget_ptr;
    divider->color = color;
    aroma_node_invalidate(divider_node);
}

void aroma_divider_set_thickness(AromaNode* divider_node, int thickness)
{
    if (!divider_node || !divider_node->node_widget_ptr || thickness <= 0) return;

    AromaDivider* divider = divider_node->node_widget_ptr;
    divider->thickness = thickness;

    if (divider->orientation == DIVIDER_ORIENTATION_HORIZONTAL) {
        divider->rect.height = thickness;
    } else {
        divider->rect.width = thickness;
    }

    aroma_node_invalidate(divider_node);
}

void aroma_divider_draw(AromaNode* divider_node, size_t window_id)
{
    if (!divider_node || aroma_node_is_hidden(divider_node)) return;

    AromaDivider* divider = divider_node->node_widget_ptr;
    if (!divider) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    gfx->fill_rectangle(
        window_id,
        divider->rect.x,
        divider->rect.y,
        divider->rect.width,
        divider->rect.height,
        divider->color,
        false,
        0.0f);
}

void aroma_divider_destroy(AromaNode* divider_node)
{
    if (!divider_node) return;

    aroma_widget_free(divider_node->node_widget_ptr);
    divider_node->node_widget_ptr = NULL;
    __destroy_node(divider_node);
}
