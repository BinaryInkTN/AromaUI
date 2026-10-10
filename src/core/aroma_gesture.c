#include "aroma_gesture.h"
#include "aroma_dp.h"
#include "aroma_time.h"
#include "aroma_timer.h"
#include "aroma_ui.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AROMA_GESTURE_MAX_OBSERVERS 32
#define AROMA_GESTURE_MAX_TRACKED 2
#define AROMA_GESTURE_TAP_SLOP_DP 8
#define AROMA_GESTURE_TAP_TIMEOUT_MS 350
#define AROMA_GESTURE_DOUBLE_GAP_MS 350
#define AROMA_GESTURE_LONG_PRESS_MS 500
#define AROMA_GESTURE_SWIPE_MIN_DP 64
#define AROMA_GESTURE_VT_SAMPLES 8

typedef struct {
    int id;
    int start_x;
    int start_y;
    int last_x;
    int last_y;
    uint64_t down_ms;
    bool moved;
    int sx[AROMA_GESTURE_VT_SAMPLES];
    int sy[AROMA_GESTURE_VT_SAMPLES];
    uint64_t st[AROMA_GESTURE_VT_SAMPLES];
    int head;
    int count;
} AromaGestureTouch;

struct AromaGestureObserver {
    bool active;
    bool enabled;
    AromaNode *node;
    uint64_t node_id;
    uint32_t mask;
    AromaGestureCallback cb;
    void *user_data;
    AromaGestureTouch fingers[AROMA_GESTURE_MAX_TRACKED];
    int pinch_down_dist;
    bool pinch_on;
    bool pinch_suppress;
    bool long_armed;
    AromaTimer *long_timer;
    uint64_t long_gen;
    bool has_last_tap;
    uint64_t last_tap_ms;
    int last_tap_x;
    int last_tap_y;
};

static AromaGestureObserver s_observers[AROMA_GESTURE_MAX_OBSERVERS];

static int gesture_slop_px(void)
{
    int s = AROMA_DP_I(AROMA_GESTURE_TAP_SLOP_DP);
    return s > 0 ? s : 1;
}

static int gesture_swipe_min_px(void)
{
    int s = AROMA_DP_I(AROMA_GESTURE_SWIPE_MIN_DP);
    return s > 0 ? s : 1;
}

static bool gesture_node_visible(AromaNode *node)
{
    AromaNode *cur = node;
    int depth = 0;
    while (cur && depth < 64) {
        if (cur->is_hidden)
            return false;
        cur = cur->parent_node;
        depth++;
    }
    return true;
}

static bool gesture_inside(AromaNode *node, int x, int y)
{
    if (!node || !gesture_node_visible(node))
        return false;
    AromaRect *r = aroma_node_get_rect(node);
    if (!r || r->width <= 0 || r->height <= 0)
        return false;
    return x >= r->x && x < r->x + r->width &&
           y >= r->y && y < r->y + r->height;
}

static AromaGestureTouch *gesture_find(AromaGestureObserver *o, int id)
{
    for (int i = 0; i < AROMA_GESTURE_MAX_TRACKED; i++) {
        if (o->fingers[i].id == id)
            return &o->fingers[i];
    }
    return NULL;
}

static AromaGestureTouch *gesture_alloc(AromaGestureObserver *o, int id)
{
    AromaGestureTouch *t = gesture_find(o, id);
    if (t)
        return t;
    for (int i = 0; i < AROMA_GESTURE_MAX_TRACKED; i++) {
        if (o->fingers[i].id < 0) {
            t = &o->fingers[i];
            memset(t, 0, sizeof(*t));
            t->id = id;
            return t;
        }
    }
    return NULL;
}

static void gesture_free(AromaGestureObserver *o, int id)
{
    AromaGestureTouch *t = gesture_find(o, id);
    if (t)
        t->id = -1;
}

static void vt_add(AromaGestureTouch *t, int x, int y, uint64_t ms)
{
    if (t->count > 0) {
        int prev = (t->head - 1 + AROMA_GESTURE_VT_SAMPLES) % AROMA_GESTURE_VT_SAMPLES;
        if (t->st[prev] == ms)
            return;
    }
    t->sx[t->head] = x;
    t->sy[t->head] = y;
    t->st[t->head] = ms;
    t->head = (t->head + 1) % AROMA_GESTURE_VT_SAMPLES;
    if (t->count < AROMA_GESTURE_VT_SAMPLES)
        t->count++;
}

static void vt_velocity(AromaGestureTouch *t, uint64_t now, float *vx, float *vy)
{
    *vx = 0.0f;
    *vy = 0.0f;
    if (t->count < 2)
        return;
    int last = (t->head - 1 + AROMA_GESTURE_VT_SAMPLES) % AROMA_GESTURE_VT_SAMPLES;
    int first = (t->head - t->count + AROMA_GESTURE_VT_SAMPLES * 2) % AROMA_GESTURE_VT_SAMPLES;
    while (first != last && now - t->st[first] > 120)
        first = (first + 1) % AROMA_GESTURE_VT_SAMPLES;
    uint64_t dt = now - t->st[first];
    if (dt == 0)
        return;
    *vx = (float)(t->sx[last] - t->sx[first]) * 1000.0f / (float)dt;
    *vy = (float)(t->sy[last] - t->sy[first]) * 1000.0f / (float)dt;
}

static void gesture_fire(AromaGestureObserver *o, AromaGestureType type,
                         AromaGestureTouch *t, float extra_a, float extra_b)
{
    if (!o->cb || !(o->mask & (uint32_t)type))
        return;
    AromaGestureEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.target = o->node;
    if (t) {
        ev.start_x = t->start_x;
        ev.start_y = t->start_y;
        ev.x = t->last_x;
        ev.y = t->last_y;
        ev.dx = t->last_x - t->start_x;
        ev.dy = t->last_y - t->start_y;
    }
    if (type == AROMA_GESTURE_PINCH) {
        ev.scale = extra_a;
    } else if (type == AROMA_GESTURE_SWIPE_LEFT ||
               type == AROMA_GESTURE_SWIPE_RIGHT ||
               type == AROMA_GESTURE_SWIPE_UP ||
               type == AROMA_GESTURE_SWIPE_DOWN) {
        ev.velocity_x = extra_a;
        ev.velocity_y = extra_b;
    }
    o->cb(&ev, o->user_data);
}

static void gesture_cancel_long(AromaGestureObserver *o)
{
    o->long_armed = false;
    o->long_gen++;
    if (o->long_timer) {
        aroma_timer_cancel(o->long_timer);
        o->long_timer = NULL;
    }
}

static void gesture_long_tick(void *user_data)
{
    AromaGestureObserver *o = (AromaGestureObserver *)user_data;
    if (!o || !o->active)
        return;
    o->long_timer = NULL;
    if (!o->enabled || !o->long_armed)
        return;
    o->long_armed = false;
    AromaGestureTouch *only = NULL;
    for (int f = 0; f < AROMA_GESTURE_MAX_TRACKED; f++) {
        if (o->fingers[f].id >= 0) {
            if (only)
                return;
            only = &o->fingers[f];
        }
    }
    if (!only || only->moved)
        return;
    gesture_fire(o, AROMA_GESTURE_LONG_PRESS, only, 0.0f, 0.0f);
}

static void gesture_arm_long(AromaGestureObserver *o)
{
    gesture_cancel_long(o);
    if (!(o->mask & AROMA_GESTURE_LONG_PRESS))
        return;
    o->long_armed = true;
    o->long_gen++;
    o->long_timer = aroma_timer_create(AROMA_GESTURE_LONG_PRESS_MS, false,
                                       gesture_long_tick, o);
}

static int gesture_dist(int x0, int y0, int x1, int y1)
{
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    return (int)(sqrtf(dx * dx + dy * dy) + 0.5f);
}

static void gesture_reset_touch(AromaGestureObserver *o, int id)
{
    gesture_free(o, id);
    if (o->fingers[0].id < 0 && o->fingers[1].id < 0) {
        o->pinch_on = false;
        o->pinch_suppress = false;
        gesture_cancel_long(o);
    } else if (o->pinch_on) {
        o->pinch_on = false;
        gesture_cancel_long(o);
        for (int i = 0; i < AROMA_GESTURE_MAX_TRACKED; i++) {
            AromaGestureTouch *t = &o->fingers[i];
            if (t->id >= 0) {
                t->start_x = t->last_x;
                t->start_y = t->last_y;
                t->down_ms = aroma_time_now_ms();
                t->moved = false;
                t->head = 0;
                t->count = 0;
                vt_add(t, t->last_x, t->last_y, t->down_ms);
            }
        }
    }
}

AromaGestureObserver *aroma_gesture_observe(AromaNode *node, uint32_t mask,
                                            AromaGestureCallback cb,
                                            void *user_data)
{
    if (!node || !cb || mask == 0)
        return NULL;
    for (int i = 0; i < AROMA_GESTURE_MAX_OBSERVERS; i++) {
        AromaGestureObserver *o = &s_observers[i];
        if (o->active)
            continue;
        memset(o, 0, sizeof(*o));
        o->active = true;
        o->enabled = true;
        o->node = node;
        o->node_id = node->node_id;
        o->mask = mask;
        o->cb = cb;
        o->user_data = user_data;
        for (int f = 0; f < AROMA_GESTURE_MAX_TRACKED; f++)
            o->fingers[f].id = -1;
        return o;
    }
    return NULL;
}

void aroma_gesture_unobserve(AromaGestureObserver *observer)
{
    if (!observer)
        return;
    gesture_cancel_long(observer);
    observer->active = false;
    observer->cb = NULL;
    observer->node = NULL;
}

void aroma_gesture_set_enabled(AromaGestureObserver *observer, bool enabled)
{
    if (!observer)
        return;
    observer->enabled = enabled;
    if (!enabled)
        gesture_cancel_long(observer);
}

static bool gesture_valid(AromaGestureObserver *o)
{
    return o->active && o->enabled && o->node && o->node->node_id == o->node_id;
}

void aroma_gesture_handle_touch(int id, int x, int y, int state)
{
    if (id < 0)
        return;
    uint64_t now = aroma_time_now_ms();
    bool is_down = (state == 1);
    bool is_up = (state == 0);
    bool is_cancel = is_up && (x < 0 || y < 0);
    for (int i = 0; i < AROMA_GESTURE_MAX_OBSERVERS; i++) {
        AromaGestureObserver *o = &s_observers[i];
        if (!gesture_valid(o))
            continue;
        if (is_down) {
            if (!gesture_inside(o->node, x, y))
                continue;
            AromaGestureTouch *t = gesture_alloc(o, id);
            if (!t)
                continue;
            t->start_x = x;
            t->start_y = y;
            t->last_x = x;
            t->last_y = y;
            t->down_ms = now;
            t->moved = false;
            vt_add(t, x, y, now);
            AromaGestureTouch *other = NULL;
            for (int f = 0; f < AROMA_GESTURE_MAX_TRACKED; f++) {
                if (o->fingers[f].id >= 0 && o->fingers[f].id != id)
                    other = &o->fingers[f];
            }
            if (other && (o->mask & AROMA_GESTURE_PINCH)) {
                o->pinch_on = true;
                o->pinch_down_dist = gesture_dist(t->last_x, t->last_y,
                                                  other->last_x, other->last_y);
                if (o->pinch_down_dist < 1)
                    o->pinch_down_dist = 1;
                gesture_cancel_long(o);
                gesture_fire(o, AROMA_GESTURE_PINCH, t,
                             1.0f, 0.0f);
            } else if (!other) {
                gesture_arm_long(o);
            } else {
                gesture_cancel_long(o);
            }
            continue;
        }
        AromaGestureTouch *t = gesture_find(o, id);
        if (!t)
            continue;
        if (is_cancel) {
            gesture_reset_touch(o, id);
            continue;
        }
        t->last_x = x;
        t->last_y = y;
        vt_add(t, x, y, now);
        int dx = x - t->start_x;
        int dy = y - t->start_y;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int slop = gesture_slop_px();
        if (!t->moved && (adx >= slop || ady >= slop)) {
            t->moved = true;
            gesture_cancel_long(o);
        }
        if (o->pinch_on && (o->mask & AROMA_GESTURE_PINCH)) {
            AromaGestureTouch *a = &o->fingers[0];
            AromaGestureTouch *b = &o->fingers[1];
            if (a->id >= 0 && b->id >= 0) {
                int dist = gesture_dist(a->last_x, a->last_y,
                                        b->last_x, b->last_y);
                float scale = (float)dist / (float)o->pinch_down_dist;
                o->pinch_suppress = true;
                gesture_fire(o, AROMA_GESTURE_PINCH, t, scale, 0.0f);
            }
            if (is_up)
                gesture_reset_touch(o, id);
            continue;
        }
        if (is_up) {
            if (o->pinch_suppress) {
                gesture_reset_touch(o, id);
                continue;
            }
            uint64_t held = now >= t->down_ms ? now - t->down_ms : 0;
            float vx = 0.0f, vy = 0.0f;
            vt_velocity(t, now, &vx, &vy);
            if (!t->moved && held <= AROMA_GESTURE_TAP_TIMEOUT_MS) {
                gesture_fire(o, AROMA_GESTURE_TAP, t, 0.0f, 0.0f);
                if ((o->mask & AROMA_GESTURE_DOUBLE_TAP) && o->has_last_tap &&
                    now - o->last_tap_ms <= AROMA_GESTURE_DOUBLE_GAP_MS) {
                    int pdx = x - o->last_tap_x;
                    int pdy = y - o->last_tap_y;
                    int apdx = pdx < 0 ? -pdx : pdx;
                    int apdy = pdy < 0 ? -pdy : pdy;
                    if (apdx <= slop && apdy <= slop)
                        gesture_fire(o, AROMA_GESTURE_DOUBLE_TAP, t, 0.0f, 0.0f);
                    o->has_last_tap = false;
                } else if (o->mask & AROMA_GESTURE_DOUBLE_TAP) {
                    o->has_last_tap = true;
                    o->last_tap_ms = now;
                    o->last_tap_x = x;
                    o->last_tap_y = y;
                }
            } else if (t->moved) {
                int swipe_min = gesture_swipe_min_px();
                if (adx >= swipe_min || ady >= swipe_min) {
                    AromaGestureType st = AROMA_GESTURE_TAP;
                    if (adx > ady)
                        st = dx > 0 ? AROMA_GESTURE_SWIPE_RIGHT : AROMA_GESTURE_SWIPE_LEFT;
                    else
                        st = dy > 0 ? AROMA_GESTURE_SWIPE_DOWN : AROMA_GESTURE_SWIPE_UP;
                    gesture_fire(o, st, t, vx, vy);
                }
            }
            gesture_reset_touch(o, id);
            continue;
        }
        if ((o->mask & AROMA_GESTURE_PAN) && t->moved)
            gesture_fire(o, AROMA_GESTURE_PAN, t, 0.0f, 0.0f);
    }
}
