#include "widgets/aroma_canvas.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_slab_alloc.h"

void aroma_canvas_draw(AromaNode* node, size_t window_id)
{
    if(!node) {
        LOG_ERROR("Invalid canvas node for drawing");
        return;
    }
    AromaCanvas* canvas = (AromaCanvas*)node->node_widget_ptr;
    if (!canvas) {
        LOG_ERROR("Canvas widget pointer is NULL");
        return;
    }
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        LOG_ERROR("Graphics interface is NULL");
        return;
    }

    switch (canvas->draw_mode) {
        case AROMA_CANVAS_DRAW_MODE_RECT:
            if(gfx->fill_rectangle)
            {
                gfx->fill_rectangle(window_id, canvas->x, canvas->y, canvas->width, canvas->height, canvas->color, canvas->border_radius > 0, (float)canvas->border_radius);
            }
            break;
        case AROMA_CANVAS_DRAW_MODE_CIRCLE:
            if (gfx->fill_rectangle) {
                gfx->fill_rectangle(window_id, canvas->x, canvas->y, canvas->width, canvas->height, canvas->color, false, 0.0f);
            }
            break;
        case AROMA_CANVAS_DRAW_MODE_LINE:
            // TODO: Implement line drawing in the graphics interface and call it here
            break;
        case AROMA_CANVAS_DRAW_MODE_TEXT:
            // TODO: Implement text drawing in the graphics interface and call it here, using canvas->text_font for font details
            break;
        case AROMA_CANVAS_DRAW_MODE_CLEAR:
           // TODO: Implement clear functionality in the graphics interface and call it here
             break;

        default:
            break;
    }



}

void aroma_canvas_clear(AromaNode* node, uint32_t color) {
    if (!node) return;
    AromaCanvas* canvas = (AromaCanvas*)node->node_widget_ptr;
    if (!canvas) return;

    canvas->color = color;
    canvas->draw_mode = AROMA_CANVAS_DRAW_MODE_CLEAR;
    aroma_node_invalidate(node);
}

AromaNode* aroma_canvas_create(AromaNode* parent, int x, int y, int width, int height) {
        AromaCanvas* canvas = (AromaCanvas*)aroma_widget_alloc(sizeof(AromaCanvas));
        if (!canvas)
        {
        LOG_ERROR("Failed to allocate memory for canvas");
        return NULL;
        }

        AromaNode* canvas_node = (AromaNode*)__add_child_node(NODE_TYPE_WIDGET, parent, canvas);
        if (!canvas_node)
        {
            aroma_widget_free(canvas);
        LOG_ERROR("Failed to create canvas node");
        return NULL;
        }
        aroma_node_set_draw_cb(canvas_node, aroma_canvas_draw);
    return canvas_node;
}

void aroma_canvas_draw_rect(AromaNode* node, int x, int y, int width, int height, int border_radius, bool is_rect_filled, uint32_t color) {
    if (!node) return;
    AromaCanvas* canvas = (AromaCanvas*)node->node_widget_ptr;
    if (!canvas) return;

    canvas->x = x;
    canvas->y = y;
    canvas->width = width;
    canvas->height = height;
    canvas->border_radius = border_radius;
    canvas->is_rect_filled = is_rect_filled;
    canvas->color = color;
    canvas->draw_mode = AROMA_CANVAS_DRAW_MODE_RECT;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_circle(AromaNode* node, int center_x, int center_y, int radius, uint32_t color) {
    if (!node) return;
    AromaCanvas* canvas = (AromaCanvas*)node->node_widget_ptr;
    if (!canvas) return;

    canvas->x = center_x;
    canvas->y = center_y;
    canvas->width = radius; 
    canvas->color = color;
    canvas->draw_mode = AROMA_CANVAS_DRAW_MODE_CIRCLE;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_line(AromaNode* node, int x1, int y1, int x2, int y2, uint32_t color, int thickness) {
    if (!node) return;
    AromaCanvas* canvas = (AromaCanvas*)node->node_widget_ptr;
    if (!canvas) return;

    canvas->x = x1;
    canvas->y = y1;
    canvas->width = x2; 
    canvas->height = y2; 
    canvas->color = color;
    canvas->draw_mode = AROMA_CANVAS_DRAW_MODE_LINE;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_text(AromaNode* node, const char* text, int x, int y, uint32_t color, AromaFont* font) {
    if (!node || !text) return;
    AromaCanvas* canvas = (AromaCanvas*)node->node_widget_ptr;
    if (!canvas) return;

    strncpy(canvas->text, text, sizeof(canvas->text) - 1);
    canvas->text[sizeof(canvas->text) - 1] = '\0';
    canvas->x = x;
    canvas->y = y;
    canvas->color = color;
    canvas->text_font = font;
    canvas->draw_mode = AROMA_CANVAS_DRAW_MODE_TEXT;
    aroma_node_invalidate(node);
}

