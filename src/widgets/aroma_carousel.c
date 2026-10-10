#include "widgets/aroma_carousel.h"
#include "widgets/aroma_container.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_time.h"
#include "core/aroma_timer.h"
#include "core/aroma_velocity.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif









static inline int tdp(int v)
{
#ifdef __ANDROID__
    return aroma_android_dp_to_px(v);
#else
    return v;
#endif
}

static inline float tdp_f(float v)
{
#ifdef __ANDROID__
    return aroma_android_dp_to_px_f(v);
#else
    return v;
#endif
}

#define CAR_DOT_AREA 48
#define CAR_SLOP_DP 8
#define CAR_FLING_MIN_PPS 500.0f
#define CAR_SNAP_FRACTION 0.35f
#define CAR_EDGE_RESIST 0.35f
#define CAR_ANIM_MS 16

struct AromaCarousel {
    AromaRect rect;
    int current;
    AromaFont *font;
    bool use_theme_colors;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t dot_color;
    uint32_t dot_active;
    AromaCarouselPageCb on_change;
    void *user_data;
    int active_pointer_id;
    int down_x;
    int down_y;
    AromaVelocityTracker vt;
    bool dragging;
    float drag_dx;
    float anim_from;
    float anim_to;
    int anim_target;
    uint64_t anim_start;
    AromaTimer *anim_timer;
    AromaNode *self_node;
    int base_x;
};

static void car_adjust(AromaEvent *e, int *x, int *y)
{
    *x = e->data.mouse.x;
    *y = e->data.mouse.y;
    if (e->event_type == EVENT_TYPE_TOUCH_DOWN ||
        e->event_type == EVENT_TYPE_TOUCH_UP ||
        e->event_type == EVENT_TYPE_TOUCH_MOVE) {
        *x = e->data.touch.x;
        *y = e->data.touch.y;
    }
    AromaNode *cur = e->target_node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int sx, sy;
            aroma_container_get_scroll(cur, &sx, &sy);
            *x += sx;
            *y += sy;
        }
        cur = cur->parent_node;
    }
}

static int car_count(AromaNode *node)
{
    if (!node)
        return 0;
    return (int)node->child_count;
}

static void car_rest_positions(AromaNode *node, AromaCarousel *c)
{
    for (uint64_t i = 0; i < node->child_count; i++) {
        AromaNode *ch = node->child_nodes[i];
        if (!ch)
            continue;
        AromaRect *r = aroma_node_get_rect(ch);
        if (r) {
            r->x = c->base_x;
        }
        aroma_layout_note_placed(ch);
        aroma_node_set_hidden(ch, (int)i != c->current);
    }
}


static void car_drag_positions(AromaNode *node, AromaCarousel *c)
{
    int n = car_count(node);
    int w = c->rect.width;
    for (int i = 0; i < n; i++) {
        AromaNode *ch = node->child_nodes[i];
        if (!ch)
            continue;
        AromaRect *r = aroma_node_get_rect(ch);
        if (r)
            r->x = c->base_x + (i - c->current) * w + (int)c->drag_dx;
        aroma_layout_note_placed(ch);
        aroma_node_set_hidden(ch, i < c->current - 1 || i > c->current + 1);
    }
}

static void car_fire(AromaNode *node, AromaCarousel *c)
{
    if (c->on_change)
        c->on_change(node, c->current, c->user_data);
}

static void car_kill_timer(AromaCarousel *c)
{
    if (c->anim_timer) {
        aroma_timer_cancel(c->anim_timer);
        c->anim_timer = NULL;
    }
}

static void car_anim_tick(void *ud)
{
    AromaNode *node = (AromaNode *)ud;
    if (!node || !node->node_widget_ptr)
        return;
    AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
    if (c->self_node != node)
        return;
    uint64_t now = aroma_time_now_ms();
    uint64_t dt = now - c->anim_start;
    float span_ms = 200.0f;
    float k = dt >= (uint64_t)span_ms ? 1.0f : (float)dt / span_ms;
    float ease = 1.0f - (1.0f - k) * (1.0f - k);
    c->drag_dx = c->anim_from + (c->anim_to - c->anim_from) * ease;
    car_drag_positions(node, c);
    if (k >= 1.0f) {
        car_kill_timer(c);
        c->current = c->anim_target;
        c->drag_dx = 0.0f;
        car_rest_positions(node, c);
        car_fire(node, c);
    }
    aroma_node_invalidate(node);
    aroma_ui_request_frame();
}

static void car_settle(AromaNode *node, AromaCarousel *c, int target)
{
    int n = car_count(node);
    if (target < 0)
        target = 0;
    if (target > n - 1)
        target = n - 1;
    car_kill_timer(c);
    c->anim_target = target;
    c->anim_from = c->drag_dx;
    c->anim_to = (float)(target - c->current) * (float)c->rect.width;
    c->anim_start = aroma_time_now_ms();
    c->anim_timer = aroma_timer_create(CAR_ANIM_MS, true, car_anim_tick,
                                       node);
    if (c->anim_timer)
        aroma_ui_request_frame();
    else {
        c->current = target;
        c->drag_dx = 0.0f;
        car_rest_positions(node, c);
        car_fire(node, c);
        aroma_node_invalidate(node);
    }
}

void aroma_carousel_set_page(AromaNode *node, int index)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
    int n = car_count(node);
    if (n <= 0)
        return;
    if (index < 0)
        index = 0;
    if (index >= n)
        index = n - 1;
    if (c->current != index) {
        car_kill_timer(c);
        c->dragging = false;
        c->drag_dx = 0.0f;
        c->current = index;
        car_rest_positions(node, c);
        aroma_node_invalidate(node);
    }
}

static bool car_dots_tap(AromaNode *node, AromaCarousel *c, int x, int y)
{
    int n = car_count(node);
    if (n <= 1)
        return false;
    int dot_top = c->rect.y + c->rect.height - CAR_DOT_AREA;
    if (y < dot_top || y >= c->rect.y + c->rect.height)
        return false;
    int dot_gap = tdp(20);
    if (dot_gap < 12)
        dot_gap = 12;
    int total_w = n * dot_gap;
    int sx = c->rect.x + (c->rect.width - total_w) / 2;
    int pad = tdp(6);
    for (int i = 0; i < n; i++) {
        if (x >= sx + i * dot_gap - pad && x < sx + i * dot_gap + dot_gap + pad) {
            if (i != c->current) {
                aroma_carousel_set_page(node, i);
                car_fire(node, c);
            }
            return true;
        }
    }
    return false;
}

static bool car_handle(AromaEvent *e, void *ud)
{
    AromaNode *node = (AromaNode *)ud;
    if (!node || !node->node_widget_ptr)
        return false;
    AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
    int x, y;
    car_adjust(e, &x, &y);
    bool in = x >= c->rect.x && x < c->rect.x + c->rect.width &&
              y >= c->rect.y && y < c->rect.y + c->rect.height;
    void (*rd)(void *) = (void (*)(void *))ud;
    (void)rd;
    int slop = tdp(CAR_SLOP_DP);
    int n = car_count(node);
    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
        return in;
    case EVENT_TYPE_MOUSE_EXIT:
        return false;
    case EVENT_TYPE_TOUCH_DOWN:
        if (!in || c->active_pointer_id != -1)
            return false;
        car_kill_timer(c);
        c->active_pointer_id = e->data.touch.id;
        c->down_x = x;
        c->down_y = y;
        c->dragging = false;
        c->drag_dx = 0.0f;
        aroma_velocity_reset(&c->vt);
        aroma_velocity_add(&c->vt, x, y, aroma_time_now_ms());
        if (n > 0) {
            AromaNode *cur = node->child_nodes[c->current];
            AromaRect *r = cur ? aroma_node_get_rect(cur) : NULL;
            c->base_x = r ? r->x : c->rect.x;
        }
        return true;
    case EVENT_TYPE_TOUCH_MOVE: {
        if (c->active_pointer_id == -1 ||
            c->active_pointer_id != e->data.touch.id)
            return c->active_pointer_id != -1;
        if (n <= 1)
            return true;
        if (!c->dragging) {
            int tdx = x - c->down_x;
            int tdy = y - c->down_y;
            if (tdx < 0)
                tdx = -tdx;
            if (tdy < 0)
                tdy = -tdy;
            if (tdx < slop || tdy >= tdx)
                return true;
            c->dragging = true;
        }
        aroma_velocity_add(&c->vt, x, y, aroma_time_now_ms());
        float raw = (float)(x - c->down_x);
        float w = (float)c->rect.width;
        float clamped = raw;
        if (c->current == 0 && raw > 0.0f)
            clamped = raw * CAR_EDGE_RESIST;
        else if (c->current == n - 1 && raw < 0.0f)
            clamped = raw * CAR_EDGE_RESIST;
        if (clamped > w)
            clamped = w;
        if (clamped < -w)
            clamped = -w;
        c->drag_dx = clamped;
        car_drag_positions(node, c);
        aroma_node_invalidate(node);
        return true;
    }
    case EVENT_TYPE_TOUCH_UP: {
        if (c->active_pointer_id != e->data.touch.id)
            return false;
        c->active_pointer_id = -1;
        if (n <= 1 || !in) {
            c->dragging = false;
            c->drag_dx = 0.0f;
            car_rest_positions(node, c);
            aroma_node_invalidate(node);
            return true;
        }
        if (!c->dragging) {
            c->drag_dx = 0.0f;
            if (car_dots_tap(node, c, x, y)) {
                aroma_node_invalidate(node);
                aroma_ui_request_redraw(NULL);
            }
            return true;
        }
        c->dragging = false;
        float vx = 0.0f;
        {
            float tx = 0.0f, ty = 0.0f;
            if (aroma_velocity_get(&c->vt, aroma_time_now_ms(), &tx, &ty))
                vx = tx;
        }
        float density = 1.0f;
#ifdef __ANDROID__
        density = aroma_android_get_density();
        if (density < 0.1f)
            density = 1.0f;
#endif
        int target = c->current;
        float w = (float)c->rect.width;
        if ((vx < 0 ? -vx : vx) >= CAR_FLING_MIN_PPS * density) {
            target = c->current + (vx < 0 ? 1 : -1);
        } else if (c->drag_dx <= -w * CAR_SNAP_FRACTION) {
            target = c->current + 1;
        } else if (c->drag_dx >= w * CAR_SNAP_FRACTION) {
            target = c->current - 1;
        }
        car_settle(node, c, target);
        aroma_ui_request_redraw(NULL);
        return true;
    }
    case EVENT_TYPE_MOUSE_CLICK: {
        if (c->active_pointer_id != -1 || !in)
            break;
        if (car_dots_tap(node, c, x, y)) {
            aroma_node_invalidate(node);
            aroma_ui_request_redraw(NULL);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

AromaNode *aroma_carousel_create(AromaNode *parent, int x, int y, int width,
                                 int height)
{
    if (!parent)
        return NULL;
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
    AromaCarousel *c = (AromaCarousel *)aroma_widget_alloc(sizeof(*c));
    if (!c)
        return NULL;
    memset(c, 0, sizeof(*c));
    c->rect.x = x;
    c->rect.y = y;
    c->rect.width = width > 0 ? width : 320;
    c->rect.height = height > 0 ? height : 200;
    c->current = 0;
    c->base_x = x;
    c->use_theme_colors = true;
    c->active_pointer_id = -1;
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, c);
    if (!node) {
        aroma_widget_free(c);
        return NULL;
    }
    c->self_node = node;
    aroma_node_set_draw_cb(node, aroma_carousel_draw);
    aroma_node_invalidate(node);
    return node;
}

AromaNode *aroma_carousel_add_page(AromaNode *car_node)
{
    if (!car_node || !car_node->node_widget_ptr)
        return NULL;
    AromaCarousel *c = (AromaCarousel *)car_node->node_widget_ptr;
    AromaNode *page = aroma_container_create(car_node, c->rect.x,
                                             c->rect.y,
                                             c->rect.width,
                                             c->rect.height - CAR_DOT_AREA);
    if (!page)
        return NULL;
    car_rest_positions(car_node, c);
    aroma_node_invalidate(car_node);
    return page;
}

int aroma_carousel_get_count(AromaNode *node)
{
    return car_count(node);
}

int aroma_carousel_get_page(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return -1;
    return ((AromaCarousel *)node->node_widget_ptr)->current;
}

void aroma_carousel_next(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
    if (c->current + 1 < car_count(node)) {
        c->current++;
        car_rest_positions(node, c);
        car_fire(node, c);
        aroma_node_invalidate(node);
    }
}

void aroma_carousel_prev(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
    if (c->current > 0) {
        c->current--;
        car_rest_positions(node, c);
        car_fire(node, c);
        aroma_node_invalidate(node);
    }
}

void aroma_carousel_set_on_change(AromaNode *n, AromaCarouselPageCb cb,
                                  void *ud)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCarousel *c = (AromaCarousel *)n->node_widget_ptr;
    c->on_change = cb;
    c->user_data = ud;
}

void aroma_carousel_set_font(AromaNode *n, AromaFont *font)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaCarousel *)n->node_widget_ptr)->font = font;
}

bool aroma_carousel_setup_events(AromaNode *n, void (*on_redraw)(void *),
                                 void *ud)
{
    (void)ud;
    (void)on_redraw;
    if (!n)
        return false;


    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_MOVE, car_handle, n,
                          80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_EXIT, car_handle, n,
                          80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_DOWN, car_handle, n,
                          90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_UP, car_handle, n,
                          90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_MOVE, car_handle, n,
                          80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_CLICK, car_handle, n,
                          90);
    return true;
}

void aroma_carousel_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node))
        return;
    AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle)
        return;
    if (c->use_theme_colors) {
        AromaTheme th = aroma_theme_get_global();
        c->bg_color = th.colors.surface;
        c->border_color = th.colors.border;
        c->dot_color = th.colors.border;
        c->dot_active = th.colors.primary;
    }
    gfx->fill_rectangle(window_id, c->rect.x, c->rect.y, c->rect.width,
                        c->rect.height, c->bg_color, true, tdp_f(12.0f));
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, c->rect.x, c->rect.y,
                                    c->rect.width, c->rect.height,
                                    c->border_color, tdp(1) > 0 ? tdp(1) : 1,
                                    true, tdp_f(12.0f));
    int n = car_count(node);
    if (n <= 0)
        return;
    int dot_h = tdp(8);
    if (dot_h < 4)
        dot_h = 4;
    int dot_gap = tdp(20);
    if (dot_gap < 12)
        dot_gap = 12;
    int dot_y = c->rect.y + c->rect.height - CAR_DOT_AREA +
                (CAR_DOT_AREA - dot_h) / 2;
    int total_w = n * dot_gap;
    int sx = c->rect.x + (c->rect.width - total_w) / 2;
    for (int i = 0; i < n; i++) {
        bool active = (i == c->current);
        int dw = active ? dot_h + tdp(6) : dot_h;
        int dx = sx + i * dot_gap + (dot_gap - dw) / 2;
        gfx->fill_rectangle(window_id, dx, dot_y, dw, dot_h,
                            active ? c->dot_active : c->dot_color, true,
                            (float)dot_h / 2.0f);
    }
}

void aroma_carousel_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr) {
        AromaCarousel *c = (AromaCarousel *)node->node_widget_ptr;
        car_kill_timer(c);
        c->self_node = NULL;
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}
