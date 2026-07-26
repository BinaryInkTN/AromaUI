

#include "widgets/aroma_container.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_node.h"
#include "core/aroma_event.h"
#include "core/aroma_timer.h"
#include "core/aroma_time.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <string.h>
#include <math.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif


#define SCROLLBAR_WIDTH 4
#define SCROLLBAR_MIN_THUMB 24
#define SCROLLBAR_PADDING 2
#define SCROLL_SPEED_DEFAULT 1.0f
#define SCROLL_SLOP 8

#define FLING_TICK_MS 16
#define FLING_FRICTION_COEFF 0.015f
#define FLING_INFLEXION 0.35f
#define DECEL_RATE 2.3582f
#define DECEL_RATE_M1 (DECEL_RATE - 1.0f)
#define GRAVITY_EARTH 9.80665f
#define INCH_PER_METER 39.37f
#define OVERSCROLL_RESIST 0.35f
#define OVERSCROLL_MAX_PX 120
#define BOUNCE_DURATION_MS 250

#define VT_MAX_SAMPLES 8
#define VT_HORIZON_MS 150

#define SCROLLBAR_FADE_DELAY_MS 1200
#define SCROLLBAR_FADE_MS 400
#define SCROLLBAR_ACTIVE_ALPHA 0xCC

typedef struct
{
    int x, y;
    uint64_t time_ms;
} VTSample;

typedef struct
{
    VTSample samples[VT_MAX_SAMPLES];
    int head;
    int count;
} VelocityTracker;

static void vt_reset(VelocityTracker *vt)
{
    vt->head = 0;
    vt->count = 0;
}

static void vt_add(VelocityTracker *vt, int x, int y, uint64_t time_ms)
{

    if (vt->count > 0)
    {
        int prev = (vt->head - 1 + VT_MAX_SAMPLES) % VT_MAX_SAMPLES;
        if (vt->samples[prev].time_ms == time_ms)
            return;
    }
    vt->samples[vt->head] = (VTSample){x, y, time_ms};
    vt->head = (vt->head + 1) % VT_MAX_SAMPLES;
    if (vt->count < VT_MAX_SAMPLES)
        vt->count++;
}

static void vt_get_velocity(const VelocityTracker *vt, uint64_t now,
                            float *out_vx, float *out_vy)
{
    *out_vx = 0.0f;
    *out_vy = 0.0f;
    if (vt->count < 2)
        return;

    int first = -1, last = -1;
    for (int k = 0; k < vt->count; k++)
    {
        int idx = (vt->head - 1 - k + VT_MAX_SAMPLES) % VT_MAX_SAMPLES;
        const VTSample *s = &vt->samples[idx];
        if (now - s->time_ms > VT_HORIZON_MS)
            break;

        if (first < 0)
            first = idx;
        last = idx;
    }
    if (first < 0 || last < 0 || first == last)
        return;

    float dt = (float)(vt->samples[first].time_ms - vt->samples[last].time_ms) / 1000.0f;
    if (dt < 0.001f)
        return;

    *out_vx = (float)(vt->samples[first].x - vt->samples[last].x) / dt;
    *out_vy = (float)(vt->samples[first].y - vt->samples[last].y) / dt;
}

typedef struct  AromaContainer
{
    AromaRect rect;

    uint32_t bg_color;
    bool draw_background;

    bool scrollable;
    bool auto_content_size;

    int content_width;
    int content_height;

    float scroll_fx;
    float scroll_fy;

    bool fling_active;
    float fling_start_x;
    float fling_start_y;
    float fling_distance_x;
    float fling_distance_y;
    float fling_duration_x_ms;
    float fling_duration_y_ms;
    uint64_t fling_start_time;

    float overscroll_x;
    float overscroll_y;

    AromaScrollDirection direction;

    bool show_scrollbar;
    uint32_t scrollbar_color;
    float scroll_speed;

    bool is_dragging;
    int drag_start_x;
    int drag_start_y;
    float drag_scroll_start_x;
    float drag_scroll_start_y;
    int active_pointer_id;

    VelocityTracker vt;

    AromaTimer *fling_timer;
    AromaNode *self_node;

    bool bouncing;
    float bounce_start_x;
    float bounce_start_y;
    uint64_t bounce_start_time;

    uint64_t last_scroll_time;
    float scrollbar_opacity;

    int prev_scroll_x;
    int prev_scroll_y;
    bool content_dirty;
} AromaContainer;

static inline int scroll_x_int(AromaContainer *c) { return (int)roundf(c->scroll_fx); }
static inline int scroll_y_int(AromaContainer *c) { return (int)roundf(c->scroll_fy); }

static inline int max_scroll_x(AromaContainer *c)
{
    int m = c->content_width - c->rect.width;
    return m > 0 ? m : 0;
}
static inline int max_scroll_y(AromaContainer *c)
{
    int m = c->content_height - c->rect.height;
    return m > 0 ? m : 0;
}

static void clamp_scroll(AromaContainer *c)
{
    float mx = (float)max_scroll_x(c);
    float my = (float)max_scroll_y(c);
    if (c->scroll_fx < 0.0f)
        c->scroll_fx = 0.0f;
    if (c->scroll_fy < 0.0f)
        c->scroll_fy = 0.0f;
    if (c->scroll_fx > mx)
        c->scroll_fx = mx;
    if (c->scroll_fy > my)
        c->scroll_fy = my;
}

static bool point_in_rect(int px, int py, const AromaRect *r)
{
    return px >= r->x && px < r->x + r->width &&
           py >= r->y && py < r->y + r->height;
}

static inline int effective_scroll_x(AromaContainer *c)
{
    return scroll_x_int(c) + (int)roundf(c->overscroll_x);
}
static inline int effective_scroll_y(AromaContainer *c)
{
    return scroll_y_int(c) + (int)roundf(c->overscroll_y);
}

static float s_physical_coeff = 0.0f;
static float get_physical_coeff(void)
{
    if (s_physical_coeff > 0.0f)
        return s_physical_coeff;

    float density = 1.0f;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->android_get_density)
    {
        float d = platform->android_get_density();
        if (d > 0.0f)
            density = d;
    }
    float ppi = density * 160.0f;
    s_physical_coeff = GRAVITY_EARTH * INCH_PER_METER * ppi * 0.84f;
    return s_physical_coeff;
}

static float fling_get_duration_ms(float velocity_pps)
{
    float v = fabsf(velocity_pps);
    if (v < 1.0f)
        return 0.0f;
    float coeff = get_physical_coeff();
    float l = logf(FLING_INFLEXION * v / (FLING_FRICTION_COEFF * coeff));
    return 1000.0f * expf(l / DECEL_RATE_M1);
}

static float fling_get_distance(float velocity_pps)
{
    float v = fabsf(velocity_pps);
    if (v < 1.0f)
        return 0.0f;
    float coeff = get_physical_coeff();
    float l = logf(FLING_INFLEXION * v / (FLING_FRICTION_COEFF * coeff));
    float sign = velocity_pps < 0.0f ? -1.0f : 1.0f;
    return sign * FLING_FRICTION_COEFF * coeff * expf(DECEL_RATE / DECEL_RATE_M1 * l);
}

static float fling_spline(float t)
{
    if (t >= 1.0f)
        return 1.0f;
    if (t <= 0.0f)
        return 0.0f;
    float inv = 1.0f - t;

    return 1.0f - expf(DECEL_RATE * logf(inv));
}

static void stop_fling(AromaContainer *c)
{
    if (c->fling_timer)
    {
        aroma_timer_cancel(c->fling_timer);
        c->fling_timer = NULL;
    }
    c->fling_active = false;
    c->bouncing = false;
}

static void fling_tick_cb(void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    AromaContainer *c = aroma_container_get(node);
    if (!c || !c->scrollable)
        return;

    bool still_moving = false;
    uint64_t now = aroma_time_now_ms();

    if (c->bouncing)
    {
        float elapsed = (float)(now - c->bounce_start_time);
        float t = elapsed / (float)BOUNCE_DURATION_MS;
        if (t >= 1.0f)
            t = 1.0f;

        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        c->overscroll_x = c->bounce_start_x * (1.0f - ease);
        c->overscroll_y = c->bounce_start_y * (1.0f - ease);

        if (t >= 1.0f)
        {
            c->overscroll_x = 0.0f;
            c->overscroll_y = 0.0f;
            c->bouncing = false;
        }
        else
        {
            still_moving = true;
        }
        c->content_dirty = true;
        aroma_node_invalidate(node);
    }

    if (c->fling_active && !c->bouncing && !c->is_dragging)
    {
        float elapsed_ms = (float)(now - c->fling_start_time);
        bool x_done = true, y_done = true;
        float new_sx = c->scroll_fx;
        float new_sy = c->scroll_fy;

        if (c->fling_duration_x_ms > 0.0f && (c->direction & AROMA_SCROLL_HORIZONTAL))
        {
            float tx = elapsed_ms / c->fling_duration_x_ms;
            if (tx >= 1.0f)
            {
                tx = 1.0f;
            }
            else
            {
                x_done = false;
            }
            new_sx = c->fling_start_x + c->fling_distance_x * fling_spline(tx);
        }
        if (c->fling_duration_y_ms > 0.0f && (c->direction & AROMA_SCROLL_VERTICAL))
        {
            float ty = elapsed_ms / c->fling_duration_y_ms;
            if (ty >= 1.0f)
            {
                ty = 1.0f;
            }
            else
            {
                y_done = false;
            }
            new_sy = c->fling_start_y + c->fling_distance_y * fling_spline(ty);
        }

        float mx = (float)max_scroll_x(c);
        float my = (float)max_scroll_y(c);

        if (new_sx < 0.0f)
        {
            c->overscroll_x = new_sx;
            if (c->overscroll_x < -OVERSCROLL_MAX_PX)
                c->overscroll_x = -OVERSCROLL_MAX_PX;
            new_sx = 0.0f;
            c->fling_active = false;
        }
        else if (new_sx > mx)
        {
            c->overscroll_x = new_sx - mx;
            if (c->overscroll_x > OVERSCROLL_MAX_PX)
                c->overscroll_x = OVERSCROLL_MAX_PX;
            new_sx = mx;
            c->fling_active = false;
        }
        if (new_sy < 0.0f)
        {
            c->overscroll_y = new_sy;
            if (c->overscroll_y < -OVERSCROLL_MAX_PX)
                c->overscroll_y = -OVERSCROLL_MAX_PX;
            new_sy = 0.0f;
            c->fling_active = false;
        }
        else if (new_sy > my)
        {
            c->overscroll_y = new_sy - my;
            if (c->overscroll_y > OVERSCROLL_MAX_PX)
                c->overscroll_y = OVERSCROLL_MAX_PX;
            new_sy = my;
            c->fling_active = false;
        }

        if ((c->overscroll_x != 0.0f || c->overscroll_y != 0.0f) && !c->bouncing)
        {
            c->bouncing = true;
            c->bounce_start_x = c->overscroll_x;
            c->bounce_start_y = c->overscroll_y;
            c->bounce_start_time = now;
            still_moving = true;
        }

        c->scroll_fx = new_sx;
        c->scroll_fy = new_sy;
        c->content_dirty = true;
        c->last_scroll_time = now;
        aroma_node_invalidate(node);

        if (x_done && y_done)
        {
            c->fling_active = false;
        }
        if (c->fling_active)
        {
            still_moving = true;
        }
    }

    if (c->show_scrollbar)
    {
        uint64_t since_scroll = now - c->last_scroll_time;
        float target = 0.0f;
        if (c->is_dragging || still_moving || since_scroll < SCROLLBAR_FADE_DELAY_MS)
        {
            target = 1.0f;
        }

        float speed = (target > c->scrollbar_opacity) ? 0.3f : 0.08f;
        c->scrollbar_opacity += (target - c->scrollbar_opacity) * speed;
        if (c->scrollbar_opacity < 0.01f)
            c->scrollbar_opacity = 0.0f;
        if (c->scrollbar_opacity > 0.99f)
            c->scrollbar_opacity = 1.0f;

        if (c->scrollbar_opacity > 0.0f)
        {
            still_moving = true;
            c->content_dirty = true;
            aroma_node_invalidate(node);
        }
    }

    if (!still_moving)
    {
        stop_fling(c);
    }
}

static void ensure_animation_timer(AromaContainer *c)
{
    if (!c->fling_timer && c->self_node)
    {
        c->fling_timer = aroma_timer_create(
            FLING_TICK_MS, true, fling_tick_cb, c->self_node);
    }
}

static bool scroll_event_handler(AromaEvent *event, void *user_data)
{
    LOG_INFO("SCR_HANDLER: entered type=%d udata=%p", event ? event->event_type : -1, user_data);

    AromaNode *node = (AromaNode *)user_data;
    if (!node)
        return false;

    AromaContainer *c = aroma_container_get(node);
    LOG_INFO("SCR_HANDLER: c=%p scrollable=%d node_type=%d",
             (void *)c, c ? c->scrollable : -1, node->node_type);
    if (!c || !c->scrollable)
        return false;

    bool can_scroll_h = (c->direction & AROMA_SCROLL_HORIZONTAL) && c->content_width > c->rect.width;
    bool can_scroll_v = (c->direction & AROMA_SCROLL_VERTICAL) && c->content_height > c->rect.height;

    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_CLICK:
    case EVENT_TYPE_TOUCH_DOWN:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_CLICK) {
            if (event->data.mouse.clicks == 0) return false;
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }
        if (!point_in_rect(tx, ty, &c->rect))
            return false;

        LOG_INFO("SCROLL_DOWN: ptr=%d content_h=%d rect_h=%d can_v=%d",
                 id, c->content_height, c->rect.height, can_scroll_v);

        stop_fling(c);
        c->overscroll_x = 0.0f;
        c->overscroll_y = 0.0f;

        c->drag_start_x = tx;
        c->drag_start_y = ty;
        c->drag_scroll_start_x = c->scroll_fx;
        c->drag_scroll_start_y = c->scroll_fy;
        c->active_pointer_id = event->data.touch.id;
        c->is_dragging = false;
        c->scrollbar_opacity = 1.0f;
        c->last_scroll_time = aroma_time_now_ms();

        vt_reset(&c->vt);
        vt_add(&c->vt, tx, ty, aroma_time_now_ms());
        return false;
    }

    case EVENT_TYPE_MOUSE_SCROLL:
    {
        int tx = event->data.mouse.x;
        int ty = event->data.mouse.y;
        if (!point_in_rect(tx, ty, &c->rect)) return false;
        
        float sx = event->data.mouse.scroll_x;
        float sy = event->data.mouse.scroll_y;
        
        if (sx == 0.0f && sy == 0.0f) return false;
        
        float old_sx = c->scroll_fx;
        float old_sy = c->scroll_fy;
        
        if (can_scroll_v) c->scroll_fy -= sy * 50.0f * c->scroll_speed;
        if (can_scroll_h) c->scroll_fx -= sx * 50.0f * c->scroll_speed;
        
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

    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_TOUCH_MOVE:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_MOVE) {
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }
        

        if (id != c->active_pointer_id)
            return false;

        if (!can_scroll_h && !can_scroll_v)
        {
            LOG_INFO("SCROLL_MOVE: bail can_v=%d content_h=%d rect_h=%d",
                     can_scroll_v, c->content_height, c->rect.height);
            return false;
        }


        int dx = c->drag_start_x - tx;
        int dy = c->drag_start_y - ty;

        vt_add(&c->vt, tx, ty, aroma_time_now_ms());

        if (!c->is_dragging)
        {
            int abs_dx = dx < 0 ? -dx : dx;
            int abs_dy = dy < 0 ? -dy : dy;
            bool slop_h = can_scroll_h && abs_dx >= SCROLL_SLOP;
            bool slop_v = can_scroll_v && abs_dy >= SCROLL_SLOP;
            if (!slop_h && !slop_v)
                return false;
            c->is_dragging = true;

            c->drag_start_x = tx;
            c->drag_start_y = ty;
            c->drag_scroll_start_x = c->scroll_fx;
            c->drag_scroll_start_y = c->scroll_fy;
            dx = 0;
            dy = 0;
        }

        float new_sx = c->drag_scroll_start_x;
        float new_sy = c->drag_scroll_start_y;

        if (can_scroll_h)
            new_sx += (float)dx * c->scroll_speed;
        if (can_scroll_v)
            new_sy += (float)dy * c->scroll_speed;

        float mx = (float)max_scroll_x(c);
        float my = (float)max_scroll_y(c);

        c->overscroll_x = 0.0f;
        c->overscroll_y = 0.0f;

        if (new_sx < 0.0f)
        {
            c->overscroll_x = new_sx * OVERSCROLL_RESIST;
            if (c->overscroll_x < -OVERSCROLL_MAX_PX)
                c->overscroll_x = -OVERSCROLL_MAX_PX;
            new_sx = 0.0f;
        }
        else if (new_sx > mx)
        {
            c->overscroll_x = (new_sx - mx) * OVERSCROLL_RESIST;
            if (c->overscroll_x > OVERSCROLL_MAX_PX)
                c->overscroll_x = OVERSCROLL_MAX_PX;
            new_sx = mx;
        }
        if (new_sy < 0.0f)
        {
            c->overscroll_y = new_sy * OVERSCROLL_RESIST;
            if (c->overscroll_y < -OVERSCROLL_MAX_PX)
                c->overscroll_y = -OVERSCROLL_MAX_PX;
            new_sy = 0.0f;
        }
        else if (new_sy > my)
        {
            c->overscroll_y = (new_sy - my) * OVERSCROLL_RESIST;
            if (c->overscroll_y > OVERSCROLL_MAX_PX)
                c->overscroll_y = OVERSCROLL_MAX_PX;
            new_sy = my;
        }

        c->scroll_fx = new_sx;
        c->scroll_fy = new_sy;
        c->content_dirty = true;
        c->last_scroll_time = aroma_time_now_ms();
        LOG_INFO("SCROLL_DRAG: fy=%.1f max=%.0f over=%.1f dy=%d",
                 c->scroll_fy, my, c->overscroll_y, dy);
        aroma_node_invalidate(node);
        return true;
    }

    case EVENT_TYPE_MOUSE_RELEASE:
    case EVENT_TYPE_TOUCH_UP:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_RELEASE) {
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }
        if (event->target_node_id != node->node_id)
            return false;
        if (id != c->active_pointer_id)
            return false;
        bool was_dragging = c->is_dragging;
        c->is_dragging = false;
        c->active_pointer_id = -1;

        vt_add(&c->vt, event->data.touch.x, event->data.touch.y,
               aroma_time_now_ms());

        if (was_dragging)
        {

            if (c->overscroll_x != 0.0f || c->overscroll_y != 0.0f)
            {
                c->bouncing = true;
                c->bounce_start_x = c->overscroll_x;
                c->bounce_start_y = c->overscroll_y;
                c->bounce_start_time = aroma_time_now_ms();
                c->fling_active = false;
                c->last_scroll_time = aroma_time_now_ms();
                ensure_animation_timer(c);
                return true;
            }

            float vx = 0.0f, vy = 0.0f;
            vt_get_velocity(&c->vt, aroma_time_now_ms(), &vx, &vy);

            vx = -vx;
            vy = -vy;

            float min_vel = 150.0f;
            bool do_fling = false;

            float dur_x = 0.0f, dur_y = 0.0f;
            float dist_x = 0.0f, dist_y = 0.0f;

            if (can_scroll_h && fabsf(vx) > min_vel)
            {
                dur_x = fling_get_duration_ms(vx);
                dist_x = fling_get_distance(vx);
                do_fling = true;
            }
            if (can_scroll_v && fabsf(vy) > min_vel)
            {
                dur_y = fling_get_duration_ms(vy);
                dist_y = fling_get_distance(vy);
                do_fling = true;
            }

            if (do_fling)
            {
                c->fling_active = true;
                c->fling_start_x = c->scroll_fx;
                c->fling_start_y = c->scroll_fy;
                c->fling_distance_x = dist_x;
                c->fling_distance_y = dist_y;
                c->fling_duration_x_ms = dur_x;
                c->fling_duration_y_ms = dur_y;
                c->fling_start_time = aroma_time_now_ms();
            }
            c->last_scroll_time = aroma_time_now_ms();
            ensure_animation_timer(c);
        }
        return was_dragging;
    }



    default:
        break;
    }
    return false;
}

static void draw_scrollbar_indicators(AromaContainer *c, size_t window_id)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle)
        return;
    if (c->scrollbar_opacity <= 0.01f)
        return;

    uint32_t base = c->scrollbar_color;
    uint8_t base_alpha = base & 0xFF;
    uint8_t alpha = (uint8_t)(base_alpha * c->scrollbar_opacity);
    uint32_t color = (base & 0xFFFFFF00u) | alpha;

    int eff_sx = effective_scroll_x(c);
    int eff_sy = effective_scroll_y(c);

    if ((c->direction & AROMA_SCROLL_VERTICAL) && c->content_height > c->rect.height)
    {
        float visible_ratio = (float)c->rect.height / (float)c->content_height;
        int thumb_height = (int)(c->rect.height * visible_ratio);
        if (thumb_height < SCROLLBAR_MIN_THUMB)
            thumb_height = SCROLLBAR_MIN_THUMB;

        int max_scroll = c->content_height - c->rect.height;
        float scroll_ratio = max_scroll > 0
                                 ? (float)eff_sy / (float)max_scroll
                                 : 0.0f;
        if (scroll_ratio < 0.0f)
            scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f)
            scroll_ratio = 1.0f;

        int track_height = c->rect.height - thumb_height;
        int thumb_y = c->rect.y + (int)(track_height * scroll_ratio);
        int bar_x = c->rect.x + c->rect.width - SCROLLBAR_WIDTH - SCROLLBAR_PADDING;

        gfx->fill_rectangle(window_id, bar_x, thumb_y,
                            SCROLLBAR_WIDTH, thumb_height,
                            color, true, (float)(SCROLLBAR_WIDTH / 2));
    }

    if ((c->direction & AROMA_SCROLL_HORIZONTAL) && c->content_width > c->rect.width)
    {
        float visible_ratio = (float)c->rect.width / (float)c->content_width;
        int thumb_width = (int)(c->rect.width * visible_ratio);
        if (thumb_width < SCROLLBAR_MIN_THUMB)
            thumb_width = SCROLLBAR_MIN_THUMB;

        int max_scroll = c->content_width - c->rect.width;
        float scroll_ratio = max_scroll > 0
                                 ? (float)eff_sx / (float)max_scroll
                                 : 0.0f;
        if (scroll_ratio < 0.0f)
            scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f)
            scroll_ratio = 1.0f;

        int track_width = c->rect.width - thumb_width;
        int thumb_x = c->rect.x + (int)(track_width * scroll_ratio);
        int bar_y = c->rect.y + c->rect.height - SCROLLBAR_WIDTH - SCROLLBAR_PADDING;

        gfx->fill_rectangle(window_id, thumb_x, bar_y,
                            thumb_width, SCROLLBAR_WIDTH,
                            color, true, (float)(SCROLLBAR_WIDTH / 2));
    }
}

AromaNode *aroma_container_create(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
    {
        LOG_ERROR("Invalid container parameters");
        return NULL;
    }

    AromaContainer *container = (AromaContainer *)aroma_widget_alloc(sizeof(AromaContainer));
    if (!container)
    {
        LOG_ERROR("Failed to allocate memory for container");
        return NULL;
    }

#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif

    memset(container, 0, sizeof(AromaContainer));
    container->rect.x = x;
    container->rect.y = y;
    container->rect.width = width;
    container->rect.height = height;
    container->draw_background = false;

    container->scrollable = false;
    container->auto_content_size = true;
    container->content_width = width;
    container->content_height = height;
    container->scroll_fx = 0.0f;
    container->scroll_fy = 0.0f;
    container->fling_active = false;
    container->direction = AROMA_SCROLL_VERTICAL;
    container->scroll_speed = SCROLL_SPEED_DEFAULT;
    container->show_scrollbar = true;
    container->scrollbar_color = 0x88888880;
    container->scrollbar_opacity = 0.0f;
    container->active_pointer_id = -1;
    container->content_dirty = true;
    container->self_node = NULL;
    vt_reset(&container->vt);

    AromaNode *node = __add_child_node(NODE_TYPE_CONTAINER, parent, container);
    if (!node)
    {
        aroma_widget_free(container);
        LOG_ERROR("Failed to create container node");
        return NULL;
    }

    container->self_node = node;
    aroma_node_set_draw_cb(node, aroma_container_draw);
    aroma_node_invalidate(node);

    return node;
}

void aroma_container_destroy(AromaNode *container_node)
{
    if (!container_node)
        return;
    if (container_node->node_widget_ptr)
    {
        aroma_widget_free(container_node->node_widget_ptr);
        container_node->node_widget_ptr = NULL;
    }
    __destroy_node(container_node);
}

void aroma_container_set_rect(AromaNode *container_node, int x, int y, int width, int height)
{
    AromaContainer *container = aroma_container_get(container_node);
    if (!container)
        return;

    if (container->rect.x == x && container->rect.y == y &&
        container->rect.width == width && container->rect.height == height)
    {
        return;
    }

    container->rect.x = x;
    container->rect.y = y;
    container->rect.width = width;
    container->rect.height = height;

    if (container->scrollable)
        clamp_scroll(container);

    for (uint64_t i = 0; i < container_node->child_count; i++)
    {
        AromaNode *child = container_node->child_nodes[i];
        if (child && !child->is_hidden)
        {
            aroma_node_update_layout(child, x, y, width, height);
        }
    }

    aroma_node_invalidate(container_node);
}

AromaRect aroma_container_get_rect(AromaNode *container_node)
{
    AromaContainer *container = aroma_container_get(container_node);
    if (!container)
    {
        AromaRect empty = {0};
        return empty;
    }
    return container->rect;
}

void aroma_container_set_debug_bg(AromaNode *container_node, uint32_t color)
{
    AromaContainer *container = aroma_container_get(container_node);
    if (!container)
        return;

    container->bg_color = color;
    container->draw_background = true;
    aroma_node_invalidate(container_node);
}

void aroma_container_set_scrollable(AromaNode *node, bool scrollable)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;
    if (c->scrollable == scrollable)
        return;

    c->scrollable = scrollable;

    if (scrollable)
    {

        aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_MOVE, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_SCROLL, scroll_event_handler, node, 0);
        aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_SCROLL, scroll_event_handler, node, 0);
    }

    aroma_node_invalidate(node);
}

void aroma_container_set_content_size(AromaNode *node, int content_width, int content_height)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;

    int cw = content_width > 0 ? content_width : c->rect.width;
    int ch = content_height > 0 ? content_height : c->rect.height;

    if (c->content_width == cw && c->content_height == ch)
        return;

    c->content_width = cw;
    c->content_height = ch;
    c->auto_content_size = false;
    clamp_scroll(c);
    c->content_dirty = true;
    aroma_node_invalidate(node);
}

void aroma_container_set_scroll_direction(AromaNode *node, AromaScrollDirection direction)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;
    c->direction = direction;
}

void aroma_container_get_scroll(AromaNode *node, int *scroll_x, int *scroll_y)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
    {
        if (scroll_x)
            *scroll_x = 0;
        if (scroll_y)
            *scroll_y = 0;
        return;
    }
    if (scroll_x)
        *scroll_x = scroll_x_int(c);
    if (scroll_y)
        *scroll_y = scroll_y_int(c);
}

void aroma_container_set_scroll(AromaNode *node, int scroll_x, int scroll_y)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;

    float old_sx = c->scroll_fx;
    float old_sy = c->scroll_fy;
    c->scroll_fx = (float)scroll_x;
    c->scroll_fy = (float)scroll_y;
    clamp_scroll(c);

    if (c->scroll_fx != old_sx || c->scroll_fy != old_sy)
    {
        c->content_dirty = true;
        c->last_scroll_time = aroma_time_now_ms();
        c->scrollbar_opacity = 1.0f;
        ensure_animation_timer(c);
        aroma_node_invalidate(node);
    }
}

void aroma_container_scroll_by(AromaNode *node, int dx, int dy)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;
    aroma_container_set_scroll(node, scroll_x_int(c) + dx, scroll_y_int(c) + dy);
}

void aroma_container_set_scroll_speed(AromaNode *node, float speed)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;
    c->scroll_speed = speed > 0.0f ? speed : SCROLL_SPEED_DEFAULT;
}

void aroma_container_show_scrollbar(AromaNode *node, bool show)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;
    c->show_scrollbar = show;
    aroma_node_invalidate(node);
}

void aroma_container_set_scrollbar_color(AromaNode *node, uint32_t color)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
        return;
    c->scrollbar_color = color;
    aroma_node_invalidate(node);
}

bool aroma_container_is_scrollable(AromaNode *node)
{
    AromaContainer *c = aroma_container_get(node);
    return c ? c->scrollable : false;
}

void aroma_container_get_content_size(AromaNode *node, int *out_w, int *out_h)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c)
    {
        if (out_w)
            *out_w = 0;
        if (out_h)
            *out_h = 0;
        return;
    }
    if (out_w)
        *out_w = c->content_width;
    if (out_h)
        *out_h = c->content_height;
}

void aroma_container_update_auto_content_size(AromaNode *node)
{
    AromaContainer *c = aroma_container_get(node);
    if (!c || !c->scrollable || !c->auto_content_size)
        return;

    int max_w = 0;
    int max_h = 0;

    for (uint64_t i = 0; i < node->child_count; i++)
    {
        AromaNode *child = node->child_nodes[i];
        if (!child || child->is_hidden || !child->node_widget_ptr)
            continue;

        AromaRect *cr = (AromaRect *)child->node_widget_ptr;
        int right = (cr->x - c->rect.x) + cr->width;
        int bottom = (cr->y - c->rect.y) + cr->height;

        if (right > max_w)
            max_w = right;
        if (bottom > max_h)
            max_h = bottom;
    }

    if (node->layout.gap > 0 && node->child_count > 0)
    {
    }

    if (max_w < c->rect.width)
        max_w = c->rect.width;
    if (max_h < c->rect.height)
        max_h = c->rect.height;

    if (c->content_width != max_w || c->content_height != max_h)
    {
        c->content_width = max_w;
        c->content_height = max_h;
        clamp_scroll(c);
        c->content_dirty = true;
    }
}

static void shift_subtree(AromaNode *node, int dx, int dy)
{
    if (!node)
        return;
    if (node->node_widget_ptr)
    {
        AromaRect *r = (AromaRect *)node->node_widget_ptr;
        r->x += dx;
        r->y += dy;
    }
    for (uint64_t i = 0; i < node->child_count; i++)
    {
        if (node->child_nodes[i])
            shift_subtree(node->child_nodes[i], dx, dy);
    }
}

/**
 * @brief Recursively draw a node and all of its non-scrollable descendants.
 *
 * Scrollable children are skipped because their own draw callbacks
 * already handle their subtree (with clip + scroll offsets).
 */
static void draw_subtree_recursive(AromaNode *node, size_t window_id)
{
    if (!node || node->is_hidden)
        return;

    AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(node);
    if (draw_cb)
    {
        draw_cb(node, window_id);
    }
    if (aroma_container_is_scrollable(node))
        return;

    for (uint64_t i = 0; i < node->child_count; i++)
    {
        draw_subtree_recursive(node->child_nodes[i], window_id);
    }
}

void aroma_container_draw(AromaNode *container_node, size_t window_id)
{
    AromaContainer *c = aroma_container_get(container_node);
    if (!c)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();

    if (c->draw_background && gfx && gfx->fill_rectangle)
    {
        gfx->fill_rectangle(window_id,
                            c->rect.x, c->rect.y,
                            c->rect.width, c->rect.height,
                            c->bg_color, false, 0.0f);
    }

    if (c->scrollable)
    {
       
        if (gfx && gfx->graphics_set_clip)
        {
            gfx->graphics_set_clip(c->rect.x, c->rect.y,
                                   c->rect.width, c->rect.height);
        }

        int eff_sx = effective_scroll_x(c);
        int eff_sy = effective_scroll_y(c);

        for (uint64_t i = 0; i < container_node->child_count; i++)
        {
            if (container_node->child_nodes[i])
                shift_subtree(container_node->child_nodes[i],
                              -eff_sx, -eff_sy);
        }

        for (uint64_t i = 0; i < container_node->child_count; i++)
        {
            draw_subtree_recursive(container_node->child_nodes[i], window_id);
        }

        for (uint64_t i = 0; i < container_node->child_count; i++)
        {
            if (container_node->child_nodes[i])
                shift_subtree(container_node->child_nodes[i],
                              eff_sx, eff_sy);
        }
        if (gfx && gfx->graphics_clear_clip)
        {
            gfx->graphics_clear_clip();
        }

        if (c->show_scrollbar)
        {
            draw_scrollbar_indicators(c, window_id);
        }

        c->prev_scroll_x = scroll_x_int(c);
        c->prev_scroll_y = scroll_y_int(c);
        c->content_dirty = false;
    }

}