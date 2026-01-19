#include "widgets/aroma_divider.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"

typedef struct AromaDivider {
    AromaRect rect;
    AromaDividerOrientation orientation;
    uint32_t color;
    int thickness;
} AromaDivider;

AromaNode* aroma_divider_create(AromaNode* parent, int x, int y, int length, AromaDividerOrientation orientation)
{
    if (!parent || length <= 0) return NULL;

    AromaDivider* divider = (AromaDivider*)aroma_widget_alloc(sizeof(AromaDivider));
    if (!divider) return NULL;

    AromaTheme theme = aroma_theme_get_global();
    divider->orientation = orientation;
    divider->thickness = 1;
    divider->color = theme.colors.border;
    divider->rect.x = x;
    divider->rect.y = y;
    divider->rect.width = (orientation == DIVIDER_ORIENTATION_HORIZONTAL) ? length : divider->thickness;
    divider->rect.height = (orientation == DIVIDER_ORIENTATION_HORIZONTAL) ? divider->thickness : length;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, divider);
    if (!node) {
        aroma_widget_free(divider);
        return NULL;
    }
    return node;
}

void aroma_divider_set_color(AromaNode* divider_node, uint32_t color)
{
    if (!divider_node || !divider_node->node_widget_ptr) return;
    AromaDivider* divider = (AromaDivider*)divider_node->node_widget_ptr;
    divider->color = color;
    aroma_node_invalidate(divider_node);
}

void aroma_divider_set_thickness(AromaNode* divider_node, int thickness)
{
    if (!divider_node || !divider_node->node_widget_ptr || thickness <= 0) return;
    AromaDivider* divider = (AromaDivider*)divider_node->node_widget_ptr;
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
    if (!divider_node || !divider_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(divider_node)) return;
    AromaDivider* divider = (AromaDivider*)divider_node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    gfx->fill_rectangle(window_id,
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
    if (divider_node->node_widget_ptr) {
        aroma_widget_free(divider_node->node_widget_ptr);
        divider_node->node_widget_ptr = NULL;
    }
    __destroy_node(divider_node);
}