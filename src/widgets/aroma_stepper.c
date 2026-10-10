#include "widgets/aroma_stepper.h"
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

#define ST_SLOP_DP 8
#define ST_MIN_TOUCH_DP 48
#define ST_FLING_MIN_PPS 350.0f
#define ST_FLING_DIV_PPS 700.0f
#define ST_FLING_MAX_ROWS 8
#define ST_REPEAT_MS 120
#define ST_REPEAT_DELAY_TICKS 4
#define ST_ANIM_MS 16

struct AromaStepper {
    AromaRect rect;
    AromaStepperMode mode;
    int min_val;
    int max_val;
    int value;
    int step;
    bool wrap;
    char labels[AROMA_STEPPER_STEPS_MAX][AROMA_STEPPER_LABEL_MAX];
    int count;
    int step_index;
    AromaFont *font;
    bool use_theme_colors;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t text_color;
    uint32_t dim_color;
    uint32_t accent_color;
    uint32_t accent_text;
    AromaStepperChangeCb on_change;
    void *user_data;
    int active_pointer_id;
    bool mouse_down;
    int down_x;
    int down_y;
    int prev_y;
    AromaVelocityTracker vt;
    bool dragging;
    float offset_px;
    float anim_from;
    float anim_to;
    uint64_t anim_start;
    int anim_rows;
    AromaTimer *anim_timer;
    AromaTimer *repeat_timer;
    int repeat_ticks;
    int repeat_dir;
    AromaNode *self_node;
};

static void st_adjust(AromaEvent *e, int *x, int *y)
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

static int st_row_h(AromaStepper *s)
{
    int rh = s->rect.height / 3;
    return rh > 0 ? rh : 1;
}

static int st_span(AromaStepper *s)
{
    return s->max_val - s->min_val + 1;
}



static int st_slot(AromaStepper *s, int k)
{
    int v = s->value + k * s->step;
    if (s->wrap) {
        int span = st_span(s);
        if (span <= 0)
            return s->value;
        while (v < s->min_val)
            v += span;
        while (v > s->max_val)
            v -= span;
        return v;
    }
    if (v < s->min_val || v > s->max_val)
        return -1;
    return v;
}

static void st_fire(AromaNode *n, AromaStepper *s, int v)
{
    if (s->on_change)
        s->on_change(n, v, s->user_data);
}

static void st_kill_timers(AromaStepper *s)
{
    if (!s)
        return;
    if (s->anim_timer) {
        aroma_timer_cancel(s->anim_timer);
        s->anim_timer = NULL;
    }
    if (s->repeat_timer) {
        aroma_timer_cancel(s->repeat_timer);
        s->repeat_timer = NULL;
    }
}



static int st_landed_value(AromaStepper *s, int rows)
{
    int landed = s->value + rows * s->step;
    if (s->wrap) {
        int span = st_span(s);
        if (span <= 0)
            return s->value;
        while (landed < s->min_val)
            landed += span;
        while (landed > s->max_val)
            landed -= span;
    } else {
        if (landed < s->min_val)
            landed = s->min_val;
        if (landed > s->max_val)
            landed = s->max_val;
    }
    return landed;
}




static void st_commit_pending(AromaNode *node, AromaStepper *s)
{
    if (!s || !s->anim_timer)
        return;
    int landed = st_landed_value(s, s->anim_rows);
    st_kill_timers(s);
    s->offset_px = 0.0f;
    if (landed != s->value) {
        s->value = landed;
        st_fire(node, s, landed);
    }
    aroma_node_invalidate(node);
}


static void st_anim_tick(void *ud)
{
    AromaNode *node = (AromaNode *)ud;
    if (!node || !node->node_widget_ptr)
        return;
    AromaStepper *s = (AromaStepper *)node->node_widget_ptr;
    if (s->self_node != node)
        return;
    uint64_t now = aroma_time_now_ms();
    uint64_t dt = now - s->anim_start;
    float span_ms = 180.0f;
    float k = dt >= (uint64_t)span_ms ? 1.0f : (float)dt / span_ms;
    float ease = 1.0f - (1.0f - k) * (1.0f - k);
    s->offset_px = s->anim_from + (s->anim_to - s->anim_from) * ease;
    if (k >= 1.0f) {
        int landed = st_landed_value(s, s->anim_rows);
        st_kill_timers(s);
        s->offset_px = 0.0f;
        if (landed != s->value) {
            s->value = landed;
            st_fire(node, s, landed);
        }
    }
    aroma_node_invalidate(node);
    aroma_ui_request_frame();
}

static void st_start_anim(AromaStepper *s, float from, float to, int rows)
{
    st_kill_timers(s);
    s->anim_from = from;
    s->anim_to = to;
    s->anim_rows = rows;
    s->anim_start = aroma_time_now_ms();
    s->anim_timer = aroma_timer_create(ST_ANIM_MS, true, st_anim_tick,
                                       s->self_node);
    if (s->anim_timer)
        aroma_ui_request_frame();
}


static void st_snap(AromaNode *node, AromaStepper *s)
{
    (void)node;
    int rh = st_row_h(s);
    int rows = (int)((s->offset_px > 0 ? s->offset_px + rh / 2
                                       : s->offset_px - rh / 2) / rh);

    int target_rows = -rows;
    if (!s->wrap) {
        int lo = (s->min_val - s->value) / (s->step > 0 ? s->step : 1);
        int hi = (s->max_val - s->value) / (s->step > 0 ? s->step : 1);
        if (target_rows < lo)
            target_rows = lo;
        if (target_rows > hi)
            target_rows = hi;
    }
    st_start_anim(s, s->offset_px, (float)-target_rows * (float)rh,
                  target_rows);
}




static void st_finish_drag(AromaNode *node, AromaStepper *s, float v)
{
    float density = 1.0f;
#ifdef __ANDROID__
    density = aroma_android_get_density();
    if (density < 0.1f)
        density = 1.0f;
#endif
    if ((v > 0 ? v : -v) >= ST_FLING_MIN_PPS * density) {
        int rh = st_row_h(s);
        int rows = (int)(v / (ST_FLING_DIV_PPS * density));
        if (rows == 0)
            rows = v > 0 ? 1 : -1;
        if (rows > ST_FLING_MAX_ROWS)
            rows = ST_FLING_MAX_ROWS;
        if (rows < -ST_FLING_MAX_ROWS)
            rows = -ST_FLING_MAX_ROWS;
        int target_rows = -rows;
        if (!s->wrap) {
            int lo = (s->min_val - s->value) /
                     (s->step > 0 ? s->step : 1);
            int hi = (s->max_val - s->value) /
                     (s->step > 0 ? s->step : 1);
            if (target_rows < lo)
                target_rows = lo;
            if (target_rows > hi)
                target_rows = hi;
        }
        st_start_anim(s, s->offset_px,
                      (float)-target_rows * (float)rh, target_rows);
    } else {
        st_snap(node, s);
    }
}




static bool st_tap_step(AromaNode *node, AromaStepper *s, int dir)
{
    if (dir == 0)
        return false;
    int next = s->value + dir * s->step;
    if (s->wrap) {
        int span = st_span(s);
        while (next < s->min_val)
            next += span;
        while (next > s->max_val)
            next -= span;
    } else if (next < s->min_val || next > s->max_val) {
        return false;
    }
    s->value = next;
    st_fire(node, s, next);
    aroma_node_invalidate(node);
    return true;
}

static void st_repeat_tick(void *ud)
{
    AromaNode *node = (AromaNode *)ud;
    if (!node || !node->node_widget_ptr)
        return;
    AromaStepper *s = (AromaStepper *)node->node_widget_ptr;
    if (s->self_node != node || s->repeat_dir == 0)
        return;
    s->repeat_ticks++;
    if (s->repeat_ticks < ST_REPEAT_DELAY_TICKS)
        return;
    int next = s->value + s->repeat_dir * s->step;
    if (s->wrap) {
        int span = st_span(s);
        while (next < s->min_val)
            next += span;
        while (next > s->max_val)
            next -= span;
    } else {
        if (next < s->min_val || next > s->max_val)
            return;
    }
    s->value = next;
    st_fire(node, s, next);
    aroma_node_invalidate(node);
    aroma_ui_request_frame();
}


static int st_numeric_zone(AromaStepper *s, int y)
{
    int third = s->rect.height / 3;
    if (y < s->rect.y + third)
        return -1;
    if (y >= s->rect.y + s->rect.height - third)
        return 1;
    return 0;
}

static int st_steps_pick(AromaStepper *s, int x, int y)
{
    if (x < s->rect.x || x >= s->rect.x + s->rect.width || y < s->rect.y ||
        y >= s->rect.y + s->rect.height || s->count <= 0)
        return -1;
    int min_w = tdp(ST_MIN_TOUCH_DP);
    int seg = s->rect.width / s->count;
    if (seg < min_w)
        seg = min_w;
    int total = seg * s->count;
    int sx = s->rect.x + (s->rect.width - total) / 2;
    if (x < sx || x >= sx + total)
        return -1;
    int idx = (x - sx) / seg;
    if (idx < 0)
        idx = 0;
    if (idx >= s->count)
        idx = s->count - 1;
    return idx;
}

static bool st_handle(AromaEvent *e, void *ud)
{
    if (!e || !e->target_node)
        return false;





    if (e->event_type == EVENT_TYPE_TOUCH_UP &&
        e->data.touch.x == -1 && e->data.touch.y == -1) {
        AromaStepper *cs = (AromaStepper *)e->target_node->node_widget_ptr;
        if (!cs)
            return false;
        cs->active_pointer_id = -1;
        if (cs->repeat_timer) {
            aroma_timer_cancel(cs->repeat_timer);
            cs->repeat_timer = NULL;
        }
        cs->dragging = false;
        cs->repeat_dir = 0;
        cs->offset_px = 0.0f;
        aroma_node_invalidate(e->target_node);
        return true;
    }
    AromaStepper *s = (AromaStepper *)e->target_node->node_widget_ptr;
    if (!s)
        return false;
    AromaNode *node = e->target_node;
    int x, y;
    st_adjust(e, &x, &y);
    bool in = x >= s->rect.x && x < s->rect.x + s->rect.width &&
              y >= s->rect.y && y < s->rect.y + s->rect.height;
    void (*rd)(void *) = (void (*)(void *))ud;
    int slop = tdp(ST_SLOP_DP);
    if (s->mode == AROMA_STEPPER_STEPS) {
        switch (e->event_type) {
        case EVENT_TYPE_MOUSE_MOVE:
            return in;
        case EVENT_TYPE_MOUSE_EXIT:
            return false;
        case EVENT_TYPE_TOUCH_DOWN:
            if (!in || s->active_pointer_id != -1)
                return false;
            s->active_pointer_id = e->data.touch.id;
            s->down_x = x;
            s->down_y = y;
            return true;
        case EVENT_TYPE_TOUCH_MOVE:
            return s->active_pointer_id != -1 &&
                   s->active_pointer_id == e->data.touch.id;
        case EVENT_TYPE_TOUCH_UP: {
            if (s->active_pointer_id != e->data.touch.id)
                return false;
            s->active_pointer_id = -1;
            if (!in)
                return true;
            int dx = x - s->down_x;
            int dy = y - s->down_y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx >= slop || dy >= slop)
                return true;
            int idx = st_steps_pick(s, x, y);
            if (idx >= 0 && idx != s->step_index) {
                s->step_index = idx;
                if (s->on_change)
                    s->on_change(node, idx, s->user_data);
                aroma_node_invalidate(node);
                if (rd)
                    rd(NULL);
            }
            return true;
        }
        case EVENT_TYPE_MOUSE_CLICK: {
            if (s->active_pointer_id != -1 || !in)
                break;
            int idx = st_steps_pick(s, x, y);
            if (idx >= 0 && idx != s->step_index) {
                s->step_index = idx;
                if (s->on_change)
                    s->on_change(node, idx, s->user_data);
                aroma_node_invalidate(node);
                if (rd)
                    rd(NULL);
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
        if (!s->mouse_down)
            return in;
        {





            int rh = st_row_h(s);
            int raw_dy = y - s->prev_y;
            int dy = raw_dy;
            int cap = 2 * rh;
            if (dy > cap)
                dy = cap;
            else if (dy < -cap)
                dy = -cap;
            s->prev_y += dy;
            aroma_velocity_add(&s->vt, x, s->prev_y, aroma_time_now_ms());
            if (!s->dragging) {
                int tdx = x - s->down_x;
                int tdy = y - s->down_y;
                if (tdx < 0)
                    tdx = -tdx;
                if (tdy < 0)
                    tdy = -tdy;
                if (tdx < slop && tdy < slop)
                    return true;
                s->dragging = true;
                s->repeat_dir = 0;
                if (s->repeat_timer) {
                    aroma_timer_cancel(s->repeat_timer);
                    s->repeat_timer = NULL;
                }
            }
            s->offset_px += (float)dy;
            if (!s->wrap) {
                int lo = (s->min_val - s->value) * rh / (s->step > 0 ? s->step : 1);
                int hi = (s->max_val - s->value) * rh / (s->step > 0 ? s->step : 1);
                float min_off = (float)-hi - (float)rh * 0.35f;
                float max_off = (float)-lo + (float)rh * 0.35f;
                if (s->offset_px < min_off)
                    s->offset_px = min_off + (s->offset_px - min_off) * 0.35f;
                if (s->offset_px > max_off)
                    s->offset_px = max_off + (s->offset_px - max_off) * 0.35f;
            }
            aroma_node_invalidate(node);
            return true;
        }
    case EVENT_TYPE_MOUSE_EXIT:
        return false;
    case EVENT_TYPE_TOUCH_DOWN: {
        if (!in || s->active_pointer_id != -1 || s->mouse_down)
            return false;


        st_commit_pending(node, s);
        s->offset_px = 0.0f;
        s->active_pointer_id = e->data.touch.id;
        s->down_x = x;
        s->down_y = y;
        s->prev_y = y;
        s->dragging = false;
        aroma_velocity_reset(&s->vt);
        aroma_velocity_add(&s->vt, x, y, aroma_time_now_ms());
        s->repeat_dir = st_numeric_zone(s, y);
        s->repeat_ticks = 0;
        if (s->repeat_dir != 0) {
            s->repeat_timer = aroma_timer_create(ST_REPEAT_MS, true,
                                                 st_repeat_tick,
                                                 s->self_node);
        }
        return true;
    }
    case EVENT_TYPE_TOUCH_MOVE: {
        if (s->active_pointer_id == -1 ||
            s->active_pointer_id != e->data.touch.id)
            return s->active_pointer_id != -1;
        int dy = y - s->prev_y;
        s->prev_y = y;
        aroma_velocity_add(&s->vt, x, y, aroma_time_now_ms());
        if (!s->dragging) {
            int tdx = x - s->down_x;
            int tdy = y - s->down_y;
            if (tdx < 0)
                tdx = -tdx;
            if (tdy < 0)
                tdy = -tdy;
            if (tdx < slop && tdy < slop)
                return true;
            s->dragging = true;
            s->repeat_dir = 0;
            if (s->repeat_timer) {
                aroma_timer_cancel(s->repeat_timer);
                s->repeat_timer = NULL;
            }
        }
        int rh = st_row_h(s);
        s->offset_px += (float)dy;
        if (!s->wrap) {
            int lo = (s->min_val - s->value) * rh / (s->step > 0 ? s->step : 1);
            int hi = (s->max_val - s->value) * rh / (s->step > 0 ? s->step : 1);
            float min_off = (float)-hi - (float)rh * 0.35f;
            float max_off = (float)-lo + (float)rh * 0.35f;
            if (s->offset_px < min_off)
                s->offset_px = min_off + (s->offset_px - min_off) * 0.35f;
            if (s->offset_px > max_off)
                s->offset_px = max_off + (s->offset_px - max_off) * 0.35f;
        }
        aroma_node_invalidate(node);
        return true;
    }
    case EVENT_TYPE_TOUCH_UP: {
        if (s->active_pointer_id != e->data.touch.id)
            return false;
        s->active_pointer_id = -1;
        if (s->repeat_timer) {
            aroma_timer_cancel(s->repeat_timer);
            s->repeat_timer = NULL;
        }
        if (!s->dragging) {
            int dir = st_numeric_zone(s, s->down_y);
            int uy = y;
            if (uy >= s->rect.y && uy < s->rect.y + s->rect.height)
                dir = st_numeric_zone(s, uy);
            s->repeat_dir = 0;


            if (s->repeat_ticks >= ST_REPEAT_DELAY_TICKS)
                return true;
            if (dir != 0 && in) {
                int next = s->value + dir * s->step;
                if (s->wrap) {
                    int span = st_span(s);
                    while (next < s->min_val)
                        next += span;
                    while (next > s->max_val)
                        next -= span;
                } else if (next < s->min_val || next > s->max_val) {
                    return true;
                }
                s->value = next;
                st_fire(node, s, next);
                aroma_node_invalidate(node);
                if (rd)
                    rd(NULL);
            }
            return true;
        }
        s->dragging = false;
        s->repeat_dir = 0;
        float v = 0.0f;
        {
            float vx = 0.0f, vy = 0.0f;
            if (aroma_velocity_get(&s->vt, aroma_time_now_ms(), &vx, &vy))
                v = vy;
        }
        st_finish_drag(node, s, v);
        if (rd)
            rd(NULL);
        return true;
    }
    case EVENT_TYPE_MOUSE_CLICK: {






        if (s->active_pointer_id != -1 || !in)
            break;


        st_commit_pending(node, s);
        s->offset_px = 0.0f;
        s->mouse_down = true;
        s->down_x = x;
        s->down_y = y;
        s->prev_y = y;
        s->dragging = false;
        aroma_velocity_reset(&s->vt);
        aroma_velocity_add(&s->vt, x, y, aroma_time_now_ms());
        s->repeat_dir = st_numeric_zone(s, y);
        s->repeat_ticks = 0;
        if (s->repeat_dir != 0) {
            s->repeat_timer = aroma_timer_create(ST_REPEAT_MS, true,
                                                 st_repeat_tick,
                                                 s->self_node);
        }
        return true;
    }
    case EVENT_TYPE_MOUSE_RELEASE: {
        if (!s->mouse_down)
            return false;
        s->mouse_down = false;
        if (s->repeat_timer) {
            aroma_timer_cancel(s->repeat_timer);
            s->repeat_timer = NULL;
        }
        if (!s->dragging) {
            int dir = st_numeric_zone(s, s->down_y);
            int uy = y;
            if (uy >= s->rect.y && uy < s->rect.y + s->rect.height)
                dir = st_numeric_zone(s, uy);
            s->repeat_dir = 0;


            if (s->repeat_ticks >= ST_REPEAT_DELAY_TICKS)
                return true;
            if (dir != 0 && in) {
                if (st_tap_step(node, s, dir) && rd)
                    rd(NULL);
            }
            return true;
        }
        s->dragging = false;
        s->repeat_dir = 0;
        float v = 0.0f;
        {
            float vx = 0.0f, vy = 0.0f;
            if (aroma_velocity_get(&s->vt, aroma_time_now_ms(), &vx, &vy))
                v = vy;
        }
        st_finish_drag(node, s, v);
        if (rd)
            rd(NULL);
        return true;
    }
    case EVENT_TYPE_MOUSE_SCROLL: {




        if (!in)
            return false;
        float sy = e->data.mouse.scroll_y;
        if (sy == 0.0f)
            return false;
        int dir = sy > 0.0f ? 1 : -1;
        bool stepped = st_tap_step(node, s, dir);
        if (stepped && rd)
            rd(NULL);
        return stepped;
    }
    default:
        break;
    }
    return false;
}

static AromaNode *st_create(AromaNode *parent, int x, int y, int w, int h)
{
    if (!parent)
        return NULL;
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    w = aroma_android_dp_to_px(w);
    h = aroma_android_dp_to_px(h);
#endif
    AromaStepper *s = (AromaStepper *)aroma_widget_alloc(sizeof(*s));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->rect.x = x;
    s->rect.y = y;
    s->rect.width = w > 0 ? w : 200;
    s->rect.height = h > 0 ? h : 44;
    s->use_theme_colors = true;
    s->wrap = true;
    s->active_pointer_id = -1;
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, s);
    if (!node) {
        aroma_widget_free(s);
        return NULL;
    }
    s->self_node = node;
    aroma_node_set_draw_cb(node, aroma_stepper_draw);
    aroma_node_invalidate(node);
    return node;
}

AromaNode *aroma_stepper_create_numeric(AromaNode *parent, int x, int y,
                                        int width, int height, int min_val,
                                        int max_val, int value, int step)
{
    if (max_val < min_val) {
        int t = min_val;
        min_val = max_val;
        max_val = t;
    }
    AromaNode *node = st_create(parent, x, y, width, height);
    if (!node)
        return NULL;
    AromaStepper *s = (AromaStepper *)node->node_widget_ptr;
    s->mode = AROMA_STEPPER_NUMERIC;
    s->min_val = min_val;
    s->max_val = max_val;
    s->step = step > 0 ? step : 1;
    s->value = value;
    if (s->value < min_val)
        s->value = min_val;
    if (s->value > max_val)
        s->value = max_val;
    return node;
}

AromaNode *aroma_stepper_create_steps(AromaNode *parent, int x, int y,
                                      int width, int height,
                                      const char **labels, int count)
{
    AromaNode *node = st_create(parent, x, y, width, height);
    if (!node)
        return NULL;
    AromaStepper *s = (AromaStepper *)node->node_widget_ptr;
    s->mode = AROMA_STEPPER_STEPS;
    s->count = 0;
    if (labels && count > 0) {
        if (count > AROMA_STEPPER_STEPS_MAX)
            count = AROMA_STEPPER_STEPS_MAX;
        for (int i = 0; i < count; i++) {
            if (labels[i]) {
                strncpy(s->labels[i], labels[i], AROMA_STEPPER_LABEL_MAX - 1);
                s->labels[i][AROMA_STEPPER_LABEL_MAX - 1] = '\0';
            }
        }
        s->count = count;
    }
    s->step_index = 0;
    return node;
}

void aroma_stepper_set_value(AromaNode *n, int value)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaStepper *s = (AromaStepper *)n->node_widget_ptr;
    if (s->mode != AROMA_STEPPER_NUMERIC)
        return;
    if (value < s->min_val)
        value = s->min_val;
    if (value > s->max_val)
        value = s->max_val;


    st_kill_timers(s);
    if (s->value != value) {
        s->value = value;
        s->offset_px = 0.0f;
        aroma_node_invalidate(n);
    } else if (s->offset_px != 0.0f) {
        s->offset_px = 0.0f;
        aroma_node_invalidate(n);
    }
}

int aroma_stepper_get_value(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return 0;
    AromaStepper *s = (AromaStepper *)n->node_widget_ptr;
    if (s->mode != AROMA_STEPPER_NUMERIC)
        return 0;
    return s->value;
}

void aroma_stepper_set_wrap(AromaNode *n, bool wrap)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaStepper *)n->node_widget_ptr)->wrap = wrap;
    aroma_node_invalidate(n);
}

void aroma_stepper_set_step_index(AromaNode *n, int index)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaStepper *s = (AromaStepper *)n->node_widget_ptr;
    if (s->mode != AROMA_STEPPER_STEPS)
        return;
    if (index < 0 || index >= s->count)
        return;
    if (s->step_index != index) {
        s->step_index = index;
        aroma_node_invalidate(n);
    }
}

int aroma_stepper_get_step_index(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return -1;
    AromaStepper *s = (AromaStepper *)n->node_widget_ptr;
    if (s->mode != AROMA_STEPPER_STEPS)
        return -1;
    return s->step_index;
}

void aroma_stepper_set_on_change(AromaNode *n, AromaStepperChangeCb cb,
                                 void *ud)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaStepper *s = (AromaStepper *)n->node_widget_ptr;
    s->on_change = cb;
    s->user_data = ud;
}

void aroma_stepper_set_font(AromaNode *n, AromaFont *font)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaStepper *)n->node_widget_ptr)->font = font;
    aroma_node_invalidate(n);
}

bool aroma_stepper_owns_touch(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return false;
    return ((AromaStepper *)n->node_widget_ptr)->mode == AROMA_STEPPER_NUMERIC;
}

bool aroma_stepper_setup_events(AromaNode *n, void (*on_redraw)(void *),
                                void *ud)
{
    (void)ud;
    if (!n)
        return false;
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_MOVE, st_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_EXIT, st_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_DOWN, st_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_UP, st_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_MOVE, st_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_CLICK, st_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_RELEASE, st_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_SCROLL, st_handle,
                          (void *)on_redraw, 80);
    return true;
}

static void st_draw_check(AromaGraphicsInterface *gfx, size_t wid, int cx,
                          int cy, int r, uint32_t color)
{
    if (!gfx || !gfx->draw_line)
        return;
    float w = tdp_f(2.0f);
    if (w <= 0.0f)
        w = 1.0f;
    int x0 = cx - r / 2;
    int y0 = cy;
    int x1 = cx - r / 8;
    int y1 = cy + r / 3;
    int x2 = cx + r / 2;
    int y2 = cy - r / 3;
    gfx->draw_line(wid, x0, y0, x1, y1, color, w, true);
    gfx->draw_line(wid, x1, y1, x2, y2, color, w, true);
}

void aroma_stepper_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node))
        return;
    AromaStepper *s = (AromaStepper *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text)
        return;
    if (!s->font) {
        for (int i = 0; i < g_window_count; i++) {
            if (g_windows[i].is_active &&
                g_windows[i].window_id == window_id &&
                g_windows[i].default_font) {
                s->font = g_windows[i].default_font;
                break;
            }
        }
    }
    if (s->use_theme_colors) {
        AromaTheme th = aroma_theme_get_global();
        s->bg_color = th.colors.surface;
        s->border_color = th.colors.border;
        s->text_color = th.colors.text_primary;
        s->dim_color = th.colors.text_secondary;
        s->accent_color = th.colors.primary;
        s->accent_text = th.colors.surface;
    }
    gfx->fill_rectangle(window_id, s->rect.x, s->rect.y, s->rect.width,
                        s->rect.height, s->bg_color, true, tdp_f(10.0f));
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, s->rect.x, s->rect.y,
                                    s->rect.width, s->rect.height,
                                    s->border_color,
                                    tdp(1) > 0 ? tdp(1) : 1, true,
                                    tdp_f(10.0f));
    if (!s->font)
        return;
    if (s->mode == AROMA_STEPPER_NUMERIC) {
        int rh = st_row_h(s);
        int mid = s->rect.y + s->rect.height / 2;
        int div_top = mid - rh / 2;
        int div_bot = mid + rh / 2;
        int div_pad = tdp(12);
        float div_w = tdp_f(2.0f);
        if (div_w <= 0.0f)
            div_w = 1.0f;
        if (gfx->draw_line) {
            gfx->draw_line(window_id, s->rect.x + div_pad, div_top,
                           s->rect.x + s->rect.width - div_pad, div_top,
                           s->accent_color, div_w, false);
            gfx->draw_line(window_id, s->rect.x + div_pad, div_bot,
                           s->rect.x + s->rect.width - div_pad, div_bot,
                           s->accent_color, div_w, false);
        }


        if (gfx->graphics_set_clip)
            gfx->graphics_set_clip(s->rect.x, s->rect.y, s->rect.width,
                                   s->rect.height);
        float frac = s->offset_px / (float)rh;
        int sel_j = -(int)(frac > 0.0f ? frac + 0.5f : frac - 0.5f);



        for (int j = sel_j - 2; j <= sel_j + 2; j++) {
            int slot = st_slot(s, j);
            if (slot < 0)
                continue;
            int ry = mid + (int)(((float)j + frac) * (float)rh);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", slot);
            int bw = aroma_font_get_line_width(s->font, buf);
            int bh = aroma_font_get_line_height(s->font);
            bool is_sel = (j == sel_j);
            uint32_t colr;
            float scale;
            if (is_sel) {
                colr = s->text_color;
                scale = 1.2f;
            } else {
                colr = aroma_color_blend(s->dim_color, s->bg_color, 0.4f);
                scale = 1.0f;
            }
            gfx->render_text(window_id, s->font, buf,
                             s->rect.x + (s->rect.width - bw) / 2,
                             ry - bh / 2, colr, scale);
        }
        if (gfx->graphics_clear_clip)
            gfx->graphics_clear_clip();
    } else if (s->count > 0) {
        int min_w = tdp(ST_MIN_TOUCH_DP);
        int seg = s->rect.width / s->count;
        if (seg < min_w)
            seg = min_w;
        int total = seg * s->count;
        int sx = s->rect.x + (s->rect.width - total) / 2;
        int label_h = tdp(20);
        int cy = s->rect.y + (s->rect.height - label_h) / 2;
        int dot_r = tdp(12);
        if (dot_r < 6)
            dot_r = 6;
        float conn_w = tdp_f(2.0f);
        if (conn_w <= 0.0f)
            conn_w = 1.0f;
        if (gfx->draw_line && s->count > 1)
            gfx->draw_line(window_id, sx + seg / 2, cy,
                           sx + total - seg / 2, cy, s->border_color, conn_w,
                           true);
        for (int i = 0; i < s->count; i++) {
            int cx = sx + i * seg + seg / 2;
            bool done = i < s->step_index;
            bool cur = i == s->step_index;
            if (cur || done) {
                gfx->fill_rectangle(window_id, cx - dot_r, cy - dot_r,
                                    dot_r * 2, dot_r * 2, s->accent_color,
                                    true, (float)dot_r);
            } else {
                gfx->fill_rectangle(window_id, cx - dot_r, cy - dot_r,
                                    dot_r * 2, dot_r * 2, s->bg_color, true,
                                    (float)dot_r);
                if (gfx->draw_hollow_rectangle)
                    gfx->draw_hollow_rectangle(window_id, cx - dot_r,
                                               cy - dot_r, dot_r * 2,
                                               dot_r * 2, s->dim_color,
                                               tdp(2) > 0 ? tdp(2) : 1,
                                               true, (float)dot_r);
            }
            if (done) {
                st_draw_check(gfx, window_id, cx, cy, dot_r, s->accent_text);
            } else {
                char num[4];
                snprintf(num, sizeof(num), "%d", i + 1);
                int nw = aroma_font_get_line_width(s->font, num);
                int nh = aroma_font_get_line_height(s->font);
                gfx->render_text(window_id, s->font, num, cx - nw / 2,
                                 cy - nh / 2,
                                 cur ? s->accent_text : s->dim_color, 0.85f);
            }
            if (s->labels[i][0]) {
                int lw = aroma_font_get_line_width(s->font, s->labels[i]);
                int lx = cx - lw / 2;
                if (lx < s->rect.x)
                    lx = s->rect.x;
                if (lx + lw > s->rect.x + s->rect.width)
                    lx = s->rect.x + s->rect.width - lw;
                gfx->render_text(window_id, s->font, s->labels[i], lx,
                                 s->rect.y + s->rect.height - label_h +
                                     (label_h -
                                      aroma_font_get_line_height(s->font)) /
                                         2,
                                 cur ? s->text_color : s->dim_color, 0.75f);
            }
        }
    }
}

void aroma_stepper_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr) {
        AromaStepper *s = (AromaStepper *)node->node_widget_ptr;
        st_kill_timers(s);
        s->self_node = NULL;
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}
