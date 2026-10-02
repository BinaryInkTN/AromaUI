#include "widgets/aroma_canvas.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_slab_alloc.h"
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

/**
 * @brief Reserve the next op slot in canvas->ops, or NULL if the fixed
 * AROMA_CANVAS_MAX_OPS capacity is already used up.
 *
 * This is a fixed array (see AROMA_CANVAS_MAX_OPS in aroma_canvas.h),
 * not a growable one -- AromaCanvas comes from aroma_widget_alloc(), a
 * slab allocator, and slab-backed memory is not something you can safely
 * hand to realloc(). So capacity is fixed at compile time; once a node's
 * op_count reaches the limit, further draw_* calls are dropped (logged
 * here, not crashed) until the node is cleared via aroma_canvas_clear().
 */
static AromaCanvasOp *aroma_canvas_push_op(AromaCanvas *canvas)
{
    if (canvas->op_count >= AROMA_CANVAS_MAX_OPS)
    {
        LOG_WARNING("Canvas op limit (%d) reached; drawing call dropped. Call aroma_canvas_clear() to reset.", AROMA_CANVAS_MAX_OPS);
        return NULL;
    }

    AromaCanvasOp *op = &canvas->ops[canvas->op_count];
    memset(op, 0, sizeof(AromaCanvasOp));
    canvas->op_count++;
    return op;
}

static void aroma_canvas_draw_op(const AromaCanvasOp *op, AromaGraphicsInterface *gfx, size_t window_id)
{
    switch (op->draw_mode)
    {
    case AROMA_CANVAS_DRAW_MODE_IDLE:
        break;
    case AROMA_CANVAS_DRAW_MODE_RECT:
        if (gfx->fill_rectangle)
        {
            gfx->fill_rectangle(window_id, op->x, op->y, op->width, op->height, op->color, op->border_radius > 0, (float)op->border_radius);
        }
        break;
    case AROMA_CANVAS_DRAW_MODE_CIRCLE:
        if (gfx->draw_arc && op->radius > 0)
        {

            float ring_radius = (float)op->radius * 0.5f;
            float fake_disc_thickness = (float)op->radius;
            gfx->draw_arc(window_id, op->x, op->y,
                          (int)(ring_radius + 0.5f),
                          0.0f, 6.28318530718f, op->color,
                          (int)(fake_disc_thickness + 0.5f));
        }
        break;
    case AROMA_CANVAS_DRAW_MODE_ARC:
        if (gfx->draw_arc && op->radius > 0)
        {

            int thickness_px = (op->thickness > 0.0f) ? (int)(op->thickness + 0.5f) : 1;
            gfx->draw_arc(window_id, op->x, op->y,
                          op->radius,
                          op->arc_start_angle, op->arc_end_angle,
                          op->color, thickness_px);
        }
        break;
    case AROMA_CANVAS_DRAW_MODE_LINE:
        if (gfx->draw_line)
        {
            gfx->draw_line(window_id, op->x, op->y,
                           op->line_x2, op->line_y2,
                           op->color, op->thickness, true);
        }
        break;
    case AROMA_CANVAS_DRAW_MODE_TEXT:
        if (gfx->render_text && op->text_font)
        {

            gfx->render_text(window_id, op->text_font, op->text,
                             op->x, op->y, op->color, 1.0f);
        }
        break;
    case AROMA_CANVAS_DRAW_MODE_CLEAR:
        if (gfx->clear)
        {

            gfx->clear(window_id, op->color);
        }
        break;

    default:
        break;
    }
}

void aroma_canvas_draw(AromaNode *node, size_t window_id)
{
    if (!node)
    {
        LOG_ERROR("Invalid canvas node for drawing");
        return;
    }
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
    {
        LOG_ERROR("Canvas widget pointer is NULL");
        return;
    }
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
    {
        LOG_ERROR("Graphics interface is NULL");
        return;
    }

    /* Render every queued op in call order, instead of the old
       switch-on-single-draw_mode which could only ever render whichever
       draw_* call happened most recently. A CLEAR op appended via
       aroma_canvas_clear() still renders in its place in the sequence,
       same as any other op. */
    for (size_t i = 0; i < canvas->op_count; i++)
    {
        aroma_canvas_draw_op(&canvas->ops[i], gfx, window_id);
    }
}

void aroma_canvas_clear(AromaNode *node, uint32_t color)
{
    if (!node)
        return;
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
        return;

    /* Drop all previously queued ops -- a canvas that's been cleared
       should not still render shapes queued before the clear. This is
       also how a node reclaims its AROMA_CANVAS_MAX_OPS slots. The clear
       color itself is kept as a single CLEAR op so aroma_canvas_draw()
       still paints the background before anything drawn after this
       call. */
    canvas->op_count = 0;

    AromaCanvasOp *op = aroma_canvas_push_op(canvas);
    if (!op)
        return;

    op->color = color;
    op->draw_mode = AROMA_CANVAS_DRAW_MODE_CLEAR;
    aroma_node_invalidate(node);
}

AromaNode *aroma_canvas_create(AromaNode *parent, int x, int y, int width, int height)
{
    AromaCanvas *canvas = (AromaCanvas *)aroma_widget_alloc(sizeof(AromaCanvas));
    if (!canvas)
    {
        LOG_ERROR("Failed to allocate memory for canvas");
        return NULL;
    }
    /* ops[] is a fixed in-struct array (see AROMA_CANVAS_MAX_OPS), so
       there's no separate buffer to allocate -- just zero the count.
       Not relying on aroma_widget_alloc() having zeroed this for us. */
    canvas->op_count = 0;

#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif

    AromaNode *canvas_node = (AromaNode *)__add_child_node(NODE_TYPE_WIDGET, parent, canvas);
    if (!canvas_node)
    {
        aroma_widget_free(canvas);
        LOG_ERROR("Failed to create canvas node");
        return NULL;
    }
    aroma_node_set_draw_cb(canvas_node, aroma_canvas_draw);
    return canvas_node;
}

void aroma_canvas_draw_rect(AromaNode *node, int x, int y, int width, int height, int border_radius, bool is_rect_filled, uint32_t color)
{
    if (!node)
        return;
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
        return;

    AromaCanvasOp *op = aroma_canvas_push_op(canvas);
    if (!op)
        return;

    op->x = x;
    op->y = y;
    op->width = width;
    op->height = height;
    op->border_radius = border_radius;
    op->is_rect_filled = is_rect_filled;
    op->color = color;
    op->draw_mode = AROMA_CANVAS_DRAW_MODE_RECT;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_circle(AromaNode *node, int center_x, int center_y, int radius, uint32_t color)
{
    if (!node)
        return;
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
        return;

    AromaCanvasOp *op = aroma_canvas_push_op(canvas);
    if (!op)
        return;

    op->x = center_x;
    op->y = center_y;
    op->radius = radius;
    op->color = color;
    op->draw_mode = AROMA_CANVAS_DRAW_MODE_CIRCLE;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_arc(AromaNode *node, int center_x, int center_y, int radius, float start_angle, float end_angle, uint32_t color, int thickness)
{
    if (!node)
        return;
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
        return;

    AromaCanvasOp *op = aroma_canvas_push_op(canvas);
    if (!op)
        return;

    op->x = center_x;
    op->y = center_y;
    op->radius = radius;
    op->arc_start_angle = start_angle;
    op->arc_end_angle = end_angle;
    op->color = color;
    op->thickness = (float)thickness;
    op->draw_mode = AROMA_CANVAS_DRAW_MODE_ARC;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_line(AromaNode *node, int x1, int y1, int x2, int y2, uint32_t color, int thickness)
{
    if (!node)
        return;
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
        return;

    AromaCanvasOp *op = aroma_canvas_push_op(canvas);
    if (!op)
        return;

    op->x = x1;
    op->y = y1;
    op->line_x2 = x2;
    op->line_y2 = y2;
    op->color = color;
    op->thickness = (float)thickness;
    op->draw_mode = AROMA_CANVAS_DRAW_MODE_LINE;
    aroma_node_invalidate(node);
}

void aroma_canvas_draw_text(AromaNode *node, const char *text, int x, int y, uint32_t color, AromaFont *font)
{
    if (!node || !text)
        return;
    AromaCanvas *canvas = (AromaCanvas *)node->node_widget_ptr;
    if (!canvas)
        return;

    AromaCanvasOp *op = aroma_canvas_push_op(canvas);
    if (!op)
        return;

    strncpy(op->text, text, sizeof(op->text) - 1);
    op->text[sizeof(op->text) - 1] = '\0';
    op->x = x;
    op->y = y;
    op->color = color;
    op->text_font = font;
    op->draw_mode = AROMA_CANVAS_DRAW_MODE_TEXT;
    aroma_node_invalidate(node);
}