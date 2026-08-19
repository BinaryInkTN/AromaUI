#ifndef AROMA_CANVAS_H
#define AROMA_CANVAS_H

#include "aroma_common.h"
#include "aroma_node.h"

#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_style.h"
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct AromaWindow AromaWindow;
    typedef enum
    {
        AROMA_CANVAS_DRAW_MODE_IDLE,
        AROMA_CANVAS_DRAW_MODE_RECT,
        AROMA_CANVAS_DRAW_MODE_CIRCLE,
        AROMA_CANVAS_DRAW_MODE_LINE,
        AROMA_CANVAS_DRAW_MODE_TEXT,
        AROMA_CANVAS_DRAW_MODE_CLEAR,
        AROMA_CANVAS_DRAW_MODE_ARC
    } CanvasDrawMode;

    /**
     * @brief A single queued draw operation.
     *
     * A canvas node holds a fixed-size array of these (see
     * AROMA_CANVAS_MAX_OPS below) instead of a single shared set of
     * fields, so multiple draw_* calls on the same node accumulate (e.g.
     * two arcs) instead of the second call overwriting the first's
     * geometry before either is ever rendered.
     *
     * Field meanings are unchanged from the original single-shot
     * AromaCanvas -- see aroma_canvas_draw_arc()'s doc comment below for
     * the angle convention (radians, +x-axis origin), and the CIRCLE case
     * in aroma_canvas.c for why CIRCLE and ARC stay separate draw modes
     * (CIRCLE fakes a filled disc via thickness == radius; ARC keeps
     * radius and thickness independent for a real stroked ring segment).
     */
    typedef struct
    {
        CanvasDrawMode draw_mode;

        int x;
        int y;
        int width;
        int height;
        int border_radius;
        bool is_rect_filled;
        uint32_t color;
        char text[256];
        AromaFont *text_font;

        int line_x2;
        int line_y2;
        float thickness;

        int radius;

        float arc_start_angle;
        float arc_end_angle;
    } AromaCanvasOp;

/**
 * @brief Maximum queued draw ops per canvas node.
 *
 * AromaCanvas is allocated via aroma_widget_alloc() (a slab allocator --
 * see aroma_slab_alloc.h), which hands out fixed-size blocks and is not
 * built for realloc()-based growth. So this is a fixed-capacity array,
 * not a dynamic one: once a node's op_count hits this limit, further
 * draw_* calls on it are dropped (logged, not crashed -- see
 * aroma_canvas_push_op() in aroma_canvas.c). Raise this if a given
 * canvas legitimately needs to queue more shapes/text than this before
 * its next clear.
 */
#define AROMA_CANVAS_MAX_OPS 64

    typedef struct
    {
        AromaCanvasOp ops[AROMA_CANVAS_MAX_OPS];
        size_t op_count;
    } AromaCanvas;

    AromaNode *aroma_canvas_create(AromaNode *parent, int x, int y, int width, int height);
    void aroma_canvas_draw_rect(AromaNode *node, int x, int y, int width, int height, int border_radius, bool is_rect_filled, uint32_t color);
    void aroma_canvas_draw_circle(AromaNode *node, int center_x, int center_y, int radius, uint32_t color);
    void aroma_canvas_draw_line(AromaNode *node, int x1, int y1, int x2, int y2, uint32_t color, int thickness);
    void aroma_canvas_draw_text(AromaNode *node, const char *text, int x, int y, uint32_t color, AromaFont *font);

    /**
     * @brief Clear the canvas's queued draw ops and record a background
     * clear-color op in their place.
     *
     * This empties the accumulated op list (previously it just
     * overwrote the single pending draw_mode/color pair, since that was
     * all a canvas could hold). Call this at the start of a frame if you
     * want the canvas to start empty rather than carrying over ops queued
     * on a previous frame -- draw_* calls do not implicitly clear, and
     * this is also how you reclaim slots once AROMA_CANVAS_MAX_OPS fills
     * up.
     *
     * @param node Canvas node to clear.
     * @param color Clear color in 0xAARRGGBB format.
     */
    void aroma_canvas_clear(AromaNode *node, uint32_t color);

    /**
     * @brief Draw a stroked (unfilled) arc -- a partial ring, e.g. for a
     * gauge's dial sweep.
     *
     * Unlike AROMA_CANVAS_DRAW_MODE_CIRCLE (which fakes a FILLED disc by
     * forcing a full 0..2*PI sweep with thickness == radius, see the
     * comment in aroma_canvas_draw()'s CIRCLE case), this draws a genuine
     * partial arc: a ring segment from start_angle to end_angle, stroked
     * at the given thickness, with the ring's own radius left independent
     * of thickness. Passing start_angle=0, end_angle=2*PI here draws a
     * full ring outline (a stroked circle, not a filled one) rather than
     * a filled disc -- for a filled disc, use aroma_canvas_draw_circle().
     *
     * Each call appends a new op rather than replacing a previous one, so
     * drawing several arcs (or an arc after a rect, etc.) on the same
     * canvas node renders all of them, in call order, up to
     * AROMA_CANVAS_MAX_OPS.
     *
     * @param node Canvas node to draw on.
     * @param center_x X-coordinate of the arc's center.
     * @param center_y Y-coordinate of the arc's center.
     * @param radius Radius of the arc's ring.
     * @param start_angle Sweep start, in RADIANS. 0 = +x axis, matching
     *        the existing CIRCLE-mode convention in aroma_canvas.c (NOT
     *        the degrees documented on the unrelated aroma_drawlist_cmd_arc()
     *        function -- AromaCanvas does not use the AromaDrawList layer).
     * @param end_angle Sweep end, in RADIANS, same convention as start_angle.
     * @param color Arc color in 0xAARRGGBB format.
     * @param thickness Stroke width of the ring, in pixels.
     */
    void aroma_canvas_draw_arc(AromaNode *node, int center_x, int center_y, int radius, float start_angle, float end_angle, uint32_t color, int thickness);

#ifdef __cplusplus
}
#endif

#endif