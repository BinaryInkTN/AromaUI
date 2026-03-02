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

#include "widgets/aroma_container.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_node.h"
#include "core/aroma_event.h"
#include "core/aroma_timer.h"
#include "core/aroma_time.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <string.h>
#include <math.h>

/* ── constants ─────────────────────────────────────────────────────────── */

#define SCROLLBAR_WIDTH         4
#define SCROLLBAR_MIN_THUMB    24
#define SCROLLBAR_PADDING       2
#define SCROLL_SPEED_DEFAULT    1.0f
#define SCROLL_SLOP             8      /* px before scroll takes over       */

/* physics */
#define FLING_FRICTION          0.985f /* velocity *= friction each tick    */
#define FLING_MIN_VELOCITY      0.5f   /* stop fling below this px/tick     */
#define FLING_TICK_MS           16     /* ~60 fps animation timer           */
#define OVERSCROLL_RESIST       0.35f  /* rubber-band damping factor        */
#define OVERSCROLL_MAX_PX       120    /* clamp overscroll extent           */
#define BOUNCE_DURATION_MS      250    /* snap-back animation duration      */

/* scrollbar appearance */
#define SCROLLBAR_FADE_DELAY_MS 1200   /* ms idle before fade starts        */
#define SCROLLBAR_FADE_MS       400    /* duration of fade transition       */
#define SCROLLBAR_ACTIVE_ALPHA  0xCC   /* visible alpha                     */

/* ── internal struct ───────────────────────────────────────────────────── */

typedef struct AromaContainer {
    AromaRect rect;

    uint32_t bg_color;
    bool draw_background;

    /* ── scroll fields (only active when scrollable == true) ───────── */
    bool scrollable;
    bool auto_content_size;        /* true = measure children each layout */

    int content_width;
    int content_height;

    /* Scroll position stored as float for sub-pixel fling accuracy.
       The int helpers round these for pixel-snapped drawing. */
    float scroll_fx;
    float scroll_fy;

    float velocity_x;
    float velocity_y;

    /* Overscroll offset (allowed to go negative / beyond max).
       Added on top of clamped scroll during rubber-band. */
    float overscroll_x;
    float overscroll_y;

    AromaScrollDirection direction;

    bool show_scrollbar;
    uint32_t scrollbar_color;
    float scroll_speed;

    /* touch / drag state */
    bool is_dragging;
    int drag_start_x;
    int drag_start_y;
    float drag_scroll_start_x;
    float drag_scroll_start_y;
    int last_touch_x;
    int last_touch_y;
    uint64_t last_touch_time_ms;
    int active_pointer_id;

    /* animation timer */
    AromaTimer* fling_timer;
    AromaNode* self_node;          /* back-pointer for timer callback     */

    /* bounce-back state */
    bool bouncing;
    float bounce_start_x;
    float bounce_start_y;
    uint64_t bounce_start_time;

    /* scrollbar fade */
    uint64_t last_scroll_time;     /* last time scroll position changed   */
    float scrollbar_opacity;       /* 0.0 – 1.0 current opacity           */

    /* dirty tracking */
    int prev_scroll_x;
    int prev_scroll_y;
    bool content_dirty;
} AromaContainer;


/* ── scroll helpers ────────────────────────────────────────────────────── */

static inline int scroll_x_int(AromaContainer* c) { return (int)roundf(c->scroll_fx); }
static inline int scroll_y_int(AromaContainer* c) { return (int)roundf(c->scroll_fy); }

static inline int max_scroll_x(AromaContainer* c) {
    int m = c->content_width - c->rect.width;
    return m > 0 ? m : 0;
}
static inline int max_scroll_y(AromaContainer* c) {
    int m = c->content_height - c->rect.height;
    return m > 0 ? m : 0;
}

static void clamp_scroll(AromaContainer* c)
{
    float mx = (float)max_scroll_x(c);
    float my = (float)max_scroll_y(c);
    if (c->scroll_fx < 0.0f) c->scroll_fx = 0.0f;
    if (c->scroll_fy < 0.0f) c->scroll_fy = 0.0f;
    if (c->scroll_fx > mx) c->scroll_fx = mx;
    if (c->scroll_fy > my) c->scroll_fy = my;
}

static bool point_in_rect(int px, int py, const AromaRect* r)
{
    return px >= r->x && px < r->x + r->width &&
           py >= r->y && py < r->y + r->height;
}

/* Return effective scroll position including overscroll rubber-band. */
static inline int effective_scroll_x(AromaContainer* c) {
    return scroll_x_int(c) + (int)roundf(c->overscroll_x);
}
static inline int effective_scroll_y(AromaContainer* c) {
    return scroll_y_int(c) + (int)roundf(c->overscroll_y);
}


/* ── animation tick (fling / bounce / scrollbar fade) ──────────────────── */

static void stop_fling(AromaContainer* c)
{
    if (c->fling_timer) {
        aroma_timer_cancel(c->fling_timer);
        c->fling_timer = NULL;
    }
    c->bouncing = false;
}

static void fling_tick_cb(void* user_data)
{
    AromaNode* node = (AromaNode*)user_data;
    AromaContainer* c = aroma_container_get(node);
    if (!c || !c->scrollable) return;

    bool still_moving = false;
    uint64_t now = aroma_time_now_ms();

    /* ── bounce-back phase ──────────────────────────────────────────── */
    if (c->bouncing) {
        float elapsed = (float)(now - c->bounce_start_time);
        float t = elapsed / (float)BOUNCE_DURATION_MS;
        if (t >= 1.0f) t = 1.0f;

        /* ease-out cubic */
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        c->overscroll_x = c->bounce_start_x * (1.0f - ease);
        c->overscroll_y = c->bounce_start_y * (1.0f - ease);

        if (t >= 1.0f) {
            c->overscroll_x = 0.0f;
            c->overscroll_y = 0.0f;
            c->bouncing = false;
        } else {
            still_moving = true;
        }
        c->content_dirty = true;
        aroma_node_invalidate(node);
    }

    /* ── fling (momentum) phase ─────────────────────────────────────── */
    if (!c->bouncing && !c->is_dragging) {
        bool vel_active = false;

        if (c->direction & AROMA_SCROLL_HORIZONTAL) {
            c->velocity_x *= FLING_FRICTION;
            if (fabsf(c->velocity_x) > FLING_MIN_VELOCITY) {
                c->scroll_fx += c->velocity_x;
                vel_active = true;
            } else {
                c->velocity_x = 0.0f;
            }
        }
        if (c->direction & AROMA_SCROLL_VERTICAL) {
            c->velocity_y *= FLING_FRICTION;
            if (fabsf(c->velocity_y) > FLING_MIN_VELOCITY) {
                c->scroll_fy += c->velocity_y;
                vel_active = true;
            } else {
                c->velocity_y = 0.0f;
            }
        }

        /* If we flowed past bounds during fling, start overscroll */
        float mx = (float)max_scroll_x(c);
        float my = (float)max_scroll_y(c);

        if (c->scroll_fx < 0.0f) {
            c->overscroll_x = c->scroll_fx;
            c->scroll_fx = 0.0f;
            c->velocity_x = 0.0f;
        } else if (c->scroll_fx > mx) {
            c->overscroll_x = c->scroll_fx - mx;
            c->scroll_fx = mx;
            c->velocity_x = 0.0f;
        }
        if (c->scroll_fy < 0.0f) {
            c->overscroll_y = c->scroll_fy;
            c->scroll_fy = 0.0f;
            c->velocity_y = 0.0f;
        } else if (c->scroll_fy > my) {
            c->overscroll_y = c->scroll_fy - my;
            c->scroll_fy = my;
            c->velocity_y = 0.0f;
        }

        /* Start bounce-back if we have overscroll */
        if ((c->overscroll_x != 0.0f || c->overscroll_y != 0.0f) && !c->bouncing) {
            c->bouncing = true;
            c->bounce_start_x = c->overscroll_x;
            c->bounce_start_y = c->overscroll_y;
            c->bounce_start_time = now;
            still_moving = true;
        } else if (vel_active) {
            still_moving = true;
        }

        if (vel_active) {
            c->content_dirty = true;
            c->last_scroll_time = now;
            aroma_node_invalidate(node);
        }
    }

    /* ── scrollbar fade ─────────────────────────────────────────────── */
    if (c->show_scrollbar) {
        uint64_t since_scroll = now - c->last_scroll_time;
        float target = 0.0f;
        if (c->is_dragging || still_moving || since_scroll < SCROLLBAR_FADE_DELAY_MS) {
            target = 1.0f;
        }
        /* Lerp opacity toward target */
        float speed = (target > c->scrollbar_opacity) ? 0.3f : 0.08f;
        c->scrollbar_opacity += (target - c->scrollbar_opacity) * speed;
        if (c->scrollbar_opacity < 0.01f) c->scrollbar_opacity = 0.0f;
        if (c->scrollbar_opacity > 0.99f) c->scrollbar_opacity = 1.0f;

        if (c->scrollbar_opacity > 0.0f) still_moving = true;
        c->content_dirty = true;
        aroma_node_invalidate(node);
    }

    if (!still_moving) {
        stop_fling(c);
    }
}

static void ensure_animation_timer(AromaContainer* c)
{
    if (!c->fling_timer && c->self_node) {
        c->fling_timer = aroma_timer_create(
            FLING_TICK_MS, true, fling_tick_cb, c->self_node);
    }
}


/* ── scroll event handler ──────────────────────────────────────────────── */

static bool scroll_event_handler(AromaEvent* event, void* user_data)
{
    AromaNode* node = (AromaNode*)user_data;
    if (!node) return false;

    AromaContainer* c = aroma_container_get(node);
    if (!c || !c->scrollable) return false;

    /* No scrolling needed when content fits in viewport */
    bool can_scroll_h = (c->direction & AROMA_SCROLL_HORIZONTAL) && c->content_width  > c->rect.width;
    bool can_scroll_v = (c->direction & AROMA_SCROLL_VERTICAL)   && c->content_height > c->rect.height;

    if (!can_scroll_h && !can_scroll_v) return false;

    switch (event->event_type) {
        case EVENT_TYPE_TOUCH_DOWN: {
            int tx = event->data.touch.x;
            int ty = event->data.touch.y;
            if (!point_in_rect(tx, ty, &c->rect)) return false;

            /* Stop any active fling/bounce immediately */
            stop_fling(c);
            c->overscroll_x = 0.0f;
            c->overscroll_y = 0.0f;
            c->velocity_x = 0.0f;
            c->velocity_y = 0.0f;

            c->drag_start_x = tx;
            c->drag_start_y = ty;
            c->drag_scroll_start_x = c->scroll_fx;
            c->drag_scroll_start_y = c->scroll_fy;
            c->last_touch_x = tx;
            c->last_touch_y = ty;
            c->last_touch_time_ms = aroma_time_now_ms();
            c->active_pointer_id = event->data.touch.id;
            c->is_dragging = false;
            c->scrollbar_opacity = 1.0f;
            c->last_scroll_time = c->last_touch_time_ms;
            return false;   /* let children handle too */
        }

        case EVENT_TYPE_TOUCH_MOVE: {
            if (event->data.touch.id != c->active_pointer_id) return false;

            int tx = event->data.touch.x;
            int ty = event->data.touch.y;
            int dx = c->drag_start_x - tx;
            int dy = c->drag_start_y - ty;

            if (!c->is_dragging) {
                int abs_dx = dx < 0 ? -dx : dx;
                int abs_dy = dy < 0 ? -dy : dy;
                bool slop_h = can_scroll_h && abs_dx >= SCROLL_SLOP;
                bool slop_v = can_scroll_v && abs_dy >= SCROLL_SLOP;
                if (!slop_h && !slop_v) return false;
                c->is_dragging = true;
            }

            /* Compute new scroll from drag delta */
            float new_sx = c->drag_scroll_start_x;
            float new_sy = c->drag_scroll_start_y;

            if (can_scroll_h) new_sx += (float)dx * c->scroll_speed;
            if (can_scroll_v) new_sy += (float)dy * c->scroll_speed;

            /* Rubber-band at edges */
            float mx = (float)max_scroll_x(c);
            float my = (float)max_scroll_y(c);

            c->overscroll_x = 0.0f;
            c->overscroll_y = 0.0f;

            if (new_sx < 0.0f) {
                c->overscroll_x = new_sx * OVERSCROLL_RESIST;
                if (c->overscroll_x < -OVERSCROLL_MAX_PX)
                    c->overscroll_x = -OVERSCROLL_MAX_PX;
                new_sx = 0.0f;
            } else if (new_sx > mx) {
                c->overscroll_x = (new_sx - mx) * OVERSCROLL_RESIST;
                if (c->overscroll_x > OVERSCROLL_MAX_PX)
                    c->overscroll_x = OVERSCROLL_MAX_PX;
                new_sx = mx;
            }
            if (new_sy < 0.0f) {
                c->overscroll_y = new_sy * OVERSCROLL_RESIST;
                if (c->overscroll_y < -OVERSCROLL_MAX_PX)
                    c->overscroll_y = -OVERSCROLL_MAX_PX;
                new_sy = 0.0f;
            } else if (new_sy > my) {
                c->overscroll_y = (new_sy - my) * OVERSCROLL_RESIST;
                if (c->overscroll_y > OVERSCROLL_MAX_PX)
                    c->overscroll_y = OVERSCROLL_MAX_PX;
                new_sy = my;
            }

            /* Track velocity for fling: use short-window delta */
            uint64_t now = aroma_time_now_ms();
            uint64_t dt = now - c->last_touch_time_ms;
            if (dt > 0 && dt < 200) {
                float vx = (float)(c->last_touch_x - tx) / (float)dt * FLING_TICK_MS;
                float vy = (float)(c->last_touch_y - ty) / (float)dt * FLING_TICK_MS;
                /* Exponential moving average for smoother velocity */
                c->velocity_x = c->velocity_x * 0.4f + vx * 0.6f;
                c->velocity_y = c->velocity_y * 0.4f + vy * 0.6f;
            }
            c->last_touch_x = tx;
            c->last_touch_y = ty;
            c->last_touch_time_ms = now;

            c->scroll_fx = new_sx;
            c->scroll_fy = new_sy;
            c->content_dirty = true;
            c->last_scroll_time = now;
            aroma_node_invalidate(node);
            return true;
        }

        case EVENT_TYPE_TOUCH_UP: {
            if (event->data.touch.id != c->active_pointer_id) return false;
            bool was_dragging = c->is_dragging;
            c->is_dragging = false;
            c->active_pointer_id = -1;

            if (was_dragging) {
                /* If finger was stationary before lift, zero velocity
                   (prevents phantom fling from stale velocity samples) */
                uint64_t up_now = aroma_time_now_ms();
                if (up_now - c->last_touch_time_ms > 100) {
                    c->velocity_x = 0.0f;
                    c->velocity_y = 0.0f;
                }

                /* Clamp fling velocity */
                float max_vel = 80.0f;
                if (c->velocity_x >  max_vel) c->velocity_x =  max_vel;
                if (c->velocity_x < -max_vel) c->velocity_x = -max_vel;
                if (c->velocity_y >  max_vel) c->velocity_y =  max_vel;
                if (c->velocity_y < -max_vel) c->velocity_y = -max_vel;

                /* If overscroll is active, start bounce immediately */
                if (c->overscroll_x != 0.0f || c->overscroll_y != 0.0f) {
                    c->bouncing = true;
                    c->bounce_start_x = c->overscroll_x;
                    c->bounce_start_y = c->overscroll_y;
                    c->bounce_start_time = aroma_time_now_ms();
                    c->velocity_x = 0.0f;
                    c->velocity_y = 0.0f;
                }
                /* Otherwise use velocity to start fling */
                c->last_scroll_time = aroma_time_now_ms();
                ensure_animation_timer(c);
            }
            return was_dragging;
        }

        case EVENT_TYPE_MOUSE_MOVE: {
            int mx = event->data.mouse.x;
            int my = event->data.mouse.y;
            if (!point_in_rect(mx, my, &c->rect)) return false;

            int dy = event->data.mouse.delta_y;
            int dx = event->data.mouse.delta_x;
            if (dy == 0 && dx == 0) return false;

            float old_sx = c->scroll_fx;
            float old_sy = c->scroll_fy;

            if (can_scroll_v) c->scroll_fy += (float)dy * c->scroll_speed;
            if (can_scroll_h) c->scroll_fx += (float)dx * c->scroll_speed;
            clamp_scroll(c);

            if (c->scroll_fx != old_sx || c->scroll_fy != old_sy) {
                c->content_dirty = true;
                c->last_scroll_time = aroma_time_now_ms();
                c->scrollbar_opacity = 1.0f;
                ensure_animation_timer(c);
                aroma_node_invalidate(node);
            }
            return true;
        }

        default:
            break;
    }
    return false;
}


/* ── scrollbar drawing ─────────────────────────────────────────────────── */

static void draw_scrollbar_indicators(AromaContainer* c, size_t window_id)
{
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle) return;
    if (c->scrollbar_opacity <= 0.01f) return;

    /* Compute alpha from base color and current fade opacity */
    uint32_t base = c->scrollbar_color;
    uint8_t base_alpha = base & 0xFF;
    uint8_t alpha = (uint8_t)(base_alpha * c->scrollbar_opacity);
    uint32_t color = (base & 0xFFFFFF00u) | alpha;

    int eff_sx = effective_scroll_x(c);
    int eff_sy = effective_scroll_y(c);

    /* Vertical scrollbar */
    if ((c->direction & AROMA_SCROLL_VERTICAL) && c->content_height > c->rect.height) {
        float visible_ratio = (float)c->rect.height / (float)c->content_height;
        int thumb_height = (int)(c->rect.height * visible_ratio);
        if (thumb_height < SCROLLBAR_MIN_THUMB) thumb_height = SCROLLBAR_MIN_THUMB;

        int max_scroll = c->content_height - c->rect.height;
        float scroll_ratio = max_scroll > 0
            ? (float)eff_sy / (float)max_scroll
            : 0.0f;
        if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

        int track_height = c->rect.height - thumb_height;
        int thumb_y = c->rect.y + (int)(track_height * scroll_ratio);
        int bar_x = c->rect.x + c->rect.width - SCROLLBAR_WIDTH - SCROLLBAR_PADDING;

        gfx->fill_rectangle(window_id, bar_x, thumb_y,
                            SCROLLBAR_WIDTH, thumb_height,
                            color, true, (float)(SCROLLBAR_WIDTH / 2));
    }

    /* Horizontal scrollbar */
    if ((c->direction & AROMA_SCROLL_HORIZONTAL) && c->content_width > c->rect.width) {
        float visible_ratio = (float)c->rect.width / (float)c->content_width;
        int thumb_width = (int)(c->rect.width * visible_ratio);
        if (thumb_width < SCROLLBAR_MIN_THUMB) thumb_width = SCROLLBAR_MIN_THUMB;

        int max_scroll = c->content_width - c->rect.width;
        float scroll_ratio = max_scroll > 0
            ? (float)eff_sx / (float)max_scroll
            : 0.0f;
        if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

        int track_width = c->rect.width - thumb_width;
        int thumb_x = c->rect.x + (int)(track_width * scroll_ratio);
        int bar_y = c->rect.y + c->rect.height - SCROLLBAR_WIDTH - SCROLLBAR_PADDING;

        gfx->fill_rectangle(window_id, thumb_x, bar_y,
                            thumb_width, SCROLLBAR_WIDTH,
                            color, true, (float)(SCROLLBAR_WIDTH / 2));
    }
}


/* ── public API : creation / destruction ───────────────────────────────── */

AromaNode* aroma_container_create(AromaNode* parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid container parameters");
        return NULL;
    }

    AromaContainer* container = (AromaContainer*)aroma_widget_alloc(sizeof(AromaContainer));
    if (!container) {
        LOG_ERROR("Failed to allocate memory for container");
        return NULL;
    }

    memset(container, 0, sizeof(AromaContainer));
    container->rect.x = x;
    container->rect.y = y;
    container->rect.width = width;
    container->rect.height = height;
    container->draw_background = false;

    /* scroll defaults (inactive until set_scrollable(true)) */
    container->scrollable = false;
    container->auto_content_size = true;
    container->content_width = width;
    container->content_height = height;
    container->scroll_fx = 0.0f;
    container->scroll_fy = 0.0f;
    container->direction = AROMA_SCROLL_VERTICAL;
    container->scroll_speed = SCROLL_SPEED_DEFAULT;
    container->show_scrollbar = true;
    container->scrollbar_color = 0x88888880;
    container->scrollbar_opacity = 0.0f;
    container->active_pointer_id = -1;
    container->content_dirty = true;
    container->self_node = NULL;

    AromaNode* node = __add_child_node(NODE_TYPE_CONTAINER, parent, container);
    if (!node) {
        aroma_widget_free(container);
        LOG_ERROR("Failed to create container node");
        return NULL;
    }

    container->self_node = node;
    aroma_node_set_draw_cb(node, aroma_container_draw);
    aroma_node_invalidate(node);

    return node;
}

void aroma_container_destroy(AromaNode* container_node)
{
    if (!container_node) return;
    if (container_node->node_widget_ptr) {
        aroma_widget_free(container_node->node_widget_ptr);
        container_node->node_widget_ptr = NULL;
    }
    __destroy_node(container_node);
}


/* ── public API : geometry ─────────────────────────────────────────────── */

void aroma_container_set_rect(AromaNode* container_node, int x, int y, int width, int height)
{
    AromaContainer* container = aroma_container_get(container_node);
    if (!container) return;

    if (container->rect.x == x && container->rect.y == y &&
        container->rect.width == width && container->rect.height == height) {
        return;
    }

    container->rect.x = x;
    container->rect.y = y;
    container->rect.width = width;
    container->rect.height = height;

    if (container->scrollable) clamp_scroll(container);

    for (uint64_t i = 0; i < container_node->child_count; i++) {
        AromaNode* child = container_node->child_nodes[i];
        if (child && !child->is_hidden) {
            aroma_node_update_layout(child, x, y, width, height);
        }
    }

    aroma_node_invalidate(container_node);
}

AromaRect aroma_container_get_rect(AromaNode* container_node)
{
    AromaContainer* container = aroma_container_get(container_node);
    if (!container) {
        AromaRect empty = {0};
        return empty;
    }
    return container->rect;
}


/* ── public API : background ───────────────────────────────────────────── */

void aroma_container_set_debug_bg(AromaNode* container_node, uint32_t color)
{
    AromaContainer* container = aroma_container_get(container_node);
    if (!container) return;

    container->bg_color = color;
    container->draw_background = true;
    aroma_node_invalidate(container_node);
}


/* ── public API : scroll mode ──────────────────────────────────────────── */

void aroma_container_set_scrollable(AromaNode* node, bool scrollable)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;
    if (c->scrollable == scrollable) return;

    c->scrollable = scrollable;

    if (scrollable) {
        /* Subscribe to input events for scrolling */
        aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_MOVE, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP,   scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, scroll_event_handler, node, 0);
    }
    /* Note: we do not unsubscribe when disabling – the handler early-outs. */

    aroma_node_invalidate(node);
}

void aroma_container_set_content_size(AromaNode* node, int content_width, int content_height)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;

    int cw = content_width  > 0 ? content_width  : c->rect.width;
    int ch = content_height > 0 ? content_height : c->rect.height;

    if (c->content_width == cw && c->content_height == ch) return;

    c->content_width  = cw;
    c->content_height = ch;
    c->auto_content_size = false;   /* explicit size disables auto */
    clamp_scroll(c);
    c->content_dirty = true;
    aroma_node_invalidate(node);
}

void aroma_container_set_scroll_direction(AromaNode* node, AromaScrollDirection direction)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;
    c->direction = direction;
}

void aroma_container_get_scroll(AromaNode* node, int* scroll_x, int* scroll_y)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) {
        if (scroll_x) *scroll_x = 0;
        if (scroll_y) *scroll_y = 0;
        return;
    }
    if (scroll_x) *scroll_x = scroll_x_int(c);
    if (scroll_y) *scroll_y = scroll_y_int(c);
}

void aroma_container_set_scroll(AromaNode* node, int scroll_x, int scroll_y)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;

    float old_sx = c->scroll_fx;
    float old_sy = c->scroll_fy;
    c->scroll_fx = (float)scroll_x;
    c->scroll_fy = (float)scroll_y;
    clamp_scroll(c);

    if (c->scroll_fx != old_sx || c->scroll_fy != old_sy) {
        c->content_dirty = true;
        c->last_scroll_time = aroma_time_now_ms();
        c->scrollbar_opacity = 1.0f;
        ensure_animation_timer(c);
        aroma_node_invalidate(node);
    }
}

void aroma_container_scroll_by(AromaNode* node, int dx, int dy)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;
    aroma_container_set_scroll(node, scroll_x_int(c) + dx, scroll_y_int(c) + dy);
}

void aroma_container_set_scroll_speed(AromaNode* node, float speed)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;
    c->scroll_speed = speed > 0.0f ? speed : SCROLL_SPEED_DEFAULT;
}

void aroma_container_show_scrollbar(AromaNode* node, bool show)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;
    c->show_scrollbar = show;
    aroma_node_invalidate(node);
}

void aroma_container_set_scrollbar_color(AromaNode* node, uint32_t color)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) return;
    c->scrollbar_color = color;
    aroma_node_invalidate(node);
}


/* ── query helpers (used by layout engine) ─────────────────────────────── */

bool aroma_container_is_scrollable(AromaNode* node)
{
    AromaContainer* c = aroma_container_get(node);
    return c ? c->scrollable : false;
}

void aroma_container_get_content_size(AromaNode* node, int* out_w, int* out_h)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    if (out_w) *out_w = c->content_width;
    if (out_h) *out_h = c->content_height;
}


/* ── auto content size measurement ─────────────────────────────────────── */

void aroma_container_update_auto_content_size(AromaNode* node)
{
    AromaContainer* c = aroma_container_get(node);
    if (!c || !c->scrollable || !c->auto_content_size) return;

    /* Walk all visible children and find the max extent relative to
       the container's origin.  This gives us the actual content size. */
    int max_w = 0;
    int max_h = 0;

    for (uint64_t i = 0; i < node->child_count; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden || !child->node_widget_ptr) continue;

        AromaRect* cr = (AromaRect*)child->node_widget_ptr;
        int right  = (cr->x - c->rect.x) + cr->width;
        int bottom = (cr->y - c->rect.y) + cr->height;

        if (right  > max_w) max_w = right;
        if (bottom > max_h) max_h = bottom;
    }

    /* Also account for gap after the last child */
    if (node->layout.gap > 0 && node->child_count > 0) {
        /* The gap is already included in positioning, but the last item's
           measurement doesn't include trailing space. No action needed. */
    }

    /* Ensure content is at least viewport-sized */
    if (max_w < c->rect.width)  max_w = c->rect.width;
    if (max_h < c->rect.height) max_h = c->rect.height;

    if (c->content_width != max_w || c->content_height != max_h) {
        c->content_width  = max_w;
        c->content_height = max_h;
        clamp_scroll(c);
        c->content_dirty = true;
    }
}


/* ── recursive subtree shift (for scroll offset) ──────────────────────── */

static void shift_subtree(AromaNode* node, int dx, int dy)
{
    if (!node) return;
    if (node->node_widget_ptr) {
        AromaRect* r = (AromaRect*)node->node_widget_ptr;
        r->x += dx;
        r->y += dy;
    }
    for (uint64_t i = 0; i < node->child_count; i++) {
        if (node->child_nodes[i])
            shift_subtree(node->child_nodes[i], dx, dy);
    }
}


/* ── draw ──────────────────────────────────────────────────────────────── */

void aroma_container_draw(AromaNode* container_node, size_t window_id)
{
    AromaContainer* c = aroma_container_get(container_node);
    if (!c) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();

    /* Background */
    if (c->draw_background && gfx && gfx->fill_rectangle) {
        gfx->fill_rectangle(window_id,
                            c->rect.x, c->rect.y,
                            c->rect.width, c->rect.height,
                            c->bg_color, false, 0.0f);
    }

    /* If scrollable, enable scissor and apply scroll offset to children */
    if (c->scrollable) {
        if (gfx && gfx->graphics_set_clip) {
            gfx->graphics_set_clip(c->rect.x, c->rect.y,
                                   c->rect.width, c->rect.height);
        }

        int eff_sx = effective_scroll_x(c);
        int eff_sy = effective_scroll_y(c);

        for (uint64_t i = 0; i < container_node->child_count; i++) {
            if (container_node->child_nodes[i])
                shift_subtree(container_node->child_nodes[i],
                              -eff_sx, -eff_sy);
        }
    }

    /* Draw children */
    for (uint64_t i = 0; i < container_node->child_count; i++) {
        AromaNode* child = container_node->child_nodes[i];
        if (!child || child->is_hidden) continue;

        AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(child);
        if (draw_cb) {
            draw_cb(child, window_id);
        }
    }

    /* Restore scroll offset and disable scissor */
    if (c->scrollable) {
        int eff_sx = effective_scroll_x(c);
        int eff_sy = effective_scroll_y(c);

        for (uint64_t i = 0; i < container_node->child_count; i++) {
            if (container_node->child_nodes[i])
                shift_subtree(container_node->child_nodes[i],
                              eff_sx, eff_sy);
        }
        if (gfx && gfx->graphics_clear_clip) {
            gfx->graphics_clear_clip();
        }
    }

    /* Scrollbar indicators (drawn outside scissor region) */
    if (c->scrollable && c->show_scrollbar) {
        draw_scrollbar_indicators(c, window_id);
    }

    /* Update dirty tracking */
    if (c->scrollable) {
        c->prev_scroll_x = scroll_x_int(c);
        c->prev_scroll_y = scroll_y_int(c);
        c->content_dirty = false;
    }
}